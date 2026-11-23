#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "tasks/target_position/detector.hpp"
#include "tasks/target_position/infantry.hpp"

namespace
{
void check(bool ok, const char * message)
{
  if (!ok) throw std::runtime_error(message);
}
}  // namespace

int main()
{
  try {
    constexpr int n = 6300;
    std::vector<float> points(8 * n, 0), colors(4 * n, 0), classes(9 * n, 0);
    tools::Config config;
    config.model_type = "szurpv_infantry_v8n";
    config.localization_enabled = false;
    config.class_id = 3;
    config.corner_mode = "keypoints";
    config.keypoint_indices = {0, 3, 2, 1};
    config.armor_color = 1;
    tools::validate_config(config);
    const std::vector<cv::Point2f> physical{{100, 100}, {100, 150}, {200, 150}, {200, 100}};
    // 覆盖三个stride及其边界，排除网格展平、通道顺序错误。
    for (const int index : {0, 4799, 4800, 5999, 6000, 6299}) {
      std::fill(classes.begin(), classes.end(), 0);
      int stride = index < 4800 ? 8 : (index < 6000 ? 16 : 32);
      int cell = index - (index < 4800 ? 0 : (index < 6000 ? 4800 : 6000));
      int x = cell % (640 / stride), y = cell / (640 / stride);
      for (int p = 0; p < 4; ++p) {
        points[2 * p * n + index] = (physical[p].x / stride - x) / 2;
        points[(2 * p + 1) * n + index] = (physical[p].y / stride - y) / 2;
      }
      colors[n + index] = 0.9f;
      classes[3 * n + index] = 0.95f;
      auto detections =
        target_position::decode_infantry(points.data(), colors.data(), classes.data(), config);
      check(detections.size() == 1, "one armor expected");
      check(detections[0].color_id == 1, "red expected");
      check(std::abs(detections[0].score - 0.95) < 1e-6, "score is class probability");
      for (int p = 0; p < 4; ++p)
        check(cv::norm(detections[0].keypoints[p] - physical[p]) < 1e-4, "decode");
      target_position::Target target;
      std::string reason;
      check(
        target_position::Detector(config).detect(
          cv::Mat::zeros(480, 640, CV_8UC3), detections[0], target, reason),
        "keypoints");
      check(cv::norm(target.points[1] - physical[3]) < 1e-4, "TL TR BR BL order");
      colors[3 * n + index] = 1;
      check(
        target_position::decode_infantry(points.data(), colors.data(), classes.data(), config)
          .empty(),
        "purple rejected");
      colors[3 * n + index] = 0;
      points[index] = std::numeric_limits<float>::quiet_NaN();
      check(
        target_position::decode_infantry(points.data(), colors.data(), classes.data(), config)
          .empty(),
        "NaN rejected");
    }
    // 两个完全重合的候选，只保留高分者。
    std::fill(classes.begin(), classes.end(), 0);
    for (int i : {0, 1}) {
      classes[3 * n + i] = 0.95f - i * 0.1f;
      colors[n + i] = 0.9f;
      for (int p = 0; p < 4; ++p) {
        points[2 * p * n + i] = (physical[p].x / 8 - i) / 2;
        points[(2 * p + 1) * n + i] = physical[p].y / 16;
      }
    }
    check(
      target_position::decode_infantry(points.data(), colors.data(), classes.data(), config)
          .size() == 1,
      "NMS");
    config.armor_color = 0;
    check(
      target_position::decode_infantry(points.data(), colors.data(), classes.data(), config)
        .empty(),
      "color filter");
    config.armor_color = -1;
    config.class_id = 4;
    check(
      target_position::decode_infantry(points.data(), colors.data(), classes.data(), config)
        .empty(),
      "class filter");
    // 全类模式保留同一画面的多个非重叠类别，且单类过滤仍有效。
    config.class_id = -1;
    tools::validate_config(config);
    std::fill(classes.begin(), classes.end(), 0);
    for (int i : {0, 1}) {
      classes[(i == 0 ? 1 : 3) * n + i] = 0.95f;
      colors[n + i] = 0.9f;
      for (int p = 0; p < 4; ++p) {
        points[2 * p * n + i] = ((physical[p].x + i * 200) / 8 - i) / 2;
        points[(2 * p + 1) * n + i] = physical[p].y / 16;
      }
    }
    auto all =
      target_position::decode_infantry(points.data(), colors.data(), classes.data(), config);
    check(all.size() == 2 && all[0].class_id == 1 && all[1].class_id == 3, "all class detections");
    config.class_id = 3;
    check(
      target_position::decode_infantry(points.data(), colors.data(), classes.data(), config)
          .size() == 1,
      "single class retained");
    // 诊断必须区分低分、颜色过滤、非法角点和几何过滤；每次解码都清零。
    std::fill(classes.begin(), classes.end(), 0);
    std::fill(colors.begin(), colors.end(), 0);
    config.class_id = -1;
    config.confidence = 0.4f;
    config.min_quad_area = 15;
    target_position::InfantryDecodeStats stats;
    classes[3 * n] = 0.2f;
    check(target_position::decode_infantry(points.data(), colors.data(), classes.data(), config,
                                          &stats).empty() &&
          std::abs(stats.best_score - .2f) < 1e-6 && stats.above_threshold == 0,
          "below threshold diagnostic");
    classes[3 * n] = .95f;
    colors[3 * n] = 1;
    target_position::decode_infantry(points.data(), colors.data(), classes.data(), config, &stats);
    check(stats.above_threshold == 1 && stats.rejected_color == 1, "color diagnostic");
    colors[3 * n] = 0;
    colors[n] = 1;
    points[0] = std::numeric_limits<float>::quiet_NaN();
    target_position::decode_infantry(points.data(), colors.data(), classes.data(), config, &stats);
    check(stats.rejected_color == 0 && stats.rejected_keypoints == 1, "keypoint diagnostic reset");
    const std::vector<cv::Point2f> small{{100, 100}, {100, 103}, {110, 103}, {110, 100}};
    for (int p = 0; p < 4; ++p) {
      points[2 * p * n] = small[p].x / 16;
      points[(2 * p + 1) * n] = small[p].y / 16;
    }
    auto small_result = target_position::decode_infantry(points.data(), colors.data(), classes.data(),
                                                       config, &stats);
    check(small_result.size() == 1 && stats.rejected_area == 0, "small quad survives pixel area");
    config.min_quad_area = 31;
    check(target_position::decode_infantry(points.data(), colors.data(), classes.data(), config,
                                          &stats).empty() && stats.rejected_area == 1,
          "area diagnostic");
    std::cout << "infantry checks passed\n";
  } catch (const std::exception & error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
