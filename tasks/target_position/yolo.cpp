#include "yolo.hpp"
#include "infantry.hpp"
#include "maix_image_cv.hpp"

#include <algorithm>
#include <memory>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace target_position
{
namespace
{
bool infantry_shape(const std::vector<int> & shape, int channels)
{
  // cv181x运行时会把ONNX的三维输出补成四维，尾部1不改变连续CHW存储。
  return shape == std::vector<int>({1, channels, 6300}) ||
         shape == std::vector<int>({1, channels, 6300, 1});
}
}  // namespace
// 关闭双缓冲，保证角点、图像和姿态处理的是同一帧。
YOLO::YOLO(const tools::Config & config) : config_(config), zoom_(config)
{
  if (config.model_type == "szurpv_infantry_v8n") {
    infantry_.reset(new maix::nn::NN(config.model_path, false));
    const auto inputs = infantry_->inputs_info();
    if (inputs.size() != 1 || inputs[0].shape != std::vector<int>({1, 3, 480, 640})) {
      throw std::runtime_error("深大模型输入必须为[1,3,480,640]");
    }
    const auto outputs = infantry_->outputs_info();
    if (outputs.size() != 3) throw std::runtime_error("请使用本工程裁剪转换的三输出深大模型");
    for (const int channels : {8, 4, 9}) {
      int matched = 0;
      for (const auto & output : outputs) {
        if (infantry_shape(output.shape, channels)) ++matched;
      }
      if (matched != 1) throw std::runtime_error("深大模型输出shape不匹配");
    }
  } else {
    detector_.reset(new maix::nn::YOLOv8(config.model_path, false));
  }
}

maix::image::Format YOLO::input_format()
{
  return infantry_ ? maix::image::FMT_RGB888 : detector_->input_format();
}

std::vector<Detection> YOLO::detect(maix::image::Image & image)
{
  const auto started = tools::monotonic_ns();
  if (infantry_) {
    if (image.width() != 640 || image.height() != 480 || image.format() != maix::image::FMT_RGB888) {
      throw std::runtime_error("深大模型相机原图需要640x480 RGB，必须与标定一致");
    }
    inference_roi_ = zoom_.next_roi();
    cv::Mat raw, resized;
    std::unique_ptr<maix::image::Image> cropped;
    if (inference_roi_ != cv::Rect(0, 0, 640, 480)) {
      maix::err::check_raise(maix::image::image2cv(image, raw, false, false));
      cv::resize(raw(inference_roi_), resized, {640, 480}, 0, 0, cv::INTER_LINEAR);
      // resized必须存活至同步forward_image完成；保持RGB，不重复转换颜色。
      cropped.reset(maix::image::cv2image(resized, false, false));
      maix::err::check_null_raise(cropped.get());
    }
    std::unique_ptr<maix::tensor::Tensors> outputs(infantry_->forward_image(
      cropped ? *cropped : image, {0, 0, 0}, {1.0f / 255, 1.0f / 255, 1.0f / 255},
      maix::image::FIT_FILL, true));
    const auto forwarded = tools::monotonic_ns();
    forward_ms_ = (forwarded - started) / 1e6;
    if (!outputs) throw std::runtime_error("深大模型推理无输出");
    const float * points = nullptr;
    const float * colors = nullptr;
    const float * classes = nullptr;
    for (const auto & entry : *outputs) {
      auto * tensor = entry.second;
      if (!tensor || tensor->dtype() != maix::tensor::DType::FLOAT32) {
        throw std::runtime_error("深大模型输出必须由SDK反量化为float32");
      }
      const auto shape = tensor->shape();
      if (infantry_shape(shape, 8)) points = static_cast<float *>(tensor->data());
      if (infantry_shape(shape, 4)) colors = static_cast<float *>(tensor->data());
      if (infantry_shape(shape, 9)) classes = static_cast<float *>(tensor->data());
    }
    auto decode_config = config_;
    // 面积门限始终代表原图像素平方；不能因放大使小于门限的目标偷偷通过。
    decode_config.min_quad_area *= 640.0 * 480 / inference_roi_.area();
    auto detections = decode_infantry(points, colors, classes, decode_config,
                                      config_.infantry_diagnostics ? &decode_stats_ : nullptr);
    for (auto & detection : detections) InfantryZoom::restore(detection, inference_roi_);
    zoom_.update(detections);
    decode_ms_ = (tools::monotonic_ns() - forwarded) / 1e6;
    return detections;
  }
  std::unique_ptr<maix::nn::Objects> objects(
    detector_->detect(image, config_.confidence, config_.iou, maix::image::FIT_CONTAIN, 0.5f));
  const auto forwarded = tools::monotonic_ns();
  forward_ms_ = (forwarded - started) / 1e6;
  if (!objects) throw std::runtime_error("YOLO返回空结果指针");
  std::vector<Detection> detections;
  for (const auto * object : *objects) {
    if (object->class_id != config_.class_id) continue;
    Detection detection;
    detection.class_id = object->class_id;
    detection.score = object->score;
    detection.box = {object->x, object->y, object->w, object->h};
    for (size_t i = 0; i + 1 < object->points.size(); i += 2) {
      detection.keypoints.emplace_back(object->points[i], object->points[i + 1]);
    }
    detections.push_back(detection);
  }
  std::sort(detections.begin(), detections.end(), [](const Detection & a, const Detection & b) {
    return a.score > b.score;
  });
  decode_ms_ = (tools::monotonic_ns() - forwarded) / 1e6;
  return detections;
}
}  // namespace target_position
