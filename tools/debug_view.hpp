#ifndef TOOLS__DEBUG_VIEW_HPP
#define TOOLS__DEBUG_VIEW_HPP

#include "tasks/target_position/detector.hpp"

namespace tools
{
// 绘制始终使用副本，不允许把叠加线条送回检测或PnP。
class DebugView
{
public:
  // private_buffer=true仅用于后台线程消费独立快照，调用方不得共享可写像素。
  explicit DebugView(const cv::Mat & bgr, bool feature_panels = true, bool rgb = false,
                     bool private_buffer = false);
  void detection(const target_position::Detection & detection);
  void search_roi(const cv::Rect & roi);
  void features(const target_position::DetectorDebug & debug);
  void corners(const target_position::Target & target, bool selected);
  void status(
    const std::string & status, const target_position::Target * target, double process_fps = 0,
    double inference_ms = 0);
  const cv::Mat & overlay() const;
  cv::Mat sheet() const;

private:
  cv::Scalar color(cv::Scalar bgr) const;
  bool rgb_ = false;
  cv::Mat overlay_;
  cv::Mat edges_;
  cv::Mat binary_;
};
}  // namespace tools

#endif  // TOOLS__DEBUG_VIEW_HPP
