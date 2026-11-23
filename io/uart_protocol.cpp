#include "uart_protocol.hpp"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace io
{
CoordinatePacket coordinate_packet(
  int64_t timestamp_ns, const target_position::Target * target,
  const std::string & frame, bool static_pose)
{
  CoordinatePacket packet;
  packet.timestamp_ns = timestamp_ns;
  if (frame == "body") {
    packet.frame = "FRD";
    if (target) { packet.xyz = target->xyz_in_body; packet.valid = true; }
  } else if (frame == "camera") {
    packet.frame = "CAM";
    if (target) { packet.xyz = target->xyz_in_camera; packet.valid = true; }
  } else if (frame == "world_relative") {
    packet.frame = static_pose ? "NED_STATIC" : "NED";
    if (target && target->world_valid) {
      packet.xyz = target->relative_xyz_in_world;
      packet.valid = true;
    }
  } else {
    throw std::invalid_argument("未知串口坐标系");
  }
  for (int i = 0; i < 3; ++i) packet.valid = packet.valid && std::isfinite(packet.xyz[i]);
  return packet;
}

uint16_t uart_crc16(const std::string & payload)
{
  uint16_t crc = 0xffff;
  for (const unsigned char byte : payload) {
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

std::string encode_coordinates(uint32_t sequence, const CoordinatePacket & packet)
{
  bool valid = packet.valid;
  for (int i = 0; i < 3; ++i) valid = valid && std::isfinite(packet.xyz[i]);
  std::ostringstream body;
  body.imbue(std::locale::classic());
  body << "XYZ," << sequence << ',' << packet.timestamp_ns << ',' << (valid ? 1 : 0)
       << ',' << packet.frame << ',';
  if (valid)
    body << std::fixed << std::setprecision(4) << packet.xyz[0] << ',' << packet.xyz[1]
         << ',' << packet.xyz[2];
  else
    body << "nan,nan,nan";
  std::ostringstream output;
  output << body.str() << '*' << std::uppercase << std::hex << std::setw(4)
         << std::setfill('0') << uart_crc16(body.str()) << "\r\n";
  return output.str();
}

std::vector<std::string> UartLines::feed(const char * data, size_t length)
{
  std::vector<std::string> lines;
  for (size_t i = 0; i < length; ++i) {
    const char c = data[i];
    if (c == '\n') {
      if (!discarding_) {
        if (!line_.empty() && line_.back() == '\r') line_.pop_back();
        if (!line_.empty()) lines.push_back(line_);
      }
      line_.clear();
      discarding_ = false;
    } else if (!discarding_) {
      if (line_.size() == 256) {
        line_.clear(); discarding_ = true; ++dropped_;
      } else {
        line_ += c;
      }
    }
  }
  return lines;
}

std::string escape_uart_text(const std::string & text)
{
  std::ostringstream out;
  for (const unsigned char c : text) {
    if (c == '\\') out << "\\\\";
    else if (c < 32 || c == 127)
      out << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    else out << c;
  }
  return out.str();
}
}  // namespace io
