#include "serial_link.hpp"
#include "uart_port.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <termios.h>
#include <unistd.h>

namespace io
{
namespace
{
speed_t baud_speed(int baud)
{
  switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default: throw std::invalid_argument("不支持的UART波特率");
  }
}
}  // namespace

SerialLink::SerialLink(const tools::Config & config, const std::string & test_device)
: config_(config), port_name_(uart_port(config.uart_port).name), latest_(coordinate_packet(0, nullptr, config.uart_coordinate_frame,
                                           config.pose_mode == "static"))
{
  const std::string device = test_device.empty() ? uart_port(config.uart_port).device : test_device;
  fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd_ < 0) throw std::runtime_error("无法打开" + port_name_ + " " + device + ": " + std::strerror(errno));
  try {
    termios attributes{};
    if (tcgetattr(fd_, &attributes) != 0) throw std::runtime_error("无法读取串口参数");
    cfmakeraw(&attributes);
    const auto speed = baud_speed(config.uart_baudrate);
    cfsetispeed(&attributes, speed); cfsetospeed(&attributes, speed);
    attributes.c_cflag = (attributes.c_cflag & ~(CSIZE | PARENB | CSTOPB | CRTSCTS)) |
                         CS8 | CLOCAL | CREAD;
    attributes.c_cc[VMIN] = 0; attributes.c_cc[VTIME] = 0;
    if (tcsetattr(fd_, TCSANOW, &attributes) != 0)
      throw std::runtime_error("无法设置串口8N1参数");
    tcflush(fd_, TCIFLUSH);
    if (!config.uart_rx_log.empty()) {
      rx_log_.open(config.uart_rx_log, std::ios::out | std::ios::app);
      if (!rx_log_) throw std::runtime_error("无法打开UART接收日志");
    }
    worker_ = std::thread(&SerialLink::run, this);
  } catch (...) {
    ::close(fd_); fd_ = -1; throw;
  }
}

SerialLink::~SerialLink()
{
  stopping_ = true;
  if (worker_.joinable()) worker_.join();
  if (fd_ >= 0) ::close(fd_);
}

void SerialLink::submit(int64_t timestamp_ns, const target_position::Target * target)
{
  auto packet = coordinate_packet(timestamp_ns, target, config_.uart_coordinate_frame,
                                  config_.pose_mode == "static");
  std::lock_guard<std::mutex> lock(mutex_);
  latest_ = std::move(packet);
  submitted_ns_ = tools::monotonic_ns();
}

void SerialLink::run()
{
  try {
    UartLines lines;
    std::string pending;
    size_t offset = 0;
    std::deque<std::string> replies;
    uint32_t sequence = 0;
    int64_t next_send = 0, pending_since = 0;
    while (!stopping_) {
      const auto now = tools::monotonic_ns();
      if (pending.empty() && !replies.empty()) {
        pending = std::move(replies.front()); replies.pop_front();
        pending_since = now;
      } else if (pending.empty() && now >= next_send) {
        CoordinatePacket packet;
        int64_t submitted;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          packet = latest_; submitted = submitted_ns_;
        }
        if (!submitted || now - submitted > static_cast<int64_t>(config_.uart_stale_ms) * 1000000) {
          packet.valid = false; packet.timestamp_ns = now;
        }
        pending = encode_coordinates(sequence++, packet);
        pending_since = now;
        next_send = now + 1000000000LL / config_.uart_tx_hz;
      }
      if (!pending.empty() && now - pending_since > 1000000000LL)
        throw std::runtime_error("UART发送超过1秒未完成，链路已停止");
      pollfd descriptor{fd_, static_cast<short>(POLLIN | (pending.empty() ? 0 : POLLOUT)), 0};
      const int ready = ::poll(&descriptor, 1, 5);
      if (ready < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("UART poll失败");
      }
      if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
        throw std::runtime_error("UART连接异常");
      if (descriptor.revents & POLLIN) {
        char buffer[1024];
        const auto count = ::read(fd_, buffer, sizeof(buffer));
        if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
          throw std::runtime_error("UART读取失败");
        if (count > 0) {
          for (const auto & line : lines.feed(buffer, static_cast<size_t>(count))) {
            const auto record = std::to_string(tools::monotonic_ns()) + "\tRX\t" + escape_uart_text(line);
            std::cerr << port_name_ << " " << record << '\n';
            if (rx_log_.is_open()) {
              rx_log_ << record << '\n'; rx_log_.flush();
              if (!rx_log_) throw std::runtime_error("UART接收日志写入失败");
            }
            if (line == "PING" && replies.size() < 8) replies.push_back("PONG\r\n");
          }
        }
      }
      if ((descriptor.revents & POLLOUT) && !pending.empty()) {
        const auto count = ::write(fd_, pending.data() + offset, pending.size() - offset);
        if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
          throw std::runtime_error("UART写入失败");
        if (count > 0) {
          offset += static_cast<size_t>(count);
          if (offset == pending.size()) { pending.clear(); offset = 0; }
        }
      }
    }
  } catch (const std::exception & error) {
    healthy_ = false;
    std::cerr << port_name_ << "_ERROR " << error.what() << '\n';
  }
}
}  // namespace io
