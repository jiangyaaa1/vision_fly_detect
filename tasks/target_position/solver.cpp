#include "solver.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <vector>

namespace target_position
{
namespace
{
using Points3d = std::array<cv::Vec3d, 4>;
using Points2d = std::array<cv::Vec2d, 4>;
using Residual = cv::Vec<double, 8>;

bool finite(const cv::Vec3d & value)
{
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

cv::Matx33d skew(const cv::Vec3d & value)
{
  return {0, -value[2], value[1], value[2], 0, -value[0], -value[1], value[0], 0};
}

// Rodrigues 指数映射，避免调用 calib3d 的同名函数。
cv::Matx33d rotation_exp(const cv::Vec3d & value)
{
  double angle = cv::norm(value);
  auto K = skew(value);
  if (angle < 1e-8) return cv::Matx33d::eye() + K + 0.5 * (K * K);
  return cv::Matx33d::eye() + std::sin(angle) / angle * K +
         (1 - std::cos(angle)) / (angle * angle) * (K * K);
}

cv::Vec3d rotation_log(const cv::Matx33d & rotation)
{
  double cosine = (cv::trace(rotation) - 1) * 0.5;
  double angle = std::acos(std::max(-1.0, std::min(1.0, cosine)));
  cv::Vec3d axis{
    rotation(2, 1) - rotation(1, 2), rotation(0, 2) - rotation(2, 0),
    rotation(1, 0) - rotation(0, 1)};
  if (angle < 1e-8) return 0.5 * axis;
  if (CV_PI - angle > 1e-5) return angle / (2 * std::sin(angle)) * axis;

  // 接近180度时由对称部分恢复旋转轴。
  int index = 0;
  if (rotation(1, 1) > rotation(index, index)) index = 1;
  if (rotation(2, 2) > rotation(index, index)) index = 2;
  cv::Vec3d result{0, 0, 0};
  result[index] = std::sqrt(std::max(0.0, (rotation(index, index) + 1) * 0.5));
  if (result[index] < 1e-8) return {angle, 0, 0};
  for (int j = 0; j < 3; ++j) {
    if (j != index) result[j] = (rotation(index, j) + rotation(j, index)) / (4 * result[index]);
  }
  if (result.dot(axis) < 0) result = -result;
  return angle * result / cv::norm(result);
}

cv::Vec2d distort(const cv::Vec2d & point, const cv::Vec<double, 5> & d)
{
  double x = point[0], y = point[1];
  double r2 = x * x + y * y;
  double radial = 1 + d[0] * r2 + d[1] * r2 * r2 + d[4] * r2 * r2 * r2;
  return {
    x * radial + 2 * d[2] * x * y + d[3] * (r2 + 2 * x * x),
    y * radial + d[2] * (r2 + 2 * y * y) + 2 * d[3] * x * y};
}

bool undistort(const cv::Point2f & pixel, const tools::Config & config, cv::Vec2d & result)
{
  auto normalized = config.camera_matrix.inv() * cv::Vec3d(pixel.x, pixel.y, 1);
  if (!finite(normalized) || std::abs(normalized[2]) < 1e-12) return false;
  cv::Vec2d observed{normalized[0] / normalized[2], normalized[1] / normalized[2]};
  result = observed;
  for (int iteration = 0; iteration < 30; ++iteration) {
    auto error = distort(result, config.distort_coeffs) - observed;
    if (!std::isfinite(error[0]) || !std::isfinite(error[1])) return false;
    if (cv::norm(error) < 1e-11) return true;
    constexpr double step = 1e-6;
    cv::Matx22d jacobian;
    for (int column = 0; column < 2; ++column) {
      auto plus = result, minus = result;
      plus[column] += step;
      minus[column] -= step;
      auto derivative =
        (distort(plus, config.distort_coeffs) - distort(minus, config.distort_coeffs)) / (2 * step);
      jacobian(0, column) = derivative[0];
      jacobian(1, column) = derivative[1];
    }
    if (std::abs(cv::determinant(jacobian)) < 1e-12) return false;
    auto update = jacobian.inv() * error;
    result -= update;
    if (cv::norm(result) > 100) return false;
  }
  return cv::norm(distort(result, config.distort_coeffs) - observed) < 1e-8;
}

bool initialize(
  const Points3d & object_points, const Points2d & image_points, cv::Matx33d & rotation,
  cv::Vec3d & translation)
{
  // h33=1 的8元线性系统。目标以自身中心为原点，正深度时 h33 不为0。
  cv::Matx<double, 8, 8> A = cv::Matx<double, 8, 8>::zeros();
  cv::Vec<double, 8> b;
  for (int i = 0; i < 4; ++i) {
    double X = object_points[i][0], Y = object_points[i][1];
    double x = image_points[i][0], y = image_points[i][1];
    A(2 * i, 0) = X;
    A(2 * i, 1) = Y;
    A(2 * i, 2) = 1;
    A(2 * i, 6) = -x * X;
    A(2 * i, 7) = -x * Y;
    A(2 * i + 1, 3) = X;
    A(2 * i + 1, 4) = Y;
    A(2 * i + 1, 5) = 1;
    A(2 * i + 1, 6) = -y * X;
    A(2 * i + 1, 7) = -y * Y;
    b[2 * i] = x;
    b[2 * i + 1] = y;
  }
  cv::Vec<double, 8> h;
  if (!cv::solve(A, b, h, cv::DECOMP_SVD)) return false;
  cv::Vec3d first{h[0], h[3], h[6]}, second{h[1], h[4], h[7]};
  double denominator = cv::norm(first) + cv::norm(second);
  if (!std::isfinite(denominator) || denominator < 1e-12) return false;
  double scale = 2 / denominator;
  first *= scale;
  second *= scale;
  auto third = first.cross(second);
  cv::Matx33d approximate{first[0], second[0], third[0],  first[1], second[1],
                          third[1], first[2],  second[2], third[2]};
  cv::SVD decomposition(cv::Mat(approximate), cv::SVD::FULL_UV);
  cv::Matx33d U, Vt;
  decomposition.u.copyTo(cv::Mat(U, false));
  decomposition.vt.copyTo(cv::Mat(Vt, false));
  cv::Matx33d correction = cv::Matx33d::eye();
  correction(2, 2) = cv::determinant(U * Vt) < 0 ? -1 : 1;
  rotation = U * correction * Vt;
  translation = scale * cv::Vec3d(h[2], h[5], 1);
  return finite(translation) && cv::checkRange(cv::Mat(rotation));
}

bool residual(
  const Points3d & object_points, const std::array<cv::Point2f, 4> & observed,
  const tools::Config & config, const cv::Matx33d & rotation, const cv::Vec3d & translation,
  Residual & errors)
{
  for (int i = 0; i < 4; ++i) {
    auto camera = rotation * object_points[i] + translation;
    if (!finite(camera) || camera[2] <= 1e-6) return false;
    auto normalized =
      distort({camera[0] / camera[2], camera[1] / camera[2]}, config.distort_coeffs);
    auto pixel = config.camera_matrix * cv::Vec3d(normalized[0], normalized[1], 1);
    if (!finite(pixel) || std::abs(pixel[2]) < 1e-12) return false;
    errors[2 * i] = pixel[0] / pixel[2] - observed[i].x;
    errors[2 * i + 1] = pixel[1] / pixel[2] - observed[i].y;
  }
  return cv::checkRange(cv::Mat(errors));
}

double refine(
  const Points3d & object_points, const std::array<cv::Point2f, 4> & observed,
  const tools::Config & config, cv::Matx33d & rotation, cv::Vec3d & translation)
{
  Residual errors;
  if (!residual(object_points, observed, config, rotation, translation, errors))
    return std::numeric_limits<double>::infinity();
  double cost = errors.dot(errors), lambda = 1e-3;
  for (int iteration = 0; iteration < 60; ++iteration) {
    cv::Matx<double, 8, 6> jacobian;
    for (int point = 0; point < 4; ++point) {
      // Brown5投影的解析导数，替代每轮12次整组点投影；不削减初值或精度门限。
      const auto rotated = rotation * object_points[point];
      const auto camera = rotated + translation;
      const double inverse_z = 1 / camera[2];
      const double x = camera[0] * inverse_z, y = camera[1] * inverse_z;
      const double r2 = x * x + y * y;
      const auto & d = config.distort_coeffs;
      const double radial = 1 + d[0] * r2 + d[1] * r2 * r2 + d[4] * r2 * r2 * r2;
      const double common = d[0] + 2 * d[1] * r2 + 3 * d[4] * r2 * r2;
      const double dx = 2 * x * common, dy = 2 * y * common;
      const cv::Matx22d brown{
        radial + x * dx + 2 * d[2] * y + 6 * d[3] * x, x * dy + 2 * d[2] * x + 2 * d[3] * y,
        y * dx + 2 * d[2] * x + 2 * d[3] * y, radial + y * dy + 6 * d[2] * y + 2 * d[3] * x};
      const cv::Matx<double, 2, 3> perspective{inverse_z, 0,         -x * inverse_z,
                                               0,         inverse_z, -y * inverse_z};
      const cv::Matx22d focal{config.camera_matrix(0, 0), 0, 0, config.camera_matrix(1, 1)};
      const auto translation_jacobian = focal * brown * perspective;
      const auto rotation_jacobian = translation_jacobian * (-skew(rotated));
      for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
          jacobian(2 * point + row, column) = rotation_jacobian(row, column);
          jacobian(2 * point + row, column + 3) = translation_jacobian(row, column);
        }
      }
    }
    auto normal = jacobian.t() * jacobian;
    auto gradient = jacobian.t() * errors;
    for (int i = 0; i < 6; ++i) normal(i, i) += lambda * std::max(normal(i, i), 1e-9);
    cv::Vec<double, 6> update;
    if (!cv::solve(normal, -gradient, update, cv::DECOMP_CHOLESKY)) {
      lambda *= 10;
      if (lambda > 1e12) break;
      continue;
    }
    cv::Vec3d angle{update[0], update[1], update[2]};
    cv::Vec3d offset{update[3], update[4], update[5]};
    auto candidate_rotation = rotation_exp(angle) * rotation;
    auto candidate_translation = translation + offset;
    Residual candidate_errors;
    bool valid = residual(
      object_points, observed, config, candidate_rotation, candidate_translation, candidate_errors);
    double candidate_cost =
      valid ? candidate_errors.dot(candidate_errors) : std::numeric_limits<double>::infinity();
    if (candidate_cost < cost) {
      double improvement = cost - candidate_cost;
      rotation = candidate_rotation;
      translation = candidate_translation;
      errors = candidate_errors;
      cost = candidate_cost;
      lambda = std::max(1e-12, lambda / 3);
      if (improvement < 1e-12 || cv::norm(update) < 1e-9) break;
    } else {
      lambda *= 10;
      if (lambda > 1e12) break;
    }
  }
  return cost;
}

bool valid_geometry(const Target & target, const tools::Config & config, std::string & reason)
{
  double cross_sign = 0, area = 0;
  for (int i = 0; i < 4; ++i) {
    const auto & a = target.points[i];
    const auto & b = target.points[(i + 1) % 4];
    const auto & c = target.points[(i + 2) % 4];
    if (
      !std::isfinite(a.x) || !std::isfinite(a.y) || a.x < 0 || a.y < 0 ||
      a.x >= config.image_width || a.y >= config.image_height) {
      reason = "corner_nonfinite_or_outside_image";
      return false;
    }
    auto first = b - a, second = c - b;
    double cross = first.x * second.y - first.y * second.x;
    if (cv::norm(first) < 2 || std::abs(cross) < 1e-3 || (i > 0 && cross * cross_sign <= 0)) {
      reason = "corners_not_convex_or_degenerate";
      return false;
    }
    cross_sign = cross;
    area += static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x;
  }
  // TL,TR,BR,BL 在像素右下坐标系中应为正向；不替调用方猜测语义。
  if (area * 0.5 < config.min_quad_area) {
    reason = "corner_order_or_area_invalid";
    return false;
  }
  return true;
}
}  // namespace

