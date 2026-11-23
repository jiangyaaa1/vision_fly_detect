#ifndef TARGET_POSITION__DETECTOR_HPP
#define TARGET_POSITION__DETECTOR_HPP

#include <opencv2/core.hpp>
#include <string>

#include "target.hpp"
#include "tools/config.hpp"

namespace target_position
{
struct DetectorDebug
{
  cv::Rect roi;
  cv::Mat edges;
  cv::Mat binary;
  std::vector<std::vector<cv::Point>> contours;  // 原图坐标，面积达到阈值的真实轮廓。
};

// YOLO负责定位；本模块只提取与目标物理尺寸对应的真实角点。
class Detector
{
public:
  explicit Detector(const tools::Config & config);

  bool detect(
    const cv::Mat & bgr, const Detection & detection, Target & target, std::string & reason,
    DetectorDebug * debug = nullptr) const;

private:
  tools::Config config_;
};
}  // namespace target_position

#endif  // TARGET_POSITION__DETECTOR_HPP
