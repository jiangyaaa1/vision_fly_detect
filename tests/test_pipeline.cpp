#include <iostream>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

#include "tasks/target_position/detector.hpp"
#include "tasks/target_position/solver.hpp"
#include "tools/debug_view.hpp"
#include "tools/debug_frame.hpp"

int main()
{
  try {
    tools::Config config;
    config.calibration_valid = config.extrinsics_valid = true;
    config.camera_matrix = {800, 0, 320, 0, 800, 240, 0, 0, 1};
    config.target_width = 0.4;
    config.target_height = 0.25;
    config.t_camera2body = {0.08, 0, 0};
    // 真实已知几何：中心在相机正前方2m，对应160x100像素矩形。
    cv::Mat image(480, 640, CV_8UC3, cv::Scalar(15, 15, 15));
    cv::rectangle(image, {240, 190}, {400, 290}, {240, 240, 240}, cv::FILLED);
    target_position::Detection detection;
    detection.box = {238, 188, 165, 105};
    detection.class_id = 0;
    detection.score = 0.9f;
    target_position::Target target;
    std::string reason;
    const cv::Mat original = image.clone();
    target_position::Target debug_target;
    target_position::DetectorDebug features;
    if (!target_position::Detector(config).detect(
          image, detection, debug_target, reason, &features)) {
      throw std::runtime_error("debug corner failure: " + reason);
    }
    if (!target_position::Detector(config).detect(image, detection, target, reason)) {
      throw std::runtime_error("pipeline corner failure: " + reason);
    }
    target_position::Solver solver(config);
    tools::DebugView view(image);
    view.detection(detection);
    view.features(features);
    view.corners(debug_target, false);
    view.status("detection_only", nullptr);
    const auto sheet = view.sheet();
    if (
      sheet.size() != cv::Size(image.cols * 3, image.rows) || sheet.type() != CV_8UC3 ||
      cv::norm(image, original, cv::NORM_INF) != 0 || features.edges.empty() ||
      cv::countNonZero(features.edges) == 0) {
      throw std::runtime_error("debug view invalid or modified calculation input");
    }
    for (int i = 0; i < 4; i++) {
      if (cv::norm(debug_target.points[i] - target.points[i]) > 1e-6) {
        throw std::runtime_error("enabling debug changed corner coordinates");
      }
    }
    // 零转换RGB调试路径必须与旧BGR路径呈现相同颜色，且不修改原始计算图像。
    cv::Mat colored(image.size(), CV_8UC3, cv::Scalar(10, 40, 80)), rgb;
    cv::cvtColor(colored, rgb, cv::COLOR_BGR2RGB);
    tools::DebugView bgr_view(colored, false), rgb_view(rgb, false, true);
    auto display_target = debug_target;
    display_target.xyz_in_camera = {0.1, 0.2, 2};
    display_target.xyz_in_body = {2, 0.1, 0.2};
    display_target.relative_xyz_in_world = {2, 0.1, 0.2};
    display_target.world_valid = true;
    for (auto * candidate : {&bgr_view, &rgb_view}) {
      candidate->detection(detection);
      candidate->features(features);
      candidate->corners(debug_target, false);
      candidate->status("ok_static_pose", &display_target, 18.5, 40);
    }
    cv::Mat expected_rgb;
    cv::cvtColor(bgr_view.overlay(), expected_rgb, cv::COLOR_BGR2RGB);
    if (cv::norm(expected_rgb, rgb_view.overlay(), cv::NORM_INF) != 0) {
      throw std::runtime_error("RGB fast display changed overlay colors");
    }
    // 后台快照必须逐像素等价，不能把下一帧图像和上一帧坐标混在一起。
    for (bool panels : {false, true}) {
      for (bool is_rgb : {false, true}) {
        const auto & source = is_rgb ? rgb : colored;
        tools::DebugFrame job;
        job.image = source.clone(); job.rgb = is_rgb; job.feature_panels = panels;
        job.detections = {detection};
        job.items.push_back({features, debug_target, true});
        job.selected = display_target; job.selected_valid = true;
        job.status = "ok_static_pose"; job.process_fps = 18.5; job.inference_ms = 40;
        tools::DebugView reference(source, panels, is_rgb);
        reference.detection(detection); reference.features(features);
        reference.corners(debug_target, false); reference.corners(display_target, true);
        reference.status(job.status, &display_target, 18.5, 40);
        const auto expected = panels ? reference.sheet() : reference.overlay();
        if (cv::norm(expected, job.render(), cv::NORM_INF) != 0)
          throw std::runtime_error("background snapshot overlay differs");
      }
    }
    // 零拷贝快照必须持有外部像素，直到后台绘制/编码结束。
    std::weak_ptr<std::vector<unsigned char>> lifetime;
    {
      auto pixels = std::make_shared<std::vector<unsigned char>>(640 * 480 * 3, 70);
      lifetime = pixels;
      tools::DebugFrame owned;
      owned.image = cv::Mat(480, 640, CV_8UC3, pixels->data());
      owned.image_owner = pixels;
      pixels.reset();
      if (lifetime.expired()) throw std::runtime_error("snapshot lost external pixel owner");
      auto rendered = owned.render();
      if (rendered.at<cv::Vec3b>(200, 200)[0] != 70)
        throw std::runtime_error("snapshot external pixels corrupted");
    }
    if (!lifetime.expired()) throw std::runtime_error("snapshot leaked external pixel owner");
    if (cv::norm(image, original, cv::NORM_INF) != 0)
      throw std::runtime_error("background drawing changed input");
    if (!solver.solve(target, reason)) throw std::runtime_error("pipeline PnP failure: " + reason);
    auto filtered_config = config;
    filtered_config.pnp_min_confidence = 0.6f;
    auto weak_target = target;
    weak_target.detection.score = 0.4f;
    if (target_position::Solver(filtered_config).solve(weak_target, reason) ||
        reason != "pnp_confidence_too_low" || weak_target.world_valid)
      throw std::runtime_error("weak detections must not run PnP");
    weak_target = target; weak_target.detection.score = 0.9f;
    if (!target_position::Solver(filtered_config).solve(weak_target, reason))
      throw std::runtime_error("strong detections must retain PnP");
    if (cv::norm(target.xyz_in_camera - cv::Vec3d(0, 0, 2)) > 0.025) {
      throw std::runtime_error("rasterized physical target recovered incorrect depth");
    }
    target_position::Pose pose;
    pose.q_body2world = {std::sqrt(0.5), 0, 0, std::sqrt(0.5)};
    solver.transform(target, &pose);
    if (
      !target.world_valid ||
      cv::norm(target.relative_xyz_in_world - cv::Vec3d(0, 2.08, 0)) > 0.025) {
      throw std::runtime_error("pipeline NED displacement incorrect");
    }
    std::cout << "image -> corners -> planar PnP -> FRD -> NED pipeline passed\n";
  } catch (const std::exception & exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
