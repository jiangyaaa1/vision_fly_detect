#include <algorithm>
#include <csignal>
#include <iostream>
#include <memory>
#include <opencv2/imgproc.hpp>

#include "io/pose_receiver.hpp"
#include "io/serial_link.hpp"
#include "io/uart_port.hpp"
#include "maix_pinmap.hpp"
#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include "maix_display.hpp"
#include "maix_image_cv.hpp"
#include "maix_jpg_stream.hpp"
#include "tasks/target_position/detector.hpp"
#include "tasks/target_position/solver.hpp"
#include "tasks/target_position/yolo.hpp"
#include "tools/benchmark.hpp"
#include "tools/config.hpp"
#include "tools/live_exposure.hpp"
#include "tools/debug_sender.hpp"
#include "tools/debug_view.hpp"
#include "tools/result_writer.hpp"

using namespace maix;

namespace
{
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }

const char * debug_html = R"html(<!DOCTYPE html>
<html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>YOLO / OpenCV 调试</title>
<style>body{background:#111827;color:#e5e7eb;font:16px system-ui;padding:16px}
img{width:100%;height:auto}p{line-height:1.6}</style>
<h2>YOLO / OpenCV 实时调试</h2>
<p>左：检测框、轮廓、角点与解算状态；中：Canny边缘；右：Otsu二值化。</p>
<p>青色框：YOLO；蓝色框：ROI；紫色线：候选轮廓；橙色四边形：已提取角点；
绿色四边形：本帧选中的PnP有效目标；红色数字：角点0、1、2、3。</p>
<img src="/stream" alt="YOLO OpenCV 三联调试画面">
<p>边缘与二值图只显示已检测目标的ROI。没有检测框时为空；keypoints模式不运行轮廓提取，中/右两栏为空。</p>
</html>)html";
}  // namespace

