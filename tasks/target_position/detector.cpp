#include "detector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <vector>

namespace target_position
{
namespace
{
using Points = std::array<cv::Point2f, 4>;

bool valid_quad(
  const Points & points, const cv::Size & size, double min_area, double min_edge,
  std::string & reason)
{
  std::vector<cv::Point2f> contour(points.begin(), points.end());
  for (int i = 0; i < 4; i++) {
    const auto & p = points[i];
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
      reason = "non_finite_corner";
      return false;
    }
    if (p.x < 0 || p.y < 0 || p.x >= size.width || p.y >= size.height) {
      reason = "corner_outside_image";
      return false;
    }
    if (cv::norm(p - points[(i + 1) % 4]) < min_edge) {
      reason = "corner_edge_too_short";
      return false;
    }
  }
  // 自交、凹四边形以及重复点不具有有效的一一物理点对应关系。
  if (!cv::isContourConvex(contour)) {
    reason = "corners_not_convex_or_not_cyclic";
    return false;
  }
  if (std::abs(cv::contourArea(contour)) < min_area) {
    reason = "corner_area_too_small";
    return false;
  }
  return true;
}

Points ordered_points(const std::vector<cv::Point> & polygon, const cv::Point & offset)
{
  Points points;
  cv::Point2f center(0, 0);
  for (int i = 0; i < 4; i++) {
    points[i] = cv::Point2f(polygon[i]) + cv::Point2f(offset);
    center += points[i] * 0.25f;
  }
  // 图像y向下，atan2升序给出顺时针环；近正立前提下，x+y最小点是左上。
  std::sort(points.begin(), points.end(), [&](const cv::Point2f & a, const cv::Point2f & b) {
    return std::atan2(a.y - center.y, a.x - center.x) < std::atan2(b.y - center.y, b.x - center.x);
  });
  auto first = std::min_element(
    points.begin(), points.end(),
    [](const cv::Point2f & a, const cv::Point2f & b) { return a.x + a.y < b.x + b.y; });
  std::rotate(points.begin(), first, points.end());
  return points;
}

bool upright(const Points & points, double max_roll_deg)
{
  const auto top = points[1] - points[0];
  const auto bottom = points[2] - points[3];
  const auto left = points[3] - points[0];
  const auto right = points[2] - points[1];
  if (top.x <= 0 || bottom.x <= 0 || left.y <= 0 || right.y <= 0) return false;
  const double max_angle = max_roll_deg * CV_PI / 180.0;
  // 同时限制四条边，拒绝物理角点语义无法由图像方向唯一确定的候选。
  return std::abs(std::atan2(top.y, top.x)) <= max_angle &&
         std::abs(std::atan2(bottom.y, bottom.x)) <= max_angle &&
         std::abs(std::atan2(left.x, left.y)) <= max_angle &&
         std::abs(std::atan2(right.x, right.y)) <= max_angle;
}

double intersection_over_union(const cv::Rect & a, const cv::Rect & b)
{
  const cv::Rect overlap = a & b;
  const double intersection = static_cast<double>(overlap.width) * overlap.height;
  const double union_area = static_cast<double>(a.width) * a.height +
                            static_cast<double>(b.width) * b.height - intersection;
  return union_area > 0 ? intersection / union_area : 0;
}

bool candidate_score(
  const Points & points, const cv::Size & size, const cv::Rect & roi, const Detection & detection,
  const tools::Config & config, double & score)
{
  std::string reason;
  if (!valid_quad(points, size, config.min_quad_area, config.min_corner_edge, reason)) return false;
  if (!upright(points, config.max_roll_deg)) return false;
  for (const auto & p : points) {
    // 触及图像或裁剪边界的轮廓可能由裁剪人为封口，不作为真实目标。
    if (
      p.x <= 1 || p.y <= 1 || p.x >= size.width - 2 || p.y >= size.height - 2 || p.x <= roi.x ||
      p.y <= roi.y || p.x >= roi.x + roi.width - 1 || p.y >= roi.y + roi.height - 1) {
      return false;
    }
  }
  const std::vector<cv::Point2f> contour(points.begin(), points.end());
  const double area = std::abs(cv::contourArea(contour));
  const double box_area = static_cast<double>(detection.box.width) * detection.box.height;
  const double roi_area = static_cast<double>(roi.width) * roi.height;
  // 面积和bbox一致性共同防止把目标内部数字、纹理小方块当成外边界。
  if (area / box_area < config.min_fill_ratio || area / roi_area < 0.05) return false;
  const double iou = intersection_over_union(cv::boundingRect(contour), detection.box);
  if (iou < 0.45) return false;
  score = 2 * iou + std::min(area / box_area, 1.0);
  return true;
}
}  // namespace

Detector::Detector(const tools::Config & config) : config_(config)
{
  if (config.corner_mode != "contour" && config.corner_mode != "keypoints") {
    throw std::invalid_argument("corner_mode must be contour or keypoints");
  }
  if (
    !std::isfinite(config.roi_expand) || config.roi_expand < 0 || config.roi_expand > 1 ||
    !std::isfinite(config.min_quad_area) || config.min_quad_area <= 0 ||
    !std::isfinite(config.min_corner_edge) || config.min_corner_edge < 2 ||
    config.min_corner_edge > 100 ||
    !std::isfinite(config.min_fill_ratio) || config.min_fill_ratio <= 0 ||
    config.min_fill_ratio > 1 || !std::isfinite(config.max_roll_deg) || config.max_roll_deg <= 0 ||
    config.max_roll_deg >= 45) {
    throw std::invalid_argument("invalid corner extraction parameters");
  }
  for (int i = 0; i < 4; i++) {
    if (config.keypoint_indices[i] < 0) throw std::invalid_argument("negative keypoint index");
    for (int j = 0; j < i; j++) {
      if (config.keypoint_indices[i] == config.keypoint_indices[j]) {
        throw std::invalid_argument("duplicate keypoint index");
      }
    }
  }
}

