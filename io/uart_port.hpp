#ifndef IO__UART_PORT_HPP
#define IO__UART_PORT_HPP

#include <cerrno>
#include <dirent.h>
#include <stdexcept>
#include <string>

namespace io
{
struct UartPort
{
  const char * name;
  const char * device;
  const char * tx_pin;
  const char * rx_pin;
  const char * tx_function;
  const char * rx_function;
};

inline UartPort uart_port(int number)
{
  static const UartPort ports[] = {
    {"UART0", "/dev/ttyS0", "A16", "A17", "UART0_TX", "UART0_RX"},
    {"UART1", "/dev/ttyS1", "A19", "A18", "UART1_TX", "UART1_RX"},
    {"UART2", "/dev/ttyS2", "A28", "A29", "UART2_TX", "UART2_RX"},
    {"UART3", "/dev/ttyS3", "P19", "P20", "UART3_TX", "UART3_RX"},
  };
  if (number < 0 || number > 3) throw std::invalid_argument("uart_port必须为0到3");
  return ports[number];
}

// UART3与Wi-Fi SDIO1引脚共用；存在SDIO设备时禁止直接改写引脚复用。
// 未发现SDIO设备也不能替代实物版本/接线检查，不自动卸载驱动或关闭Wi-Fi。
inline bool uart3_sdio_conflict(const std::string & directory = "/sys/bus/sdio/devices")
{
  DIR * dir = opendir(directory.c_str());
  if (!dir) {
    if (errno == ENOENT) return false;
    throw std::runtime_error("无法检查SDIO占用，尚未设置UART3引脚");
  }
  bool occupied = false;
  while (auto * entry = readdir(dir)) {
    if (entry->d_name[0] != '.') { occupied = true; break; }
  }
  closedir(dir);
  return occupied;
}
}  // namespace io
#endif  // IO__UART_PORT_HPP
