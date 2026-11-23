#ifndef TARGET_POSITION__TARGET_HPP
#define TARGET_POSITION__TARGET_HPP

#include <array>
#include <cstdint>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace target_position
{
// 相机: x右/y下/z前；机体FRD: x前/y右/z下；世界NED: x北/y东/z下。
struct Detection
{
  int class_id = -1;
  int color_id = -1;
  float score = 0;
  cv::Rect box;
  std::vector<cv::Point2f> keypoints;
};

struct Target
{
  Detection detection;
  // 物理目标的左上、右上、右下、左下；单位为原始输入图像像素。
  std::array<cv::Point2f, 4> points;
  cv::Vec3d xyz_in_camera{0, 0, 0};
  cv::Vec3d xyz_in_body{0, 0, 0};
  cv::Vec3d relative_xyz_in_world{0, 0, 0};
  cv::Vec3d xyz_in_world{0, 0, 0};
  cv::Vec3d rvec{0, 0, 0};
  double reprojection_error = 0;
  bool world_valid = false;
  bool absolute_valid = false;
};

struct Pose
{
  int64_t timestamp_ns = 0;            // 本板 CLOCK_MONOTONIC 时钟。
  cv::Vec4d q_body2world{1, 0, 0, 0};  // w,x,y,z，主动旋转 FRD -> NED。
  cv::Vec3d position_in_world{0, 0, 0};
  bool position_valid = false;
};
}  // namespace target_position

#endif  // TARGET_POSITION__TARGET_HPP
