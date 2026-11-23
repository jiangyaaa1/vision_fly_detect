#include "config.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace tools
{
namespace
{
template <typename T>
void read(const cv::FileStorage & file, const char * key, T & value)
{
  if (!file[key].empty()) file[key] >> value;
}

void read_flag(const cv::FileStorage & file, const char * key, bool & value)
{
  int number = value ? 1 : 0;
  read(file, key, number);
  if (number != 0 && number != 1) throw std::runtime_error(std::string(key) + " 必须是0或1");
  value = number != 0;
}

template <int N>
void read_array(const cv::FileStorage & file, const char * key, double * data)
{
  if (file[key].empty()) return;
  std::vector<double> values;
  file[key] >> values;
  if (values.size() != N) throw std::runtime_error(std::string(key) + " 参数数量错误");
  for (int i = 0; i < N; i++) {
    if (!std::isfinite(values[i])) throw std::runtime_error(std::string(key) + " 包含非有限值");
    data[i] = values[i];
  }
}

void require(bool condition, const std::string & message)
{
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

Config load_config(const std::string & path)
{
  cv::FileStorage file(path, cv::FileStorage::READ);
  require(file.isOpened(), "无法打开配置文件: " + path);
  Config config;
  read(file, "model_path", config.model_path);
  read(file, "model_type", config.model_type);
  read(file, "armor_color", config.armor_color);
  read(file, "image_width", config.image_width);
  read(file, "image_height", config.image_height);
  read(file, "calibration_width", config.calibration_width);
  read(file, "calibration_height", config.calibration_height);
  read(file, "camera_fps", config.camera_fps);
  read_flag(file, "camera_auto_exposure", config.camera_auto_exposure);
  read(file, "camera_exposure_us", config.camera_exposure_us);
  read(file, "camera_gain", config.camera_gain);
  read(file, "class_id", config.class_id);
  read(file, "confidence", config.confidence);
  read(file, "pnp_min_confidence", config.pnp_min_confidence);
  read(file, "iou", config.iou);
  read_flag(file, "infantry_diagnostics", config.infantry_diagnostics);
  read_flag(file, "infantry_zoom_enabled", config.infantry_zoom_enabled);
  read(file, "infantry_full_interval", config.infantry_full_interval);
  read(file, "infantry_crop_width", config.infantry_crop_width);
  read_flag(file, "display", config.display);
  read_flag(file, "localization_enabled", config.localization_enabled);
  read_flag(file, "debug_stream", config.debug_stream);
  read(file, "debug_port", config.debug_port);
  read(file, "debug_fps", config.debug_fps);
  read(file, "debug_layout", config.debug_layout);
  read(file, "debug_jpeg_quality", config.debug_jpeg_quality);
  read_flag(file, "debug_async", config.debug_async);
  read_flag(file, "calibration_valid", config.calibration_valid);
  read_flag(file, "extrinsics_valid", config.extrinsics_valid);
  read_array<9>(file, "camera_matrix", config.camera_matrix.val);
  read_array<5>(file, "distort_coeffs", config.distort_coeffs.val);
  read_array<9>(file, "R_camera2body", config.R_camera2body.val);
  read_array<3>(file, "t_camera2body", config.t_camera2body.val);
  read(file, "target_width", config.target_width);
  read(file, "target_height", config.target_height);
  read_array<9>(file, "armor_width_by_class", config.armor_width_by_class.val);
  read_array<9>(file, "armor_height_by_class", config.armor_height_by_class.val);
  read(file, "corner_mode", config.corner_mode);
  if (!file["keypoint_indices"].empty()) {
    std::vector<int> indices;
    file["keypoint_indices"] >> indices;
    require(indices.size() == 4, "keypoint_indices 必须包含4个索引");
    for (int i = 0; i < 4; i++) config.keypoint_indices[i] = indices[i];
  }
  read(file, "roi_expand", config.roi_expand);
  read(file, "min_quad_area", config.min_quad_area);
  read(file, "min_corner_edge", config.min_corner_edge);
  read(file, "min_fill_ratio", config.min_fill_ratio);
  read(file, "max_roll_deg", config.max_roll_deg);
  read(file, "max_reprojection_error", config.max_reprojection_error);
  read(file, "min_distance", config.min_distance);
  read(file, "max_distance", config.max_distance);
  read(file, "pose_mode", config.pose_mode);
  read_array<4>(file, "static_q_body2world", config.static_q_body2world.val);
  read(file, "pose_bind_address", config.pose_bind_address);
  read(file, "pose_port", config.pose_port);
  read(file, "max_pose_gap_ms", config.max_pose_gap_ms);
  read(file, "image_time_offset_ms", config.image_time_offset_ms);
  read_flag(file, "uart_enabled", config.uart_enabled);
  read(file, "uart_port", config.uart_port);
  read(file, "uart_baudrate", config.uart_baudrate);
  read(file, "uart_tx_hz", config.uart_tx_hz);
  read(file, "uart_stale_ms", config.uart_stale_ms);
  read(file, "uart_coordinate_frame", config.uart_coordinate_frame);
  read(file, "uart_rx_log", config.uart_rx_log);
  read(file, "output_path", config.output_path);
  validate_config(config);
  return config;
}

void validate_config(const Config & config)
{
  if (config.localization_enabled) {
    require(config.calibration_valid, "请先填写真实相机标定参数，再将 calibration_valid 设为1");
    require(config.extrinsics_valid, "请先确认相机到机体外参，再将 extrinsics_valid 设为1");
  }
  require(!config.model_path.empty(), "model_path 不可为空");
  require(
    config.model_type == "yolov8" || config.model_type == "szurpv_infantry_v8n",
    "model_type 只能为 yolov8 或 szurpv_infantry_v8n");
  require(config.armor_color >= -1 && config.armor_color <= 2, "armor_color 应为-1/0/1/2");
  require(!config.infantry_zoom_enabled || config.model_type == "szurpv_infantry_v8n",
          "infantry_zoom_enabled仅适用于深大三输出模型");
  require(config.infantry_full_interval >= 2 && config.infantry_full_interval <= 10,
          "infantry_full_interval应为2到10");
  require(config.infantry_crop_width >= 352 && config.infantry_crop_width <= 576 &&
          config.infantry_crop_width % 4 == 0, "infantry_crop_width应为352到576且为4的倍数");
  if (config.model_type == "szurpv_infantry_v8n") {
    require(
      config.image_width == 640 && config.image_height == 480,
      "此深大模型固定640x480，请保持相机与模型同尺寸");
    require(config.class_id >= -1 && config.class_id < 9, "深大类别应为-1（全部）或0到8");
    require(
      config.corner_mode == "keypoints" && config.keypoint_indices == cv::Vec4i(0, 3, 2, 1),
      "深大模型应使用keypoints及[0,3,2,1]角点顺序");
  }
  require(config.image_width > 0 && config.image_height > 0, "图像尺寸必须为正数");
  require(
    !config.localization_enabled || (config.image_width == config.calibration_width &&
                                     config.image_height == config.calibration_height),
    "图像尺寸必须与标定尺寸完全一致；裁剪、缩放后请重新标定或正确换算内参");
  require(config.camera_fps > 0 && config.camera_fps <= 120, "camera_fps 应在1到120之间");
  require(
    config.camera_exposure_us > 0 && config.camera_exposure_us <= 1000000,
    "camera_exposure_us 应为正微秒值且不超过1秒");
  require(
    config.camera_auto_exposure || config.camera_exposure_us <= 1000000 / config.camera_fps,
    "手动曝光不能长于配置帧周期，请降低camera_exposure_us");
  require(
    config.camera_gain >= 1024 && config.camera_gain <= 16384,
    "camera_gain应在1024到16384之间（1倍到16倍）");
  const auto & k = config.camera_matrix;
  require(cv::checkRange(cv::Mat(k)), "camera_matrix 包含非法数值");
  const bool all_armor_classes =
    config.model_type == "szurpv_infantry_v8n" && config.class_id == -1;
  int known_sizes = 0;
  for (int i = 0; i < 9; ++i) {
    const double width = config.armor_width_by_class[i], height = config.armor_height_by_class[i];
    require(
      std::isfinite(width) && std::isfinite(height) &&
        ((width == 0 && height == 0) || (width > 0 && height > 0)),
      "逐类尺寸必须宽高均为正数；宽高均为0表示该类只检测不定位");
    if (width > 0) ++known_sizes;
  }
  require(
    !config.localization_enabled || !all_armor_classes || known_sizes > 0,
    "全类别PnP需要填写armor_width_by_class/armor_height_by_class");
  require(
    !config.localization_enabled ||
      (k(0, 0) > 0 && k(1, 1) > 0 && std::abs(k(0, 1)) < 1e-10 && std::abs(k(1, 0)) < 1e-10 &&
       std::abs(k(2, 0)) < 1e-10 && std::abs(k(2, 1)) < 1e-10 && std::abs(k(2, 2) - 1) < 1e-10),
    "camera_matrix 应为 [fx,0,cx,0,fy,cy,0,0,1]，fx/fy须为正数");
  require(cv::checkRange(cv::Mat(config.distort_coeffs)), "distort_coeffs 包含非法数值");
  require(cv::checkRange(cv::Mat(config.R_camera2body)), "外参包含非法数值");
  require(cv::checkRange(cv::Mat(config.t_camera2body)), "平移外参包含非法数值");
  const auto identity = config.R_camera2body.t() * config.R_camera2body;
  require(
    cv::norm(cv::Mat(identity - cv::Matx33d::eye())) < 1e-5 &&
      std::abs(cv::determinant(cv::Mat(config.R_camera2body)) - 1) < 1e-5,
    "R_camera2body 必须是正交且行列式为+1的旋转矩阵");
  require(
    !config.localization_enabled || all_armor_classes ||
      (std::isfinite(config.target_width) && std::isfinite(config.target_height) &&
       config.target_width > 0 && config.target_height > 0),
    "请填写目标实际宽高 target_width/target_height，单位为米");
  require(
    config.class_id >= 0 || all_armor_classes,
    "class_id=-1目前仅用于深大模型；普通模型请指定已知尺寸的类别");
  require(config.uart_port >= 0 && config.uart_port <= 3, "uart_port必须为0到3");
  require(config.uart_coordinate_frame == "body" || config.uart_coordinate_frame == "camera" ||
          config.uart_coordinate_frame == "world_relative", "uart_coordinate_frame只支持body/camera/world_relative");
  require(config.uart_tx_hz >= 1 && config.uart_tx_hz <= 50, "uart_tx_hz应为1到50");
  require(config.uart_stale_ms >= 20 && config.uart_stale_ms <= 5000, "uart_stale_ms应为20到5000毫秒");
  require(config.uart_baudrate == 9600 || config.uart_baudrate == 19200 ||
          config.uart_baudrate == 38400 || config.uart_baudrate == 57600 ||
          config.uart_baudrate == 115200 || config.uart_baudrate == 230400, "UART波特率不支持");
  require(config.uart_baudrate >= config.uart_tx_hz * 160 * 10,
          "UART波特率不足，请降低uart_tx_hz或提高uart_baudrate");
  require(config.uart_rx_log.empty() || config.uart_rx_log != config.output_path,
          "UART接收日志不能与坐标输出文件相同");
  require(config.confidence > 0 && config.confidence <= 1, "confidence 应在(0,1]内");
  require(std::isfinite(config.pnp_min_confidence) && config.pnp_min_confidence >= 0 &&
          config.pnp_min_confidence <= 1, "pnp_min_confidence 应在[0,1]内");
  require(config.iou > 0 && config.iou <= 1, "iou 应在(0,1]内");
  require(config.debug_port > 0 && config.debug_port <= 65535, "debug_port 端口不合法");
  require(config.debug_fps >= 0 && config.debug_fps <= 30, "debug_fps 应为0（不限速）或1到30");
  require(
    config.debug_layout == "sheet" || config.debug_layout == "overlay",
    "debug_layout 只能为sheet或overlay");
  require(
    config.debug_jpeg_quality >= 20 && config.debug_jpeg_quality <= 95,
    "debug_jpeg_quality 应在20到95之间");
  require(
    config.corner_mode == "contour" || config.corner_mode == "keypoints",
    "corner_mode 只能为 contour 或 keypoints");
  for (int i = 0; i < 4; i++) {
    require(config.keypoint_indices[i] >= 0, "关键点索引不能为负数");
    for (int j = 0; j < i; j++) {
      require(config.keypoint_indices[i] != config.keypoint_indices[j], "关键点索引不能重复");
    }
  }
  require(config.roi_expand >= 0 && config.roi_expand <= 1, "roi_expand 应在[0,1]内");
  require(
    std::isfinite(config.min_quad_area) && config.min_quad_area > 0,
    "min_quad_area 必须为有限正数");
  require(config.min_fill_ratio > 0 && config.min_fill_ratio <= 1, "min_fill_ratio 应在(0,1]内");
  require(std::isfinite(config.min_corner_edge) && config.min_corner_edge >= 2 &&
          config.min_corner_edge <= 100, "min_corner_edge应为2到100原图像素");
  require(config.max_roll_deg > 0 && config.max_roll_deg < 45, "max_roll_deg 必须在(0,45)内");
  require(
    std::isfinite(config.max_reprojection_error) && config.max_reprojection_error > 0,
    "重投影阈值必须为有限正数");
  require(
    config.min_distance > 0 && config.max_distance > config.min_distance &&
      std::isfinite(config.max_distance),
    "距离区间不合法");
  require(
    config.pose_mode == "none" || config.pose_mode == "static" || config.pose_mode == "udp",
    "pose_mode 只能为 none/static/udp");
  if (config.pose_mode == "static") quaternion_to_rotation(config.static_q_body2world);
  require(config.pose_port > 0 && config.pose_port < 65536, "UDP端口不合法");
  require(
    config.max_pose_gap_ms > 0 && config.max_pose_gap_ms <= 1000, "max_pose_gap_ms 应在(0,1000]内");
  require(
    std::isfinite(config.image_time_offset_ms) && std::abs(config.image_time_offset_ms) <= 1000,
    "image_time_offset_ms 应在[-1000,1000]内");
}

cv::Matx33d quaternion_to_rotation(const cv::Vec4d & quaternion)
{
  require(cv::checkRange(cv::Mat(quaternion)), "四元数包含非法数值");
  const double norm = cv::norm(quaternion);
  require(norm > 1e-9 && std::isfinite(norm), "四元数长度为零或非法");
  const auto q = quaternion / norm;
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  return {1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
          2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
          2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)};
}

int64_t monotonic_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}
}  // namespace tools
