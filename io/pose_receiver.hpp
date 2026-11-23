#ifndef IO__POSE_RECEIVER_HPP
#define IO__POSE_RECEIVER_HPP

#include <deque>
#include <string>

#include "tasks/target_position/target.hpp"
#include "tools/config.hpp"

namespace io
{
// 单线程、非阻塞读取；datagram时间戳必须已映射到本板单调时钟。
class PoseReceiver
{
public:
  // open_udp=false用于离线回放/测试，由push注入带时间戳的姿态。
  explicit PoseReceiver(const tools::Config & config, bool open_udp = true);
  ~PoseReceiver();
  PoseReceiver(const PoseReceiver &) = delete;
  PoseReceiver & operator=(const PoseReceiver &) = delete;

  void poll();
  bool sample(int64_t timestamp_ns, target_position::Pose & pose, std::string & reason) const;
  bool push(const target_position::Pose & pose);
  static bool parse(const std::string & packet, target_position::Pose & pose);

private:
  tools::Config config_;
  int socket_ = -1;
  std::deque<target_position::Pose> poses_;
};
}  // namespace io

#endif  // IO__POSE_RECEIVER_HPP
