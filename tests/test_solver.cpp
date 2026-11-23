#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <opencv2/calib3d.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "tasks/target_position/solver.hpp"

namespace
{
void require(bool condition, const std::string & message)
{
  if (!condition) throw std::runtime_error(message);
}

tools::Config configuration()
{
  tools::Config config;
  config.image_width = 1280;
  config.image_height = 720;
  config.camera_matrix = {900, 0, 640, 0, 910, 360, 0, 0, 1};
  config.distort_coeffs = {-0.12, 0.025, 0.001, -0.002, 0.005};
  config.calibration_valid = true;
  config.extrinsics_valid = true;
  config.target_width = 0.4;
  config.target_height = 0.25;
  config.min_quad_area = 20;
  config.max_reprojection_error = 0.6;
  config.max_distance = 50;
  return config;
}

std::vector<cv::Point3d> physical_points(const tools::Config & config)
{
  double w = config.target_width / 2, h = config.target_height / 2;
  return {{-w, -h, 0}, {w, -h, 0}, {w, h, 0}, {-w, h, 0}};
}

target_position::Target projected(
  const tools::Config & config, const cv::Vec3d & rvec, const cv::Vec3d & translation)
{
  std::vector<cv::Point2d> pixels;
  cv::projectPoints(
    physical_points(config), rvec, translation, config.camera_matrix, config.distort_coeffs,
    pixels);
  target_position::Target target;
  for (int i = 0; i < 4; ++i) target.points[i] = cv::Point2f(pixels[i]);
  return target;
}

void exact_projection_test()
{
  auto config = configuration();
  target_position::Solver solver(config);
  std::vector<cv::Vec3d> rotations{
    {0, 0, 0}, {0.15, -0.4, 0.2}, {-0.35, 0.55, -0.15}, {0.6, 0.1, 0.35}, {-0.1, -0.9, 0.15}};
  std::vector<double> distances{0.9, 2, 5, 10, 20};
  double max_position_error = 0, max_opencv_difference = 0, max_rms = 0;
  int cases = 0;
  for (const auto & rotation : rotations) {
    for (double distance : distances) {
      cv::Vec3d translation{distance * 0.07, distance * -0.035, distance};
      auto target = projected(config, rotation, translation);
      std::string reason;
      require(solver.solve(target, reason), "exact PnP failed: " + reason);
      double error = cv::norm(target.xyz_in_camera - translation);
      max_position_error = std::max(max_position_error, error);
      max_rms = std::max(max_rms, target.reprojection_error);
      require(error < 0.001, "exact projected position error exceeds 1mm");
      require(target.reprojection_error < 1e-3, "exact projected residual exceeds 0.001px");
      cv::Vec3d reference_rvec, reference_translation;
      std::vector<cv::Point2f> pixels(target.points.begin(), target.points.end());
      require(
        cv::solvePnP(
          physical_points(config), pixels, config.camera_matrix, config.distort_coeffs,
          reference_rvec, reference_translation, false, cv::SOLVEPNP_IPPE),
        "OpenCV reference PnP failed");
      max_opencv_difference =
        std::max(max_opencv_difference, cv::norm(target.xyz_in_camera - reference_translation));
      require(
        cv::norm(target.xyz_in_camera - reference_translation) < 0.002,
        "portable PnP differs from OpenCV IPPE by over 2mm");
      ++cases;
    }
  }
  std::cout << "exact " << cases << " cases; max position error=" << max_position_error
            << "m; max OpenCV difference=" << max_opencv_difference << "m; RMS=" << max_rms
            << "px\n";
}

void noise_and_rejection_test()
{
  auto config = configuration();
  target_position::Solver solver(config);
  cv::Vec3d translation{0.25, -0.1, 3};
  auto target = projected(config, {0.2, -0.4, 0.1}, translation);
  const std::array<cv::Point2f, 4> noise{
    cv::Point2f{0.2f, -0.15f}, cv::Point2f{-0.1f, 0.12f}, cv::Point2f{0.15f, 0.2f},
    cv::Point2f{-0.2f, -0.1f}};
  for (int i = 0; i < 4; ++i) target.points[i] += noise[i];
  std::string reason;
  require(solver.solve(target, reason), "small-noise PnP failed: " + reason);
  require(
    cv::norm(target.xyz_in_camera - translation) < 0.02,
    "small-noise position error exceeds 2cm at 3m");
  std::cout << "0.2px noise: position error=" << cv::norm(target.xyz_in_camera - translation)
            << "m, RMS=" << target.reprojection_error << "px\n";

  auto valid = projected(config, {0.2, -0.4, 0.1}, translation);
  target = valid;
  target.points[0].x = std::numeric_limits<float>::quiet_NaN();
  require(!solver.solve(target, reason), "NaN corner accepted");
  target = valid;
  target.points[0].x = -1;
  require(!solver.solve(target, reason), "negative/missing corner accepted");
  target = valid;
  target.points[2] = target.points[1];
  require(!solver.solve(target, reason), "duplicate corner accepted");
  target = valid;
  std::swap(target.points[1], target.points[2]);
  require(!solver.solve(target, reason), "self-intersecting corner order accepted");
  target = valid;
  std::reverse(target.points.begin(), target.points.end());
  require(!solver.solve(target, reason), "reversed corner order accepted");
  target = valid;
  target.points[2] += cv::Point2f(18, -13);
  require(!solver.solve(target, reason), "large erroneous corner accepted");
  require(reason == "reprojection_error_too_large", "unexpected bad-corner rejection: " + reason);
  config.max_distance = 2;
  require(!target_position::Solver(config).solve(valid, reason), "distance limit not enforced");
  config.calibration_valid = false;
  require(!target_position::Solver(config).solve(valid, reason), "missing calibration accepted");
}

void multiclass_sizes_test()
{
  auto config = configuration();
  config.model_type = "szurpv_infantry_v8n";
  config.class_id = -1;
  config.corner_mode = "keypoints";
  config.keypoint_indices = {0, 3, 2, 1};
  config.image_width = config.calibration_width = 640;
  config.image_height = config.calibration_height = 480;
  config.camera_matrix = {800, 0, 320, 0, 800, 240, 0, 0, 1};
  for (int i = 0; i < 9; ++i) {
    config.armor_width_by_class[i] = i == 1 || i == 7 ? 0.230 : 0.135;
    config.armor_height_by_class[i] = 0.056;
  }
  tools::validate_config(config);
  target_position::Solver solver(config);
  const cv::Vec3d expected{0.1, -0.05, 2.0};
  std::string reason;
  for (int i = 0; i < 9; ++i) {
    auto projection = config;
    projection.target_width = config.armor_width_by_class[i];
    projection.target_height = config.armor_height_by_class[i];
    auto target = projected(projection, {0.15, -0.3, 0.05}, expected);
    target.detection.class_id = i;
    require(solver.solve(target, reason), "per-class PnP: " + reason);
    require(cv::norm(target.xyz_in_camera - expected) < 0.001, "wrong per-class physical size");
  }
  auto target = projected(config, {0.1, -0.2, 0}, expected);
  target.detection.class_id = 7;
  config.armor_width_by_class[7] = config.armor_height_by_class[7] = 0;
  require(
    !target_position::Solver(config).solve(target, reason) && reason == "class_dimensions_missing",
    "unknown-size class accepted");
  auto invalid = config;
  invalid.camera_matrix(0, 0) = 0;
  bool rejected = false;
  try {
    tools::validate_config(invalid);
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "all-class mode bypassed camera calibration validation");
}

void transform_test()
{
  auto config = configuration();
  config.t_camera2body = {0.1, 0.2, -0.05};
  target_position::Solver solver(config);
  target_position::Target target;
  target.xyz_in_camera = {0.3, -0.2, 2};
  solver.transform(target, nullptr);
  require(
    cv::norm(target.xyz_in_body - cv::Vec3d(2.1, 0.5, -0.25)) < 1e-12,
    "camera FRD transform or lever arm wrong");
  require(!target.world_valid && !target.absolute_valid, "missing attitude marked world valid");
  target_position::Pose pose;
  double half_sqrt = std::sqrt(0.5);
  pose.q_body2world = {half_sqrt, 0, 0, half_sqrt};  // NED yaw +90deg。
  pose.position_in_world = {10, 20, 30};
  pose.position_valid = true;
  solver.transform(target, &pose);
  require(target.world_valid && target.absolute_valid, "valid pose failed");
  require(
    cv::norm(target.relative_xyz_in_world - cv::Vec3d(-0.5, 2.1, -0.25)) < 1e-12,
    "body-to-world rotation wrong");
  require(
    cv::norm(target.xyz_in_world - cv::Vec3d(9.5, 22.1, 29.75)) < 1e-12,
    "absolute world position wrong");
  pose.position_valid = false;
  solver.transform(target, &pose);
  require(target.world_valid && !target.absolute_valid, "relative and absolute validity conflated");
  config.extrinsics_valid = false;
  target_position::Solver(config).transform(target, &pose);
  require(!target.world_valid, "unconfirmed extrinsics marked world valid");
}
}  // namespace

int main()
{
  try {
    exact_projection_test();
    noise_and_rejection_test();
    multiclass_sizes_test();
    transform_test();
    std::cout << "solver tests passed\n";
    return 0;
  } catch (const std::exception & exception) {
    std::cerr << "solver test failed: " << exception.what() << '\n';
    return 1;
  }
}