int _main(int argc, char * argv[])
{
  const auto config = tools::load_config(argc > 1 ? argv[1] : "configs/maixcam.yaml");
  if (argc == 5 && std::string(argv[2]) == "--benchmark-fixture")
    return tools::benchmark_fixture(config, argv[3], std::stoi(argv[4]));
  const bool test_image = argc == 4 && std::string(argv[2]) == "--test-image";
  if (argc > 2 && !test_image)
    throw std::runtime_error("用法：vision_fly_detect 配置 [--test-image 图片]");
  target_position::YOLO yolo(config);
  target_position::Detector detector(config);
  target_position::Solver solver(config);
  io::PoseReceiver pose_receiver(config);
  tools::ResultWriter writer(config.output_path);
  std::unique_ptr<io::SerialLink> serial;
  if (config.uart_enabled && !test_image) {
    const auto port = io::uart_port(config.uart_port);
    if (config.uart_port == 3 && io::uart3_sdio_conflict())
      throw std::runtime_error("UART3的P19/P20与Wi-Fi SDIO共用，检测到SDIO设备；请先按硬件说明释放引脚，或选择其它串口");
    err::check_raise(peripheral::pinmap::set_pin_function(port.tx_pin, port.tx_function));
    err::check_raise(peripheral::pinmap::set_pin_function(port.rx_pin, port.rx_function));
    serial.reset(new io::SerialLink(config));
    std::cerr << port.name << "_READY " << port.device << " TX=" << port.tx_pin
              << " RX=" << port.rx_pin << " baud=" << config.uart_baudrate
              << " frame=" << config.uart_coordinate_frame << " unit=m\n";
  }
  // 相机先于显示析构，避免显示停止时采集仍持有硬件缓冲。
  std::unique_ptr<display::Display> screen;
  std::unique_ptr<camera::Camera> camera;
  if (!test_image) {
    camera.reset(new camera::Camera(
      config.image_width, config.image_height, yolo.input_format(), nullptr, config.camera_fps, 1));
    camera->exp_mode(config.camera_auto_exposure ? camera::AeMode::Auto : camera::AeMode::Manual);
    if (!config.camera_auto_exposure) {
      camera->exposure(config.camera_exposure_us);
      camera->gain(config.camera_gain);
    }
    std::cerr << "CAMERA_EXPOSURE mode=" << (config.camera_auto_exposure ? "auto" : "manual")
              << " requested_us=" << config.camera_exposure_us << " gain=" << config.camera_gain
              << '\n';
  }
  if (config.display && !test_image) screen.reset(new display::Display());
  std::unique_ptr<http::JpegStreamer> stream;
  std::unique_ptr<tools::DebugSender> debug_sender;
  if (config.debug_stream && !test_image) {
    stream.reset(new http::JpegStreamer("0.0.0.0", config.debug_port));
    err::check_raise(stream->set_html(debug_html));
    err::check_raise(stream->start());
    if (config.debug_async)
      debug_sender.reset(new tools::DebugSender(*stream, config.debug_jpeg_quality));
    std::cerr << "YOLO/OpenCV 调试网页端口: " << config.debug_port << '\n';
  }
  tools::LiveExposure live_exposure(argc > 1 ? argv[1] : "configs/maixcam.yaml",
    config.camera_fps, config.camera_auto_exposure, config.camera_exposure_us, config.camera_gain);
  int64_t last_stream_ns = 0;
  int64_t performance_start = tools::monotonic_ns();
  int performance_frames = 0;
  int exposure_warmup_frames = 0;
  double last_process_fps = 0;
  double performance_read_ms = 0, performance_infer_ms = 0, performance_debug_ms = 0;
  double performance_geometry_ms = 0, performance_output_ms = 0;
  int performance_pnp_frames = 0;
  double performance_forward_ms = 0, performance_decode_ms = 0;
  int performance_boxes = 0, performance_corners = 0;
  int performance_candidates = 0, performance_color_rejects = 0;
  int performance_point_rejects = 0, performance_area_rejects = 0;
  float performance_best_score = 0;
  std::cerr << "定位启动；机体FRD，世界NED，单位米，图像时间戳为曝光估计值。\n";

  while (!stop_requested && !app::need_exit()) {
    const int64_t frame_start = tools::monotonic_ns();
    if (camera) live_exposure.poll(*camera, frame_start);
    pose_receiver.poll();
    std::unique_ptr<image::Image> image(
      test_image ? image::load(argv[3], yolo.input_format()) : camera->read());
    const int64_t timestamp_ns =
      tools::monotonic_ns() + static_cast<int64_t>(config.image_time_offset_ms * 1e6);
    if (!image) {
      if (test_image) throw std::runtime_error("无法读取测试图片");
      if (serial) serial->submit(timestamp_ns, nullptr);
      writer.write(timestamp_ns, tools::monotonic_ns(), nullptr, "camera_read_failed");
      continue;
    }
    if (camera && ++exposure_warmup_frames == 12) {
      std::cerr << "CAMERA_EXPOSURE_ACTUAL exposure_us=" << camera->exposure()
                << " gain=" << camera->gain() << " mode=" << static_cast<int>(camera->exp_mode())
                << '\n';
    }
    // 传感器实际输出分辨率可能被硬件对齐，禁止使用不匹配的标定参数。
    if (image->width() != config.image_width || image->height() != config.image_height) {
      throw std::runtime_error("相机实际输出尺寸与YAML/标定尺寸不同，请调整分辨率并重新标定");
    }
    pose_receiver.poll();
    const int64_t inference_start = tools::monotonic_ns();
    const auto detections = yolo.detect(*image);
    const auto inference_roi = yolo.inference_roi();
    const double inference_ms = (tools::monotonic_ns() - inference_start) / 1e6;
    cv::Mat bgr;
    // 关键点校验只读取尺寸，不读取像素；RGB叠加图可直接送硬件JPEG，避免两次通道转换。
    // 轮廓模式仍必须使用真实BGR，不能把RGB送进灰度/颜色特征提取。
    const bool rgb_geometry = config.corner_mode == "keypoints" &&
                              config.debug_layout == "overlay" &&
                              image->format() == image::FMT_RGB888;
    err::check_raise(image::image2cv(*image, bgr, !rgb_geometry, !rgb_geometry));
    const bool stream_due =
      stream && (config.debug_fps == 0 ||
                 tools::monotonic_ns() - last_stream_ns >= 1000000000LL / config.debug_fps);
    std::unique_ptr<tools::DebugView> view;
    std::unique_ptr<tools::DebugFrame> debug_frame;
    if (stream_due && debug_sender && !screen) {
      debug_frame.reset(new tools::DebugFrame());
      // RGB路径稍后连同Camera::read返回的独立堆图像一起转交后台；BGR路径由Mat持有副本。
      debug_frame->image = bgr;
      debug_frame->rgb = rgb_geometry;
      debug_frame->feature_panels = config.debug_layout == "sheet";
      debug_frame->detections = detections;
      if (config.infantry_zoom_enabled) debug_frame->inference_roi = inference_roi;
    } else if (screen || stream_due) {
      view.reset(new tools::DebugView(bgr, config.debug_layout == "sheet", rgb_geometry));
      if (config.infantry_zoom_enabled) view->search_roi(inference_roi);
      for (const auto & detection : detections) view->detection(detection);
    }
    const auto geometry_start = tools::monotonic_ns();
    pose_receiver.poll();
    target_position::Pose pose;
    std::string pose_reason;
    const bool pose_valid =
      config.localization_enabled && pose_receiver.sample(timestamp_ns, pose, pose_reason);
    target_position::Target result;
    bool found = false;
    std::string status = "no_target";
    int corners_ready = 0;
    int examined = 0;
    // 原始bgr只用于计算，调试标记始终画在独立副本上。
    for (const auto & detection : detections) {
      if (found && ((!view && !debug_frame) || examined >= 8)) break;
      ++examined;
      target_position::Target target;
      target_position::DetectorDebug features;
      std::string candidate_reason;
      const bool corner_ok =
        detector.detect(
          bgr, detection, target, candidate_reason, (view || debug_frame) ? &features : nullptr);
      if (view) view->features(features);
      if (debug_frame) debug_frame->items.push_back({features, target, corner_ok});
      if (!corner_ok) {
        if (!found) status = candidate_reason;
        continue;
      }
      ++corners_ready;
      if (view) view->corners(target, false);
      if (!config.localization_enabled || found) continue;
      if (!solver.solve(target, candidate_reason)) {
        if (!found) status = candidate_reason;
        continue;
      }
      solver.transform(target, pose_valid ? &pose : nullptr);
      result = target;
      found = true;
      status = pose_valid ? (config.pose_mode == "static" ? "ok_static_pose" : "ok")
                          : "camera_body_only:" + pose_reason;
    }
    if (!config.localization_enabled) {
      status = "detection_only boxes=" + std::to_string(detections.size()) +
               " corners=" + std::to_string(corners_ready);
    }
    const auto geometry_end = tools::monotonic_ns();
    if (serial) serial->submit(timestamp_ns, found ? &result : nullptr);
    writer.write(timestamp_ns, tools::monotonic_ns(), found ? &result : nullptr, status);
    const auto output_end = tools::monotonic_ns();
    if (test_image) {
      std::cerr << "MODEL_TEST boxes=" << detections.size() << " corners=" << corners_ready
                << " inference_ms=" << inference_ms
                << " diagnostics=" << config.infantry_diagnostics;
      if (config.infantry_diagnostics)
        std::cerr << " best_raw_score=" << yolo.decode_stats().best_score;
      std::cerr << '\n';
      for (const auto & detection : detections) {
        std::cerr << "class=" << detection.class_id << " color=" << detection.color_id
                  << " score=" << detection.score;
        for (const auto & point : detection.keypoints)
          std::cerr << " (" << point.x << ',' << point.y << ')';
        std::cerr << '\n';
      }
      break;
    }
    if (debug_frame) {
      if (rgb_geometry)
        debug_frame->image_owner = std::shared_ptr<image::Image>(std::move(image));
      debug_frame->selected = result;
      debug_frame->selected_valid = found;
      debug_frame->status = status;
      debug_frame->process_fps = last_process_fps;
      debug_frame->inference_ms = inference_ms;
      debug_sender->submit(std::move(debug_frame));
      last_stream_ns = tools::monotonic_ns();
    }
    if (view) {
      if (found) view->corners(result, true);
      view->status(status, found ? &result : nullptr, last_process_fps, inference_ms);
      if (screen) {
        cv::Mat overlay = view->overlay();
        std::unique_ptr<image::Image> preview(image::cv2image(overlay, !rgb_geometry, false));
        if (preview) err::check_raise(screen->show(*preview));
      }
      if (stream_due) {
        // overlay引用由view持有的Mat，编码完成前始终有效，省去大图复制。
        cv::Mat sheet = config.debug_layout == "sheet" ? view->sheet() : view->overlay();
        if (debug_sender) {
          debug_sender->submit(sheet, !rgb_geometry);
        } else {
          std::unique_ptr<image::Image> debug_image(image::cv2image(sheet, !rgb_geometry, false));
          err::check_null_raise(debug_image.get());
          std::unique_ptr<image::Image> jpeg(debug_image->to_jpeg(config.debug_jpeg_quality));
          err::check_null_raise(jpeg.get());
          err::check_raise(stream->write(jpeg.get()));
        }
        last_stream_ns = tools::monotonic_ns();
      }
    }
    ++performance_frames;
    performance_boxes += static_cast<int>(detections.size());
    performance_corners += corners_ready;
    const auto & decode_stats = yolo.decode_stats();
    performance_candidates += decode_stats.above_threshold;
    performance_color_rejects += decode_stats.rejected_color;
    performance_point_rejects += decode_stats.rejected_keypoints;
    performance_area_rejects += decode_stats.rejected_area;
    performance_best_score = std::max(performance_best_score, decode_stats.best_score);
    performance_pnp_frames += found;
    performance_forward_ms += yolo.forward_ms();
    performance_decode_ms += yolo.decode_ms();
    performance_geometry_ms += (geometry_end - geometry_start) / 1e6;
    performance_output_ms += (output_end - geometry_end) / 1e6;
    performance_read_ms += (inference_start - frame_start) / 1e6;
    performance_infer_ms += inference_ms;
    performance_debug_ms += (tools::monotonic_ns() - inference_start) / 1e6 - inference_ms;
    if (performance_frames >= 30) {
      last_process_fps = performance_frames * 1e9 / (tools::monotonic_ns() - performance_start);
      std::cerr << "PERF fps="
                << performance_frames * 1e9 / (tools::monotonic_ns() - performance_start)
                << " read_ms=" << performance_read_ms / performance_frames
                << " infer_ms=" << performance_infer_ms / performance_frames
                << " process_display_ms=" << performance_debug_ms / performance_frames
                << " nn_forward_ms=" << performance_forward_ms / performance_frames
                << " decode_ms=" << performance_decode_ms / performance_frames
                << " geometry_ms=" << performance_geometry_ms / performance_frames
                << " output_ms=" << performance_output_ms / performance_frames
                << " pnp_valid_frames=" << performance_pnp_frames
                << " boxes=" << performance_boxes << " corners=" << performance_corners
                << " diagnostics=" << config.infantry_diagnostics;
      if (config.infantry_diagnostics)
        std::cerr << " best_raw_score=" << performance_best_score
                << " candidates=" << performance_candidates
                << " color_rejects=" << performance_color_rejects
                << " point_rejects=" << performance_point_rejects
                << " area_rejects=" << performance_area_rejects;
      std::cerr << " inference_roi=" << inference_roi.x << ',' << inference_roi.y << ','
                << inference_roi.width << ',' << inference_roi.height
                << " last_status=" << status;
      if (debug_sender)
        std::cerr << " async_render_ms=" << debug_sender->render_ms()
                  << " async_encode_ms=" << debug_sender->encode_ms()
                  << " preview_replaced=" << debug_sender->replaced_frames();
      std::cerr << '\n';
      performance_start = tools::monotonic_ns();
      performance_frames = 0;
      performance_read_ms = performance_infer_ms = performance_debug_ms = 0;
      performance_geometry_ms = performance_output_ms = 0;
      performance_pnp_frames = 0;
      performance_forward_ms = performance_decode_ms = 0;
      performance_boxes = performance_corners = performance_candidates = 0;
      performance_color_rejects = performance_point_rejects = performance_area_rejects = 0;
      performance_best_score = 0;
    }
  }
  serial.reset();
  debug_sender.reset();
  if (stream) err::check_raise(stream->stop());
  camera.reset();
  screen.reset();
  return 0;
}

int main(int argc, char * argv[])
{
  sys::register_default_signal_handle();
  std::signal(SIGTERM, request_stop);
  std::signal(SIGPIPE, SIG_IGN);
  CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
