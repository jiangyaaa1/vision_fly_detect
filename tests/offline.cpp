#include <iostream>
#include <opencv2/imgcodecs.hpp>

#include "tasks/target_position/detector.hpp"
#include "tasks/target_position/solver.hpp"
#include "tools/result_writer.hpp"

// 对录制的原分辨率照片测试真实轮廓与PnP；框来自人工指定，不执行神经网络。
int main(int argc, char * argv[])
{
  if (argc != 7) {
    std::cerr << "用法: vision_offline config.yaml image.png box_x box_y box_w box_h\n";
    return 2;
  }
  try {
    const auto config = tools::load_config(argv[1]);
    if (config.corner_mode != "contour") throw std::runtime_error("离线工具仅支持contour模式");
    const auto image = cv::imread(argv[2]);
    if (image.cols != config.image_width || image.rows != config.image_height) {
      throw std::runtime_error("照片尺寸必须与标定尺寸相同");
    }
    target_position::Detection detection;
    detection.class_id = config.class_id;
    detection.score = 1;
    detection.box = {
      std::stoi(argv[3]), std::stoi(argv[4]), std::stoi(argv[5]), std::stoi(argv[6])};
    target_position::Target target;
    std::string reason;
    const bool valid = target_position::Detector(config).detect(image, detection, target, reason) &&
                       target_position::Solver(config).solve(target, reason);
    tools::ResultWriter writer("");
    writer.write(
      tools::monotonic_ns(), tools::monotonic_ns(), valid ? &target : nullptr,
      valid ? "offline_camera_body_only" : reason);
    return valid ? 0 : 1;
  } catch (const std::exception & exception) {
    std::cerr << exception.what() << '\n';
    return 2;
  }
}
