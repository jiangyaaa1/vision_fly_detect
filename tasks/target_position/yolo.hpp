#ifndef TARGET_POSITION__YOLO_HPP
#define TARGET_POSITION__YOLO_HPP

#include <maix_nn_yolov8.hpp>
#include <memory>
#include <vector>

#include "target.hpp"
#include "infantry.hpp"
#include "infantry_zoom.hpp"
#include "tools/config.hpp"

namespace target_position
{
class YOLO
{
public:
  explicit YOLO(const tools::Config & config);
  maix::image::Format input_format();
  std::vector<Detection> detect(maix::image::Image & image);
  double forward_ms() const { return forward_ms_; }
  double decode_ms() const { return decode_ms_; }
  cv::Rect inference_roi() const { return inference_roi_; }
  const InfantryDecodeStats & decode_stats() const { return decode_stats_; }

private:
  tools::Config config_;
  InfantryZoom zoom_;
  cv::Rect inference_roi_{0, 0, 640, 480};
  InfantryDecodeStats decode_stats_;
  double forward_ms_ = 0, decode_ms_ = 0;
  std::unique_ptr<maix::nn::YOLOv8> detector_;
  std::unique_ptr<maix::nn::NN> infantry_;
};
}  // namespace target_position

#endif  // TARGET_POSITION__YOLO_HPP
