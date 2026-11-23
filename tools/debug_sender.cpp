#include "debug_sender.hpp"

#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <memory>

#include "maix_image_cv.hpp"
#include "tools/config.hpp"

namespace tools
{
DebugSender::DebugSender(maix::http::JpegStreamer & stream, int quality)
: stream_(stream), quality_(quality), worker_(&DebugSender::work, this)
{
}

DebugSender::~DebugSender()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    pending_.release();
    pending_frame_.reset();
  }
  ready_.notify_one();
  if (worker_.joinable()) worker_.join();
}

void DebugSender::submit(const cv::Mat & frame, bool bgr)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (error_) std::rethrow_exception(error_);
  if (!pending_.empty() || pending_frame_) ++replaced_;
  pending_frame_.reset();
  pending_ = frame;  // Mat引用计数保证旧view销毁后像素仍有效；旧待编码帧直接替换。
  pending_bgr_ = bgr;
  ready_.notify_one();
}

void DebugSender::submit(std::unique_ptr<DebugFrame> frame)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (error_) std::rethrow_exception(error_);
  if (!pending_.empty() || pending_frame_) ++replaced_;
  pending_.release();
  pending_frame_ = std::move(frame);
  ready_.notify_one();
}

void DebugSender::work()
{
  // Linux的nice按线程生效；编码低优先级，算力紧张时优先完成真实定位帧。
  setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 5);
  try {
    while (true) {
      cv::Mat frame;
      std::unique_ptr<DebugFrame> job;
      bool bgr;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return stop_ || !pending_.empty() || pending_frame_; });
        if (stop_) return;
        job = std::move(pending_frame_);
        frame = std::move(pending_);
        bgr = pending_bgr_;
        pending_.release();
      }
      const auto render_start = monotonic_ns();
      if (job) {
        bgr = !job->rgb;
        frame = job->render();
      }
      const auto encode_start = monotonic_ns();
      render_us_ = (encode_start - render_start) / 1000;
      std::unique_ptr<maix::image::Image> image(maix::image::cv2image(frame, bgr, false));
      maix::err::check_null_raise(image.get());
      std::unique_ptr<maix::image::Image> jpeg(image->to_jpeg(quality_));
      maix::err::check_null_raise(jpeg.get());
      maix::err::check_raise(stream_.write(jpeg.get()));
      encode_us_ = (monotonic_ns() - encode_start) / 1000;
    }
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = std::current_exception();
  }
}
}  // namespace tools
