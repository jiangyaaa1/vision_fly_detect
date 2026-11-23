#include "infantry.hpp"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace target_position
{
std::vector<Detection> decode_infantry(
  const float * raw_points, const float * colors, const float * classes,
  const tools::Config & config, InfantryDecodeStats * stats)
{
  if (stats) *stats = InfantryDecodeStats{};
  if (!raw_points || !colors || !classes) throw std::invalid_argument("深大模型输出为空");
  if (config.class_id < -1 || config.class_id >= 9) throw std::invalid_argument("装甲板类别越界");
  constexpr int count = 6300;
  std::vector<Detection> candidates;
  int index = 0;
  for (const int stride : {8, 16, 32}) {
    for (int y = 0; y < 480 / stride; ++y) {
      for (int x = 0; x < 640 / stride; ++x, ++index) {
        // 固定类别模式的等价早筛：所选类别低于门限时，不可能通过后面的argmax筛选。
        // 绝大多数背景位置在这里退出，避免每帧对6300项完整解析颜色/所有类别。
        if (config.class_id >= 0) {
          const float selected = classes[config.class_id * count + index];
          if (stats && std::isfinite(selected) && selected >= 0 && selected <= 1)
            stats->best_score = std::max(stats->best_score, selected);
          if (!std::isfinite(selected) || selected < config.confidence || selected > 1) continue;
        } else {
          bool possible = false;
          for (int c = 0; c < 9; ++c) {
            const float value = classes[c * count + index];
            if (stats && std::isfinite(value) && value >= 0 && value <= 1)
              stats->best_score = std::max(stats->best_score, value);
            if (classes[c * count + index] >= config.confidence) {
              possible = true;
              break;
            }
          }
          if (!possible) continue;
        }
        int category = 0, color = 0;
        bool finite = true;
        for (int c = 0; c < 9; ++c) {
          const float score = classes[c * count + index];
          finite = finite && std::isfinite(score) && score >= 0 && score <= 1;
          if (score > classes[category * count + index]) category = c;
        }
        for (int c = 0; c < 4; ++c) {
          const float score = colors[c * count + index];
          finite = finite && std::isfinite(score) && score >= 0 && score <= 1;
          if (score > colors[color * count + index]) color = c;
        }
        // 原仓库以类别最大概率为置信度，颜色不与类别分数相乘，不重复Sigmoid。
        const float score = classes[category * count + index];
        if (stats && finite && score >= 0 && score <= 1)
          stats->best_score = std::max(stats->best_score, score);
        if (
          !finite || (config.class_id >= 0 && category != config.class_id) ||
          score < config.confidence)
          continue;
        if (stats) ++stats->above_threshold;
        if (color == 3 || (config.armor_color >= 0 && color != config.armor_color)) {
          if (stats) ++stats->rejected_color;
          continue;
        }
        Detection detection;
        detection.class_id = category;
        detection.color_id = color;
        detection.score = score;
        for (int p = 0; p < 4; ++p) {
          // 从本次ONNX的Mul/Add常量核对：pixel = (raw * 2 + grid) * stride。
          const float px = (raw_points[(p * 2) * count + index] * 2 + x) * stride;
          const float py = (raw_points[(p * 2 + 1) * count + index] * 2 + y) * stride;
          if (
            !std::isfinite(px) || !std::isfinite(py) || px < 0 || py < 0 || px >= 640 ||
            py >= 480) {
            finite = false;
            break;
          }
          detection.keypoints.emplace_back(px, py);
        }
        if (!finite) {
          if (stats) ++stats->rejected_keypoints;
          continue;
        }
        if (
          !cv::isContourConvex(detection.keypoints) ||
          std::abs(cv::contourArea(detection.keypoints)) < config.min_quad_area)
        {
          if (stats) ++stats->rejected_area;
          continue;
        }
        detection.box = cv::boundingRect(detection.keypoints) & cv::Rect(0, 0, 640, 480);
        if (!detection.box.empty()) candidates.push_back(std::move(detection));
      }
    }
  }
  std::stable_sort(
    candidates.begin(), candidates.end(),
    [](const Detection & a, const Detection & b) { return a.score > b.score; });
  std::vector<Detection> results;
  for (const auto & candidate : candidates) {
    bool suppressed = false;
    for (const auto & kept : results) {
      const double intersection = (candidate.box & kept.box).area();
      const double union_area = candidate.box.area() + kept.box.area() - intersection;
      if (union_area > 0 && intersection / union_area > config.iou) {
        suppressed = true;
        break;
      }
    }
    if (!suppressed) results.push_back(candidate);
  }
  return results;
}
}  // namespace target_position
