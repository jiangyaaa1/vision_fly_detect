#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <opencv2/core.hpp>
#include "maix_camera.hpp"

namespace tools
{
// 仅在采集线程操作同一个Camera；不重启推理，不让第二个进程抢占传感器。
class LiveExposure
{
public:
  LiveExposure(std::string path, int fps, bool automatic, int exposure, int gain)
  : path_(std::move(path)), fps_(fps), automatic_(automatic), exposure_(exposure), gain_(gain) {}

  void poll(maix::camera::Camera & camera, int64_t now)
  {
    if (pending_ > 0 && --pending_ == 0) {
      const auto temporary = path_ + ".exposure.json.tmp";
      std::ofstream out(temporary);
      out << "{\"id\":\"" << id_ << "\",\"exposure_us\":" << camera.exposure()
          << ",\"gain\":" << camera.gain() << ",\"mode\":"
          << static_cast<int>(camera.exp_mode()) << "}";
      out.close();
      std::rename(temporary.c_str(), (path_ + ".exposure.json").c_str());
    }
    if (now < next_poll_) return;
    next_poll_ = now + 500000000;
    try {
      cv::FileStorage file(path_, cv::FileStorage::READ);
      if (!file.isOpened()) return;
      int automatic = automatic_, exposure = exposure_, gain = gain_;
      std::string id;
      if (!file["camera_auto_exposure"].empty()) file["camera_auto_exposure"] >> automatic;
      if (!file["camera_exposure_us"].empty()) file["camera_exposure_us"] >> exposure;
      if (!file["camera_gain"].empty()) file["camera_gain"] >> gain;
      if (!file["camera_control_id"].empty()) file["camera_control_id"] >> id;
      if (automatic == automatic_ && exposure == exposure_ && gain == gain_ && id == id_) return;
      // 请求ID仅允许数字，写回JSON无需接收任意字符串。
      if (id.find_first_not_of("0123456789") != std::string::npos ||
          (automatic != 0 && automatic != 1) || exposure <= 0 || exposure > 1000000 ||
          (!automatic && exposure > 1000000 / fps_) || gain < 1024 || gain > 16384)
        return;
      camera.exp_mode(automatic ? maix::camera::AeMode::Auto : maix::camera::AeMode::Manual);
      if (!automatic) { camera.exposure(exposure); camera.gain(gain); }
      automatic_ = automatic; exposure_ = exposure; gain_ = gain; id_ = id;
      pending_ = 3;  // 等待后续采集帧，再读回传感器实际参数。
      std::cerr << "CAMERA_EXPOSURE_LIVE requested_us=" << exposure << " gain=" << gain
                << " auto=" << automatic << '\n';
    } catch (const std::exception & error) {
      std::cerr << "曝光配置暂不可读，保留当前参数: " << error.what() << '\n';
    }
  }

private:
  std::string path_, id_;
  int fps_, automatic_, exposure_, gain_, pending_ = 0;
  int64_t next_poll_ = 0;
};
}  // namespace tools
