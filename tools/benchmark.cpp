#include "benchmark.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

#include "debug_sender.hpp"
#include "debug_view.hpp"
#include "maix_basic.hpp"
#include "maix_image_cv.hpp"
#include "tasks/target_position/detector.hpp"
#include "tasks/target_position/solver.hpp"
#include "tasks/target_position/yolo.hpp"

namespace tools
{
int benchmark_fixture(Config config, const std::string & image_path, int frames)
{
  if (frames < 10 || frames > 2000) throw std::runtime_error("基准帧数应为10到2000");
  // 仅此测试函数使用模拟标定/姿态，不写回配置、不接相机、不发布定位结果。
  config.calibration_valid = true;
  config.extrinsics_valid = true;
  config.camera_matrix = {800, 0, config.image_width / 2.0, 0, 800, config.image_height / 2.0, 0,
                          0,   1};
  config.distort_coeffs = {0, 0, 0, 0, 0};
  config.target_width = 0.135;
  config.target_height = 0.056;
  target_position::YOLO model(config);
  target_position::Detector detector(config);
  target_position::Solver solver(config);
  std::unique_ptr<maix::image::Image> image(maix::image::load(image_path, model.input_format()));
  if (!image || image->width() != config.image_width || image->height() != config.image_height)
    throw std::runtime_error("基准图片尺寸错误");
  cv::Mat bgr;
  if (config.corner_mode != "keypoints") throw std::runtime_error("此基准仅支持关键点模型");
  maix::err::check_raise(maix::image::image2cv(*image, bgr, false, false));
  const bool rgb = image->format() == maix::image::FMT_RGB888;
  std::unique_ptr<maix::http::JpegStreamer> stream;
  std::unique_ptr<DebugSender> sender;
  if (config.debug_stream && config.debug_async) {
    stream.reset(new maix::http::JpegStreamer("0.0.0.0", config.debug_port));
    maix::err::check_raise(stream->start());
    sender.reset(new DebugSender(*stream, config.debug_jpeg_quality));
  }
  int64_t last_debug_ns = 0;
  std::vector<double> times;
  double inference_total = 0, pnp_total = 0, debug_total = 0;
  int pnp_valid = 0, world_valid = 0;
  target_position::Pose fixture_pose;
  for (int i = -3; i < frames && !maix::app::need_exit(); ++i) {
    const auto begin = monotonic_ns();
    const auto detections = model.detect(*image);
    const auto inferred = monotonic_ns();
    bool solved = false, transformed = false;
    target_position::Target selected;
    for (const auto & detection : detections) {
      target_position::Target target;
      std::string reason;
      if (!detector.detect(bgr, detection, target, reason)) continue;
      if (!solver.solve(target, reason)) continue;
      solver.transform(target, &fixture_pose);
      solved = true;
      transformed = target.world_valid;
      selected = target;
      break;
    }
    const auto solved_at = monotonic_ns();
    if (
      sender &&
      (config.debug_fps == 0 || solved_at - last_debug_ns >= 1000000000LL / config.debug_fps)) {
      DebugView view(bgr, false, rgb);
      for (const auto & detection : detections) view.detection(detection);
      if (solved) view.corners(selected, true);
      // 水印明确说明是固定图片/模拟标定，绝不伪装成真实相机定位画面。
      view.status("BENCHMARK - SYNTHETIC CALIBRATION", nullptr);
      sender->submit(view.overlay(), !rgb);
      last_debug_ns = monotonic_ns();
    }
    const auto end = monotonic_ns();
    if (i >= 0) {
      times.push_back((end - begin) / 1e6);
      inference_total += (inferred - begin) / 1e6;
      pnp_total += (solved_at - inferred) / 1e6;
      debug_total += (end - solved_at) / 1e6;
      pnp_valid += solved;
      world_valid += transformed;
    }
  }
  sender.reset();
  if (stream) maix::err::check_raise(stream->stop());
  if (times.empty()) throw std::runtime_error("基准没有完成帧");
  double total = 0;
  for (double ms : times) total += ms;
  std::sort(times.begin(), times.end());
  std::cout << "BENCHMARK fixture_only=1 frames=" << times.size() << " pnp_frames=" << pnp_valid
            << " world_transform_frames=" << world_valid << " fps=" << times.size() * 1000 / total
            << " mean_ms=" << total / times.size()
            << " p95_ms=" << times[(times.size() - 1) * 95 / 100]
            << " inference_ms=" << inference_total / times.size()
            << " corner_pnp_transform_ms=" << pnp_total / times.size()
            << " debug_submit_ms=" << debug_total / times.size()
            << " asynchronous_display=" << bool(stream) << " camera_capture_included=0"
            << " calibration=SYNTHETIC attitude=SYNTHETIC output_not_for_navigation=1\n";
  return pnp_valid == static_cast<int>(times.size()) ? 0 : 2;
}
}  // namespace tools