Solver::Solver(const tools::Config & config) : config_(config) {}

bool Solver::solve(Target & target, std::string & reason) const
{
  reason.clear();
  target.world_valid = false;
  target.absolute_valid = false;
  if (config_.pnp_min_confidence > 0 &&
      (!std::isfinite(target.detection.score) || target.detection.score < config_.pnp_min_confidence)) {
    reason = "pnp_confidence_too_low";
    return false;
  }
  double physical_width = config_.target_width, physical_height = config_.target_height;
  if (config_.model_type == "szurpv_infantry_v8n" && config_.class_id == -1) {
    const int category = target.detection.class_id;
    if (
      category < 0 || category >= 9 || config_.armor_width_by_class[category] <= 0 ||
      config_.armor_height_by_class[category] <= 0) {
      reason = "class_dimensions_missing";
      return false;
    }
    physical_width = config_.armor_width_by_class[category];
    physical_height = config_.armor_height_by_class[category];
  }
  if (!(physical_width > 0) || !(physical_height > 0) || !config_.calibration_valid) {
    reason = "target_size_or_calibration_missing";
    return false;
  }
  if (!valid_geometry(target, config_, reason)) return false;
  double width = physical_width / 2, height = physical_height / 2;
  const Points3d object_points{
    cv::Vec3d{-width, -height, 0}, cv::Vec3d{width, -height, 0}, cv::Vec3d{width, height, 0},
    cv::Vec3d{-width, height, 0}};
  Points2d normalized;
  for (int i = 0; i < 4; ++i) {
    if (!undistort(target.points[i], config_, normalized[i])) {
      reason = "undistortion_failed";
      return false;
    }
  }
  cv::Matx33d initial_rotation;
  cv::Vec3d initial_translation;
  if (!initialize(object_points, normalized, initial_rotation, initial_translation)) {
    reason = "homography_initialization_failed";
    return false;
  }
  cv::Matx33d best_rotation;
  cv::Vec3d best_translation;
  double best_cost = std::numeric_limits<double>::infinity();
  // 近平行平面存在弱可观测性；从几个不同倾角出发，降低单一局部极小的影响。
  // 这并不等价于 IPPE 的双解穷举，不能消除对称目标的物理方向歧义。
  const std::array<cv::Vec3d, 5> offsets{
    cv::Vec3d{0, 0, 0}, cv::Vec3d{0.35, 0, 0}, cv::Vec3d{-0.35, 0, 0}, cv::Vec3d{0, 0.35, 0},
    cv::Vec3d{0, -0.35, 0}};
  for (const auto & offset : offsets) {
    auto rotation = rotation_exp(offset) * initial_rotation;
    auto translation = initial_translation;
    double cost = refine(object_points, target.points, config_, rotation, translation);
    if (cost < best_cost) {
      best_cost = cost;
      best_rotation = rotation;
      best_translation = translation;
    }
  }
  if (!std::isfinite(best_cost) || !finite(best_translation)) {
    reason = "pnp_optimization_failed";
    return false;
  }
  double distance = cv::norm(best_translation);
  if (distance < config_.min_distance || distance > config_.max_distance) {
    reason = "distance_out_of_range";
    return false;
  }
  // 每个角点的欧氏像素误差的均方根，而不是每个坐标分量的 RMS。
  double rms = std::sqrt(best_cost / 4);
  if (rms > config_.max_reprojection_error) {
    reason = "reprojection_error_too_large";
    return false;
  }
  target.xyz_in_camera = best_translation;
  target.rvec = rotation_log(best_rotation);
  target.reprojection_error = rms;
  transform(target, nullptr);
  return true;
}

void Solver::transform(Target & target, const Pose * pose) const
{
  target.world_valid = false;
  target.absolute_valid = false;
  target.relative_xyz_in_world = {0, 0, 0};
  target.xyz_in_world = {0, 0, 0};
  target.xyz_in_body = config_.R_camera2body * target.xyz_in_camera + config_.t_camera2body;
  if (!pose || !config_.extrinsics_valid) return;
  try {
    auto rotation = tools::quaternion_to_rotation(pose->q_body2world);
    target.relative_xyz_in_world = rotation * target.xyz_in_body;
    target.world_valid = finite(target.relative_xyz_in_world);
    if (target.world_valid && pose->position_valid && finite(pose->position_in_world)) {
      target.xyz_in_world = pose->position_in_world + target.relative_xyz_in_world;
      target.absolute_valid = finite(target.xyz_in_world);
    }
  } catch (const std::exception &) {
    // 无效姿态保留相机/机体结果，不能把单位旋转伪装成世界坐标。
  }
}
}  // namespace target_position
