#include <fcntl.h>
#include <pty.h>
#include <unistd.h>
#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "io/serial_link.hpp"
#include "io/uart_port.hpp"

namespace
{
void check(bool value, const char * message)
{
  if (!value) throw std::runtime_error(message);
}

void protocol_test()
{
  const auto port = io::uart_port(3);
  check(std::string(port.device) == "/dev/ttyS3" && std::string(port.tx_pin) == "P19" &&
        std::string(port.rx_pin) == "P20" && std::string(port.tx_function) == "UART3_TX" &&
        std::string(port.rx_function) == "UART3_RX", "UART3 mapping");
  check(std::string(io::uart_port(0).device) == "/dev/ttyS0", "UART0 selectable");
  bool invalid_port = false;
  try { io::uart_port(4); } catch (const std::invalid_argument &) { invalid_port = true; }
  check(invalid_port, "invalid UART accepted");
  std::string directory = "/tmp/vision_uart_sdio_" + std::to_string(getpid());
  check(::mkdir(directory.c_str(), 0700) == 0, "SDIO test directory");
  check(!io::uart3_sdio_conflict(directory), "empty SDIO directory occupied");
  const auto card = directory + "/mmc1:0001:1";
  { std::ofstream marker(card); marker << "test"; }
  check(io::uart3_sdio_conflict(directory), "SDIO conflict was not detected");
  ::unlink(card.c_str()); ::rmdir(directory.c_str());
  check(io::uart_crc16("123456789") == 0x29b1, "CRC16-CCITT-FALSE test vector");
  target_position::Target target;
  target.xyz_in_body = {3, -1, .2};
  target.xyz_in_camera = {-1, .2, 3};
  target.relative_xyz_in_world = {1, 2, 3};
  auto packet = io::coordinate_packet(123, &target, "body", false);
  check(packet.valid && packet.frame == "FRD" && packet.xyz[0] == 3, "body selection");
  const auto encoded = io::encode_coordinates(7, packet);
  check(encoded.find("XYZ,7,123,1,FRD,3.0000,-1.0000,0.2000*") == 0, "wire format");
  const auto star = encoded.find('*');
  check(std::stoul(encoded.substr(star + 1, 4), nullptr, 16) ==
        io::uart_crc16(encoded.substr(0, star)), "wire CRC");
  packet = io::coordinate_packet(123, &target, "world_relative", true);
  check(!packet.valid && packet.frame == "NED_STATIC", "missing attitude must reject world");
  target.world_valid = true;
  packet = io::coordinate_packet(123, &target, "world_relative", false);
  check(packet.valid && packet.frame == "NED" && packet.xyz[0] == 1, "relative world selection");
  packet = io::coordinate_packet(123, &target, "camera", false);
  check(packet.valid && packet.xyz[0] == -1 && packet.frame == "CAM", "camera selection");
  target.xyz_in_body[0] = std::numeric_limits<double>::quiet_NaN();
  packet = io::coordinate_packet(123, &target, "body", false);
  check(!packet.valid, "nonfinite coordinate accepted");
  packet = io::coordinate_packet(123, nullptr, "body", false);
  check(io::encode_coordinates(8, packet).find(",0,FRD,nan,nan,nan*") != std::string::npos,
        "lost target must not send origin or old coordinate");
  io::UartLines lines;
  check(lines.feed("PI", 2).empty(), "partial line accepted");
  auto decoded = lines.feed("NG\r\nhello\n", 10);
  check(decoded.size() == 2 && decoded[0] == "PING" && decoded[1] == "hello", "split/merged lines");
  std::string oversized(257, 'X'); oversized += "PING\nPING\n";
  decoded = lines.feed(oversized.data(), oversized.size());
  check(lines.dropped() == 1 && decoded.size() == 1 && decoded[0] == "PING", "overflow recovery");
  check(io::escape_uart_text(std::string("a\0b\t", 4)) == "a\\x00b\\x09", "log control escaping");
  tools::Config config; config.localization_enabled = false;
  config.uart_tx_hz = 20; config.uart_baudrate = 9600;
  bool rejected = false;
  try { tools::validate_config(config); } catch (...) { rejected = true; }
  check(rejected, "oversubscribed baudrate accepted");
}

void transport_test()
{
  int master = -1, slave = -1;
  char name[128]{};
  check(openpty(&master, &slave, name, nullptr, nullptr) == 0, "openpty");
  fcntl(master, F_SETFL, O_NONBLOCK);
  const std::string log_path = "/tmp/vision_uart_test_" + std::to_string(getpid()) + ".log";
  tools::Config config;
  config.uart_rx_log = log_path;
  config.uart_stale_ms = 100;
  io::UartLines received;
  auto wait_line = [&](const std::function<bool(const std::string &)> & match) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
      char data[1024]; const auto n = ::read(master, data, sizeof(data));
      if (n > 0) for (const auto & line : received.feed(data, static_cast<size_t>(n)))
        if (match(line)) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  };
  {
    io::SerialLink serial(config, name);
    ::close(slave); slave = -1;
    target_position::Target target; target.xyz_in_body = {6.25, -.5, .1};
    serial.submit(123456, &target);
    check(wait_line([](const std::string & s) {
      return s.find(",123456,1,FRD,6.2500,-0.5000,0.1000*") != std::string::npos;
    }), "PTY coordinate delivery");
    check(::write(master, "PI", 2) == 2, "write partial ping");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(::write(master, "NG\r\nhello\n", 10) == 10, "write ping and text");
    check(wait_line([](const std::string & s) { return s == "PONG"; }), "PTY PING/PONG");
    check(wait_line([](const std::string & s) {
      return s.find(",0,FRD,nan,nan,nan*") != std::string::npos;
    }), "stale result must become invalid without another submit");
    serial.submit(999999, nullptr);
    check(wait_line([](const std::string & s) {
      return s.find(",999999,0,FRD,nan,nan,nan*") != std::string::npos;
    }), "explicit target loss delivery");
    check(serial.healthy(), "healthy PTY failed");
  }
  ::close(master);
  std::ifstream log(log_path);
  std::ostringstream text; text << log.rdbuf();
  check(text.str().find("\tRX\tPING\n") != std::string::npos &&
        text.str().find("\tRX\thello\n") != std::string::npos, "received text log");
  log.close(); ::unlink(log_path.c_str());
}
}  // namespace
int main()
{
  try { protocol_test(); transport_test(); std::cout << "UART checks passed\n"; }
  catch (const std::exception & e) { std::cerr << e.what() << '\n'; return 1; }
}
