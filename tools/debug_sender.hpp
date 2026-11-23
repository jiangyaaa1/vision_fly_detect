#ifndef TOOLS__DEBUG_SENDER_HPP
#define TOOLS__DEBUG_SENDER_HPP

#include <atomic>
#include <memory>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <opencv2/core.hpp>
#include <thread>

#include "maix_jpg_stream.hpp"
#include "debug_frame.hpp"

namespace tools
{
// 最新帧单槽，编码/网络不阻塞定位；调用方提交后不能再修改Mat的像素。
class DebugSender
{
public:
  DebugSender(maix::http::JpegStreamer & stream, int quality);
  ~DebugSender();
  void submit(const cv::Mat & frame, bool bgr = true);
  void submit(std::unique_ptr<DebugFrame> frame);
  double render_ms() const { return render_us_.load() / 1000.0; }
  double encode_ms() const { return encode_us_.load() / 1000.0; }
  uint64_t replaced_frames() const { return replaced_; }

private:
  void work();
  maix::http::JpegStreamer & stream_;
  int quality_;
  std::mutex mutex_;
  std::condition_variable ready_;
  cv::Mat pending_;
  std::unique_ptr<DebugFrame> pending_frame_;
  std::atomic<int64_t> render_us_{0}, encode_us_{0};
  std::atomic<uint64_t> replaced_{0};
  bool pending_bgr_ = true;
  bool stop_ = false;
  std::exception_ptr error_;
  std::thread worker_;
};
}  // namespace tools
#endif  // TOOLS__DEBUG_SENDER_HPP
