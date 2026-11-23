#ifndef TOOLS__DEBUG_FRAME_HPP
#define TOOLS__DEBUG_FRAME_HPP

#include <memory>
#include "debug_view.hpp"

namespace tools
{
struct DebugItem
{
  target_position::DetectorDebug features;
  target_position::Target target;
  bool corners_valid = false;
};

// 同一帧图像、检测、角点及坐标的快照；像素必须由Mat或image_owner独占持有。
struct DebugFrame
{
  cv::Mat image;
  // image2cv(copy=false)没有Mat数据所有权，持有Camera::read返回的堆图像直至编码结束。
  // 主程序在完成该帧的推理/几何计算后转移所有权，禁止借用可被下一帧复用的DMA缓冲。
  std::shared_ptr<void> image_owner;
  bool rgb = false;
  bool feature_panels = false;
  std::vector<target_position::Detection> detections;
  cv::Rect inference_roi;
  std::vector<DebugItem> items;
  target_position::Target selected;
  bool selected_valid = false;
  std::string status;
  double process_fps = 0;
  double inference_ms = 0;

  // 消费独立图像，后台绘制，避免再次复制整幅图像。
  cv::Mat render();
};
}  // namespace tools
#endif  // TOOLS__DEBUG_FRAME_HPP
