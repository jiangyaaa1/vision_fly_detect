#include "infantry_zoom.hpp"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace target_position
{
InfantryZoom::InfantryZoom(const tools::Config & config)
: enabled_(config.infantry_zoom_enabled), full_interval_(config.infantry_full_interval),
  crop_width_(config.infantry_crop_width),
  track_confidence_(std::max(config.confidence, config.pnp_min_confidence)),
  min_edge_(config.min_corner_edge)
{
  // 与配置读取使用同一检查，避免直接构造时发生取模零/越界裁剪。
  tools::validate_config(config);
}

cv::Rect InfantryZoom::next_roi()
{
  last_tracking_ = false;
  if (!enabled_ || frame_++ % full_interval_ == 0) {
    last_roi_ = {0, 0, 640, 480};
  } else if (tracking_) {
    last_tracking_ = true;
    // 近处大目标扩大窗口，禁止裁掉角点；远处目标至少保留配置大小的背景。
    int width = std::max(crop_width_, std::max(tracked_box_.width * 3, tracked_box_.height * 4));
    width = std::min(640, (width + 3) / 4 * 4);
    const int height = width * 3 / 4;
    const int x = std::clamp(tracked_box_.x + tracked_box_.width / 2 - width / 2, 0, 640 - width);
    const int y = std::clamp(tracked_box_.y + tracked_box_.height / 2 - height / 2, 0, 480 - height);
    last_roi_ = {x, y, width, height};
  } else {
    const int height = crop_width_ * 3 / 4;
    // 四角窗口存在重叠，目标跨图像中线时仍可完整落入其中一个窗口。
    const int index = tile_++ % 4;
    last_roi_ = {index % 2 ? 640 - crop_width_ : 0,
                 index / 2 ? 480 - height : 0, crop_width_, height};
  }
  return last_roi_;
}

void InfantryZoom::update(const std::vector<Detection> & detections)
{
  if (!enabled_) return;
  const Detection * best = nullptr;
  for (const auto & detection : detections) {
    if (!std::isfinite(detection.score) || detection.score < track_confidence_ ||
        detection.keypoints.size() != 4) continue;
    bool usable = true;
    for (int i = 0; i < 4; ++i) {
      if (cv::norm(detection.keypoints[i] - detection.keypoints[(i + 1) % 4]) < min_edge_)
        usable = false;
    }
    if (usable && (!best || detection.score > best->score)) best = &detection;
  }
  if (best) {
    tracked_box_ = best->box;
    tracking_ = true;
    misses_ = 0;
  } else if (tracking_ && last_tracking_ && ++misses_ >= 2) {
    tracking_ = false;
    misses_ = 0;
  }
}

void InfantryZoom::restore(Detection & detection, const cv::Rect & roi)
{
  if (roi == cv::Rect(0, 0, 640, 480)) return;
  // 与cv::resize(INTER_LINEAR)的像素中心映射一致；保留浮点角点，禁止整数截断。
  const double sx = roi.width / 640.0, sy = roi.height / 480.0;
  for (auto & point : detection.keypoints) {
    point.x = static_cast<float>((point.x + 0.5) * sx - 0.5 + roi.x);
    point.y = static_cast<float>((point.y + 0.5) * sy - 0.5 + roi.y);
  }
  detection.box = cv::boundingRect(detection.keypoints) & cv::Rect(0, 0, 640, 480);
}
}  // namespace target_position
