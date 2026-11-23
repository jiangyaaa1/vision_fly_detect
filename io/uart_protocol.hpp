#ifndef IO__UART_PROTOCOL_HPP
#define IO__UART_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>
#include "tasks/target_position/target.hpp"

namespace io
{
struct CoordinatePacket
{
  int64_t timestamp_ns = 0;
  bool valid = false;
  std::string frame = "FRD";
  cv::Vec3d xyz{0, 0, 0};
};

CoordinatePacket coordinate_packet(
  int64_t timestamp_ns, const target_position::Target * target,
  const std::string & frame, bool static_pose);
uint16_t uart_crc16(const std::string & payload);
std::string encode_coordinates(uint32_t sequence, const CoordinatePacket & packet);

// 串口是字节流：支持半包、粘包；超长行整行丢弃，下一个换行后恢复。
class UartLines
{
public:
  std::vector<std::string> feed(const char * data, size_t length);
  size_t dropped() const { return dropped_; }
private:
  std::string line_;
  bool discarding_ = false;
  size_t dropped_ = 0;
};
std::string escape_uart_text(const std::string & text);
}  // namespace io
#endif  // IO__UART_PROTOCOL_HPP