bool Detector::detect(
  const cv::Mat & bgr, const Detection & detection, Target & target, std::string & reason,
  DetectorDebug * debug) const
{
  // 失败时清空旧结果，禁止上一帧坐标混入当前检测。
  target = Target{};
  if (debug) *debug = DetectorDebug{};
  reason.clear();
  if (bgr.empty() || bgr.type() != CV_8UC3) {
    reason = "image_must_be_nonempty_bgr8";
    return false;
  }
  const auto & box = detection.box;
  if (
    box.width <= 0 || box.height <= 0 || box.x < 0 || box.y < 0 ||
    static_cast<int64_t>(box.x) + box.width > bgr.cols ||
    static_cast<int64_t>(box.y) + box.height > bgr.rows) {
    reason = "invalid_or_clipped_detection_box";
    return false;
  }

  Points best_points;
  if (config_.corner_mode == "keypoints") {
    for (int i = 0; i < 4; i++) {
      const auto index = static_cast<std::size_t>(config_.keypoint_indices[i]);
      if (index >= detection.keypoints.size()) {
        reason = "missing_keypoint";
        return false;
      }
      best_points[i] = detection.keypoints[index];
    }
    // 保留训练定义的语义顺序；不能对旋转目标按图像上下重新排序。
    if (!valid_quad(best_points, bgr.size(), config_.min_quad_area, config_.min_corner_edge, reason))
      return false;
  } else {
    const int dx = static_cast<int>(std::ceil(box.width * config_.roi_expand));
    const int dy = static_cast<int>(std::ceil(box.height * config_.roi_expand));
    const int x0 = std::max(0, box.x - dx);
    const int y0 = std::max(0, box.y - dy);
    const int x1 = static_cast<int>(std::min<int64_t>(bgr.cols, int64_t(box.x) + box.width + dx));
    const int y1 = static_cast<int>(std::min<int64_t>(bgr.rows, int64_t(box.y) + box.height + dy));
    const cv::Rect roi(x0, y0, x1 - x0, y1 - y0);
    cv::Mat gray, blurred, edges, bright, dark;
    cv::cvtColor(bgr(roi), gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, blurred, {3, 3}, 0);
    cv::Canny(blurred, edges, 50, 150);
    cv::threshold(blurred, bright, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::bitwise_not(bright, dark);
    if (debug) {
      debug->roi = roi;
      debug->edges = edges;
      debug->binary = bright;
    }

    double best_score = -std::numeric_limits<double>::infinity();
    for (const auto & binary : {edges, bright, dark}) {
      std::vector<std::vector<cv::Point>> contours;
      cv::findContours(binary.clone(), contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
      for (const auto & contour : contours) {
        if (std::abs(cv::contourArea(contour)) < config_.min_quad_area) continue;
        if (debug && debug->contours.size() < 100) {
          std::vector<cv::Point> original;
          for (const auto & point : contour) original.push_back(point + roi.tl());
          debug->contours.push_back(std::move(original));
        }
        std::vector<cv::Point> polygon;
        cv::approxPolyDP(contour, polygon, 0.02 * cv::arcLength(contour, true), true);
        if (polygon.size() != 4 || !cv::isContourConvex(polygon)) continue;
        auto points = ordered_points(polygon, roi.tl());
        double score;
        if (!candidate_score(points, bgr.size(), roi, detection, config_, score)) continue;
        if (score > best_score) {
          best_score = score;
          best_points = points;
        }
      }
    }
    if (!std::isfinite(best_score)) {
      reason = "no_valid_physical_quadrilateral";
      return false;
    }

    // 在ROI灰度上精化，并限制位移和重新验证，避免精化跳到附近纹理。
    std::vector<cv::Point2f> refined;
    bool enough_border = true;
    for (const auto & p : best_points) {
      const cv::Point2f local = p - cv::Point2f(roi.tl());
      refined.push_back(local);
      if (local.x < 5 || local.y < 5 || local.x >= gray.cols - 5 || local.y >= gray.rows - 5) {
        enough_border = false;
      }
    }
    if (enough_border) {
      cv::cornerSubPix(
        gray, refined, {3, 3}, {-1, -1},
        cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 20, 0.03));
      Points refined_points;
      bool small_displacement = true;
      for (int i = 0; i < 4; i++) {
        refined_points[i] = refined[i] + cv::Point2f(roi.tl());
        if (cv::norm(refined_points[i] - best_points[i]) > 3) small_displacement = false;
      }
      double refined_score;
      if (
        small_displacement &&
        candidate_score(refined_points, bgr.size(), roi, detection, config_, refined_score)) {
        best_points = refined_points;
      }
    }
  }

  target.detection = detection;
  target.points = best_points;
  return true;
}
}  // namespace target_position
