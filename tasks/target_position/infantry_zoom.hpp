#ifndef TARGET_POSITION__INFANTRY_ZOOM_HPP
#define TARGET_POSITION__INFANTRY_ZOOM_HPP

#include "target.hpp"
#include "tools/config.hpp"

namespace target_position
{
// 每帧只选一个窗口；不会跨帧合并检测结果或沿用旧的三维坐标。
class InfantryZoom
{
public:
  explicit InfantryZoom(const tools::Config & config);
  cv::Rect next_roi();
  void update(const std::vector<Detection> & detections);
  static void restore(Detection & detection, const cv::Rect & roi);

private:
  bool enabled_;
  int full_interval_, crop_width_;
  float track_confidence_;
  double min_edge_;
  uint64_t frame_ = 0;
  int tile_ = 0, misses_ = 0;
  bool tracking_ = false;
  bool last_tracking_ = false;
  cv::Rect tracked_box_, last_roi_{0, 0, 640, 480};
};
}  // namespace target_position
#endif  // TARGET_POSITION__INFANTRY_ZOOM_HPP
