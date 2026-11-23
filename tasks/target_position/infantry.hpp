#ifndef TARGET_POSITION__INFANTRY_HPP
#define TARGET_POSITION__INFANTRY_HPP

#include "target.hpp"
#include "tools/config.hpp"

namespace target_position
{
struct InfantryDecodeStats
{
  float best_score = 0;
  int above_threshold = 0;
  int rejected_color = 0;
  int rejected_keypoints = 0;
  int rejected_area = 0;
};
// 固定20260726模型：三个NCHW输出，6300个候选；不适用于标准YOLOv8-Pose。
std::vector<Detection> decode_infantry(
  const float * raw_points, const float * colors, const float * classes,
  const tools::Config & config, InfantryDecodeStats * stats = nullptr);
}  // namespace target_position
#endif  // TARGET_POSITION__INFANTRY_HPP
