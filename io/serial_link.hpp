#ifndef IO__SERIAL_LINK_HPP
#define IO__SERIAL_LINK_HPP

#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#include "io/uart_protocol.hpp"
#include "tools/config.hpp"

namespace io
{
// Linux原始串口、非阻塞读写；独立线程，队列只保留最新坐标。
// 引脚由主程序使用MaixCDK pinmap设置；设备名覆盖仅用于主机伪终端测试。
class SerialLink
{
public:
  explicit SerialLink(const tools::Config & config, const std::string & test_device = "");
  ~SerialLink();
  SerialLink(const SerialLink &) = delete;
  SerialLink & operator=(const SerialLink &) = delete;
  void submit(int64_t timestamp_ns, const target_position::Target * target);
  bool healthy() const { return healthy_; }
private:
  void run();
  tools::Config config_;
  std::string port_name_;
  int fd_ = -1;
  std::ofstream rx_log_;
  std::thread worker_;
  std::atomic<bool> stopping_{false}, healthy_{true};
  std::mutex mutex_;
  CoordinatePacket latest_;
  int64_t submitted_ns_ = 0;
};
}  // namespace io
#endif  // IO__SERIAL_LINK_HPP
