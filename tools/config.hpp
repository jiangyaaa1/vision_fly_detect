#ifndef TOOLS__CONFIG_HPP
#define TOOLS__CONFIG_HPP

#include <cstdint>
#include <opencv2/core.hpp>
#include <string>

namespace tools
{
struct Config
{
  std::string model_path = "/root/models/target.mud";
  std::string model_type = "yolov8";
  int armor_color = -1;  // 深大模型：-1全部，0蓝，1红，2白；紫色始终过滤。
  int image_width = 640;
  int image_height = 480;
  int calibration_width = 640;
  int calibration_height = 480;
  int camera_fps = 30;
  bool camera_auto_exposure = true;
  int camera_exposure_us = 2000;
  int camera_gain = 1024;  // MaixCAM gain单位：1024为1倍。
  int class_id = 0;
  float confidence = 0.6f;
  float pnp_min_confidence = 0;  // 0沿用检测门槛；可高于confidence，仅拒绝低分目标定位。
  float iou = 0.45f;
  bool infantry_diagnostics = false;  // 逐候选概率统计仅用于排查，关闭可减少CPU开销。
  bool infantry_zoom_enabled = false;  // 单次推理的全图/切片交替；仅深大模型使用。
  int infantry_full_interval = 5;      // 每N帧进行一次全图巡检。
  int infantry_crop_width = 384;       // 4:3搜索窗口，四个重叠窗口覆盖640x480。
  bool display = false;
  bool localization_enabled = true;
  bool debug_stream = false;
  int debug_port = 8000;
  int debug_fps = 10;  // 0：不限制预览帧率，每个处理帧都提交。
  std::string debug_layout = "sheet";
  int debug_jpeg_quality = 70;
  bool debug_async = false;
  bool calibration_valid = false;
  bool extrinsics_valid = false;
  cv::Matx33d camera_matrix = cv::Matx33d::eye();
  cv::Vec<double, 5> distort_coeffs{0, 0, 0, 0, 0};
  cv::Matx33d R_camera2body{0, 0, 1, 1, 0, 0, 0, 1, 0};
  cv::Vec3d t_camera2body{0, 0, 0};
  double target_width = 0;
  double target_height = 0;
  cv::Vec<double, 9> armor_width_by_class = cv::Vec<double, 9>::all(0);
  cv::Vec<double, 9> armor_height_by_class = cv::Vec<double, 9>::all(0);
  std::string corner_mode = "contour";
  cv::Vec4i keypoint_indices{0, 1, 2, 3};
  double roi_expand = 0.1;
  double min_quad_area = 100;
  double min_corner_edge = 4;  // 原图像素；不随切片放大而降低质量门槛。
  double min_fill_ratio = 0.3;
  double max_roll_deg = 40;
  double max_reprojection_error = 2;
  double min_distance = 0.05;
  double max_distance = 30;
  std::string pose_mode = "none";
  cv::Vec4d static_q_body2world{1, 0, 0, 0};
  std::string pose_bind_address = "127.0.0.1";
  int pose_port = 5005;
  double max_pose_gap_ms = 100;
  double image_time_offset_ms = 0;
  bool uart_enabled = false;
  int uart_port = 0;  // MaixCAM UART编号，0到3；UART3与Wi-Fi的SDIO引脚共用。
  int uart_baudrate = 115200;
  int uart_tx_hz = 20;
  int uart_stale_ms = 300;
  std::string uart_coordinate_frame = "body";
  std::string uart_rx_log = "uart0_received.log";
  std::string output_path = "target_positions.jsonl";  // 空串时stdout可能混有SDK日志。
};

Config load_config(const std::string & path);
void validate_config(const Config & config);
cv::Matx33d quaternion_to_rotation(const cv::Vec4d & quaternion);
int64_t monotonic_ns();
}  // namespace tools

#endif  // TOOLS__CONFIG_HPP
