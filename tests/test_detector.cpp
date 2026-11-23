#include <cmath>
#include <iostream>
#include <limits>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "tasks/target_position/detector.hpp"

namespace
{
void require(bool condition, const std::string & message)
{
  if (!condition) throw std::runtime_error(message);
}

void require_near(const cv::Point2f & actual, const cv::Point & expected)
{
  require(cv::norm(actual - cv::Point2f(expected)) < 2.5, "physical corner differs from target");
}
}  // namespace

int main()
{
  try {
    tools::Config config;
    config.min_quad_area = 150;
    config.min_fill_ratio = 0.4;
    target_position::Detector detector(config);
    target_position::Detection detection;
    detection.class_id = 0;
    detection.score = 0.95f;
    detection.box = {125, 83, 190, 155};
    target_position::Target target;
    std::string reason;

    // 透视梯形刻意偏离YOLO轴对齐框，验证真实角点和ROI到原图偏移。
    cv::Mat image(360, 480, CV_8UC3, cv::Scalar(15, 15, 15));
    const std::vector<cv::Point> physical{{140, 95}, {300, 115}, {310, 225}, {130, 230}};
    cv::fillConvexPoly(image, physical, cv::Scalar(240, 240, 240));
    cv::rectangle(image, {200, 140, 40, 35}, cv::Scalar(15, 15, 15), cv::FILLED);
    require(detector.detect(image, detection, target, reason), "perspective rectangle: " + reason);
    for (int i = 0; i < 4; i++) require_near(target.points[i], physical[i]);

    cv::Mat inverted;
    cv::bitwise_not(image, inverted);
    require(detector.detect(inverted, detection, target, reason), "dark rectangle: " + reason);
    for (int i = 0; i < 4; i++) require_near(target.points[i], physical[i]);

    cv::Mat blank(image.size(), image.type(), cv::Scalar(15, 15, 15));
    require(!detector.detect(blank, detection, target, reason), "blank bbox is not a target");
    require(!target.world_valid && target.detection.class_id == -1, "failed result must reset");
    cv::Mat circle = blank.clone();
    cv::circle(circle, {220, 160}, 65, cv::Scalar(240, 240, 240), cv::FILLED);
    require(
      !detector.detect(circle, detection, target, reason), "circle has no physical four corners");

    cv::Mat diamond = blank.clone();
    const std::vector<cv::Point> rotated{{220, 80}, {300, 160}, {220, 240}, {140, 160}};
    cv::fillConvexPoly(diamond, rotated, cv::Scalar(240, 240, 240));
    auto diamond_detection = detection;
    diamond_detection.box = {135, 75, 170, 170};
    require(
      !detector.detect(diamond, diamond_detection, target, reason),
      "contour mode must reject ambiguous 45-degree corner semantics");

    auto invalid_box = detection;
    invalid_box.box.x = -5;
    require(!detector.detect(image, invalid_box, target, reason), "out-of-image box must fail");
    invalid_box.box = {450, 80, 60, 60};
    require(!detector.detect(image, invalid_box, target, reason), "right-clipped box must fail");
    invalid_box.box = {130, 90, std::numeric_limits<int>::max(), 100};
    require(!detector.detect(image, invalid_box, target, reason), "integer overflow box must fail");

    cv::Mat clipped = blank.clone();
    cv::rectangle(clipped, {0, 100, 160, 120}, cv::Scalar(240, 240, 240), cv::FILLED);
    auto edge_detection = detection;
    edge_detection.box = {0, 95, 165, 130};
    require(
      !detector.detect(clipped, edge_detection, target, reason), "frame border is not a corner");

    config.corner_mode = "keypoints";
    config.keypoint_indices = {2, 0, 3, 1};
    target_position::Detector keypoint_detector(config);
    // 目标旋转90°；语义左上现在在画面右上，不能重新按图像上下排序。
    detection.keypoints = {{300, 230}, {150, 100}, {300, 100}, {150, 230}};
    require(
      keypoint_detector.detect(image, detection, target, reason), "rotated keypoints: " + reason);
    require_near(target.points[0], {300, 100});
    require_near(target.points[1], {300, 230});
    require_near(target.points[2], {150, 230});
    require_near(target.points[3], {150, 100});

    auto bad = detection;
    bad.keypoints[2].x = std::numeric_limits<float>::quiet_NaN();
    require(!keypoint_detector.detect(image, bad, target, reason), "NaN corner must fail");
    bad = detection;
    bad.keypoints[2].x = -1;
    require(!keypoint_detector.detect(image, bad, target, reason), "negative corner must fail");
    bad = detection;
    bad.keypoints[2].y = image.rows;
    require(!keypoint_detector.detect(image, bad, target, reason), "outside corner must fail");
    bad = detection;
    std::swap(bad.keypoints[0], bad.keypoints[3]);
    require(
      !keypoint_detector.detect(image, bad, target, reason), "crossing point order must fail");
    bad = detection;
    bad.keypoints.resize(2);
    require(!keypoint_detector.detect(image, bad, target, reason), "missing keypoint must fail");

    std::cout << "Detector tests passed\n";
    return 0;
  } catch (const std::exception & error) {
    std::cerr << "Detector test failed: " << error.what() << '\n';
    return 1;
  }
}
