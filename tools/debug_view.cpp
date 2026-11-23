#include "debug_view.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <opencv2/imgproc.hpp>
#include <sstream>

namespace tools
{
namespace
{
void label(cv::Mat & image, const std::string & value, int y, const cv::Scalar & color)
{
  cv::putText(image, value, {5, y}, cv::FONT_HERSHEY_SIMPLEX, 0.45, color, 1, cv::LINE_AA);
}
}  // namespace

DebugView::DebugView(const cv::Mat & bgr, bool feature_panels, bool rgb, bool private_buffer)
: rgb_(rgb), overlay_(private_buffer ? bgr : bgr.clone())
{
  if (feature_panels) {
    edges_ = cv::Mat::zeros(bgr.size(), CV_8UC1);
    binary_ = cv::Mat::zeros(bgr.size(), CV_8UC1);
  }
}

cv::Scalar DebugView::color(cv::Scalar bgr) const
{
  if (rgb_) std::swap(bgr[0], bgr[2]);
  return bgr;
}

void DebugView::search_roi(const cv::Rect & roi)
{
  if (roi.empty() || roi != (roi & cv::Rect(0, 0, overlay_.cols, overlay_.rows))) return;
  cv::rectangle(overlay_, roi, color({180, 180, 180}), 1);
  const std::string text = roi.width == 640 ? "FULL" : "ZOOM " + std::to_string(roi.width) +
    "x" + std::to_string(roi.height);
  cv::putText(overlay_, text, {roi.x + 4, std::min(overlay_.rows - 4, roi.y + roi.height - 6)},
              cv::FONT_HERSHEY_SIMPLEX, 0.4, color({180, 180, 180}), 1);
}

void DebugView::detection(const target_position::Detection & detection)
{
  const auto box = detection.box & cv::Rect(0, 0, overlay_.cols, overlay_.rows);
  if (box.empty()) return;
  cv::rectangle(overlay_, box, color({255, 255, 0}), 1);
  std::ostringstream text;
  text << "class=" << detection.class_id << " " << std::fixed << std::setprecision(2)
       << detection.score;
  if (detection.color_id >= 0) text << " color=" << detection.color_id;
  cv::putText(
    overlay_, text.str(), {box.x, std::max(12, box.y - 4)}, cv::FONT_HERSHEY_SIMPLEX, 0.4,
    color({255, 255, 0}), 1);
  for (const auto & point : detection.keypoints) {
    if (
      std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0 && point.y >= 0 &&
      point.x < overlay_.cols && point.y < overlay_.rows) {
      cv::circle(overlay_, point, 2, color({0, 255, 255}), -1);
    }
  }
}

void DebugView::features(const target_position::DetectorDebug & debug)
{
  if (debug.roi.empty()) return;
  if (debug.roi != (debug.roi & cv::Rect(0, 0, overlay_.cols, overlay_.rows))) return;
  cv::rectangle(overlay_, debug.roi, color({255, 120, 0}), 1);
  if (!edges_.empty() && !debug.edges.empty())
    cv::bitwise_or(edges_(debug.roi), debug.edges, edges_(debug.roi));
  if (!binary_.empty() && !debug.binary.empty())
    cv::bitwise_or(binary_(debug.roi), debug.binary, binary_(debug.roi));
  if (!debug.contours.empty())
    cv::drawContours(overlay_, debug.contours, -1, color({255, 0, 255}), 1);
}

void DebugView::corners(const target_position::Target & target, bool selected)
{
  const cv::Scalar pen = color(selected ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 165, 255));
  for (int i = 0; i < 4; i++) {
    cv::line(overlay_, target.points[i], target.points[(i + 1) % 4], pen, 2);
    cv::circle(overlay_, target.points[i], 3, pen, -1);
    cv::putText(
      overlay_, std::to_string(i), target.points[i] + cv::Point2f(4, -4), cv::FONT_HERSHEY_SIMPLEX,
      0.45, color({0, 0, 255}), 1);
  }
}

void DebugView::status(
  const std::string & status, const target_position::Target * target, double process_fps,
  double inference_ms)
{
  cv::rectangle(
    overlay_, {0, 0}, {overlay_.cols - 1, target ? 82 : (process_fps > 0 ? 64 : 45)}, {15, 15, 15},
    -1);
  std::ostringstream headline;
  headline << status;
  if (target) {
    headline << " C" << target->detection.class_id;
    if (process_fps > 0)
      headline << " | " << std::fixed << std::setprecision(1) << process_fps << " FPS";
    headline << " | RMS " << std::fixed << std::setprecision(2) << target->reprojection_error
             << "px";
  }
  label(overlay_, headline.str(), 17, {255, 255, 255});
  if (target) {
    const auto coordinates = [](const char * name, const cv::Vec3d & xyz) {
      std::ostringstream text;
      text << std::fixed << std::setprecision(3) << name << " " << xyz[0] << ", " << xyz[1] << ", "
           << xyz[2];
      return text.str();
    };
    label(overlay_, coordinates("Cam XYZ(m):", target->xyz_in_camera), 36, {0, 255, 0});
    label(overlay_, coordinates("Body FRD(m):", target->xyz_in_body), 54, {0, 255, 0});
    if (target->world_valid) {
      label(
        overlay_,
        coordinates(
          status == "ok_static_pose" ? "NED [STATIC](m):" : "NED(m):",
          target->relative_xyz_in_world),
        72, {0, 255, 0});
    } else {
      label(overlay_, "NED unavailable: no matching attitude", 72, color({0, 165, 255}));
    }
  } else {
    label(overlay_, "No valid 3D output", 36, color({0, 165, 255}));
  }
  if (process_fps > 0 && !target) {
    std::ostringstream timing;
    timing << std::fixed << std::setprecision(1) << "Process " << process_fps << " FPS   NN "
           << inference_ms << " ms";
    label(overlay_, timing.str(), 56, {255, 255, 255});
  }
}

const cv::Mat & DebugView::overlay() const { return overlay_; }

cv::Mat DebugView::sheet() const
{
  cv::Mat edge_bgr, binary_bgr, output;
  cv::cvtColor(edges_, edge_bgr, cv::COLOR_GRAY2BGR);
  cv::cvtColor(binary_, binary_bgr, cv::COLOR_GRAY2BGR);
  cv::rectangle(edge_bgr, {0, 0}, {edge_bgr.cols - 1, 24}, {15, 15, 15}, -1);
  cv::rectangle(binary_bgr, {0, 0}, {binary_bgr.cols - 1, 24}, {15, 15, 15}, -1);
  label(edge_bgr, "Canny edges in YOLO ROI", 17, {255, 255, 255});
  label(binary_bgr, "Otsu binary in YOLO ROI", 17, {255, 255, 255});
  std::vector<cv::Mat> panels{overlay_, edge_bgr, binary_bgr};
  cv::hconcat(panels, output);
  return output;
}
}  // namespace tools
