#include "maix_basic.hpp"
#include "maix_camera.hpp"
#include "maix_display.hpp"
#include "maix_jpg_stream.hpp"

#include <iostream>
#include <csignal>
#include <memory>
#include <stdexcept>
#include <string>

namespace
{
volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int) { stop_requested = 1; }

const char * preview_html = R"html(<!DOCTYPE html>
<html lang="zh-CN" data-app="vision-fly-camera-preview"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MaixCAM 实时相机</title>
<style>body{margin:0;padding:28px;background:#111827;color:#e5e7eb;
font:16px system-ui;text-align:center}img{width:min(100%,960px);height:auto;
border-radius:12px;background:#000}p{color:#9ca3af}</style>
<h1>MaixCAM 实时相机</h1><p>移动镜头前的物体，即可确认实时画面。</p>
<img src="/stream" alt="MaixCAM相机实时画面">
<p>MaixCDK C++ 实时预览 · 关闭网页不会停止相机；停止请发送退出信号。</p>
</html>)html";

int positive_integer(const char * text, int maximum)
{
  size_t parsed = 0;
  const std::string value(text);
  const int number = std::stoi(value, &parsed);
  if (parsed != value.size() || number <= 0 || number > maximum) {
    throw std::invalid_argument("相机尺寸或端口参数无效");
  }
  return number;
}
}  // namespace

int _main(int argc, char * argv[])
{
  int width = 640;
  int height = 480;
  int port = 8000;
  bool use_display = true;
  for (int i = 1; i < argc; ++i) {
    const std::string option(argv[i]);
    if (option == "--no-display") {
      use_display = false;
    } else if (option == "--help") {
      std::cout << "camera_preview [--width 640] [--height 480] [--port 8000] [--no-display]\n";
      return 0;
    } else if ((option == "--width" || option == "--height" || option == "--port") &&
               i + 1 < argc) {
      const int value = positive_integer(argv[++i], option == "--port" ? 65535 : 4096);
      if (option == "--width") width = value;
      if (option == "--height") height = value;
      if (option == "--port") port = value;
    } else {
      throw std::invalid_argument("未知参数或缺少参数值: " + option);
    }
  }

  // 使用与官方/Python 预览一致的自动帧率和三缓冲；小分辨率会选用 720p60 传感器模式。
  std::cout << "PREVIEW_CAMERA_OPENING size=" << width << "x" << height
            << " fps=auto buffers=3" << std::endl;
  // 先声明屏幕所有者，再声明相机：异常退出时相机也会先于屏幕关闭。
  std::unique_ptr<maix::display::Display> screen;
  std::unique_ptr<maix::camera::Camera> camera(
    new maix::camera::Camera(width, height, maix::image::FMT_RGB888, nullptr, -1, 3));
  if (use_display) screen.reset(new maix::display::Display());
  // 流对象在相机之前析构，图像与 JPEG 均由 unique_ptr 管理。
  maix::http::JpegStreamer stream("0.0.0.0", port);
  maix::err::check_raise(stream.set_html(preview_html), "设置预览网页失败");
  maix::err::check_raise(stream.start(), "启动预览服务失败");
  std::cout << "PREVIEW_READY port=" << port << " size=" << camera->width() << "x"
            << camera->height() << std::endl;

  uint64_t count = 0;
  bool first_frame = true;
  uint64_t last_log = maix::time::ticks_ms();
  while (!stop_requested && !maix::app::need_exit()) {
    std::unique_ptr<maix::image::Image> frame(camera->read());
    if (!frame) continue;
    if (screen) maix::err::check_raise(screen->show(*frame), "显示相机画面失败");
    std::unique_ptr<maix::image::Image> jpeg(frame->to_jpeg(80));
    maix::err::check_null_raise(jpeg.get(), "编码 JPEG 失败");
    // 官方 write() 会复制 JPEG 数据，不接管本地 image::Image 的所有权。
    maix::err::check_raise(stream.write(jpeg.get()), "推送相机画面失败");
    if (first_frame) {
      std::cout << "PREVIEW_FIRST_FRAME" << std::endl;
      first_frame = false;
    }
    ++count;
    const uint64_t now = maix::time::ticks_ms();
    if (now - last_log >= 5000) {
      std::cout << "PREVIEW_FRAMES " << count << " fps="
                << count * 1000.0 / (now - last_log) << std::endl;
      count = 0;
      last_log = now;
    }
  }
  // 先停止推流，再停止采集，最后关闭显示，避免 VO 退出时相机仍在采集。
  maix::err::check_raise(stream.stop(), "停止预览服务失败");
  camera.reset();
  screen.reset();
  std::cout << "PREVIEW_STOPPED" << std::endl;
  return 0;
}

int main(int argc, char * argv[])
{
  // SDK 异常宏内部使用未限定的 app/err/log 名称，需在调用处引入命名空间。
  using namespace maix;
  maix::sys::register_default_signal_handle();
  // 此 SDK 默认只处理 SIGINT；为电脑端 stop 命令补上正常 SIGTERM 清理。
  std::signal(SIGTERM, request_stop);
  // HTTP 客户端关闭连接时不直接终止整个采集进程。
  std::signal(SIGPIPE, SIG_IGN);
  CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}
