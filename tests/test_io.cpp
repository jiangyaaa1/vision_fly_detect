#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "io/pose_receiver.hpp"
#include "tools/result_writer.hpp"

namespace
{
void require(bool value, const std::string & message)
{
  if (!value) throw std::runtime_error(message);
}

void pose_test()
{
  target_position::Pose pose;
  require(io::PoseReceiver::parse("POSE 1000000 1 0 0 0", pose), "basic packet rejected");
  require(!pose.position_valid, "missing position marked valid");
  require(
    io::PoseReceiver::parse("POSE 2000000 0.707106781 0 0 0.707106781 10 20 30", pose),
    "position packet rejected");
  require(pose.position_valid, "position omitted");
  for (const std::string packet :
       {"POSE 0 1 0 0 0", "POSE 100 0 0 0 0", "POSE 100 5 0 0 0", "POSE 100 nan 0 0 0",
        "POSE 100 1 0 0 0 1 2", "POSE 100 1 0 0 0 1 2 3 garbage", "WRONG 100 1 0 0 0"}) {
    require(!io::PoseReceiver::parse(packet, pose), "bad packet accepted: " + packet);
  }
  tools::Config config;
  config.pose_mode = "udp";
  config.pose_bind_address = "127.0.0.1";
  config.max_pose_gap_ms = 20;
  io::PoseReceiver receiver(config, false);
  target_position::Pose before, after;
  before.timestamp_ns = 10000000;
  before.position_valid = true;
  after = before;
  after.timestamp_ns = 20000000;
  after.q_body2world = {-std::sqrt(0.5), 0, 0, -std::sqrt(0.5)};  // q与-q等价。
  after.position_in_world = {2, 4, 6};
  require(receiver.push(before) && receiver.push(after), "samples rejected");
  require(!receiver.push(before), "out-of-order sample accepted");
  std::string reason;
  require(receiver.sample(15000000, pose, reason), "bracketed sample failed");
  const auto forward = tools::quaternion_to_rotation(pose.q_body2world) * cv::Vec3d(1, 0, 0);
  require(
    cv::norm(forward - cv::Vec3d(std::sqrt(0.5), std::sqrt(0.5), 0)) < 1e-8, "SLERP not yaw45");
  require(
    cv::norm(pose.position_in_world - cv::Vec3d(1, 2, 3)) < 1e-12, "position interpolation wrong");
  require(!receiver.sample(25000000, pose, reason), "future extrapolation allowed");
  require(!receiver.sample(5000000, pose, reason), "past extrapolation allowed");
  after.timestamp_ns = 80000000;
  require(receiver.push(after), "third sample rejected");
  require(!receiver.sample(50000000, pose, reason), "large pose gap accepted");
  config.pose_mode = "none";
  require(!io::PoseReceiver(config).sample(1, pose, reason), "disabled pose accepted");
}

void config_and_output_test()
{
  tools::Config config;
  bool rejected = false;
  try {
    tools::validate_config(config);
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "placeholder config accepted");
  config.localization_enabled = false;
  config.debug_stream = true;
  tools::validate_config(config);
  config.localization_enabled = true;
  config.calibration_valid = config.extrinsics_valid = true;
  config.camera_matrix = {800, 0, 320, 0, 800, 240, 0, 0, 1};
  config.target_width = 0.4;
  config.target_height = 0.25;
  tools::validate_config(config);
  config.calibration_width = 320;
  rejected = false;
  try {
    tools::validate_config(config);
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "mismatching calibration dimensions accepted");

  const std::string yaml_path = "/tmp/vision_fly_config_test_" + std::to_string(getpid()) + ".yaml";
  {
    std::ofstream yaml(yaml_path);
    yaml << "%YAML:1.0\n---\n# 中文标定参数读取验证\n"
            "calibration_valid: 1\nextrinsics_valid: 1\n"
            "target_width: 0.4\ntarget_height: 0.25\n"
            "camera_matrix: [800.,0.,320.,0.,800.,240.,0.,0.,1.]\n"
            "distort_coeffs: [-0.1,0.02,0.001,-0.002,0.]\n";
  }
  const auto loaded = tools::load_config(yaml_path);
  require(std::abs(loaded.camera_matrix(0, 0) - 800) < 1e-12, "YAML intrinsics not loaded");
  require(std::abs(loaded.distort_coeffs[3] + 0.002) < 1e-12, "YAML distortion order wrong");
  std::remove(yaml_path.c_str());

  const std::string path = "/tmp/vision_fly_result_test_" + std::to_string(getpid()) + ".jsonl";
  {
    tools::ResultWriter writer(path);
    writer.write(123, 456, nullptr, "no_target");
    target_position::Target target;
    target.xyz_in_camera = {1, 2, 3};
    target.xyz_in_body = {3, 1, 2};
    writer.write(789, 1000, &target, "camera_body_only:pose_missing");
  }
  std::ifstream input(path);
  std::string line;
  std::getline(input, line);
  require(line.find("\"valid\":false") != std::string::npos, "lost target lacks validity");
  std::getline(input, line);
  require(
    line.find("\"relative_xyz_in_world\":null") != std::string::npos,
    "missing pose emitted world values");
  require(line.find("\"xyz_in_camera\":[1,2,3]") != std::string::npos, "camera output missing");
  std::remove(path.c_str());
}
}  // namespace

int main()
{
  try {
    pose_test();
    config_and_output_test();
    std::cout << "pose/config/output tests passed\n";
  } catch (const std::exception & exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
