#include "pose_receiver.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace io
{
PoseReceiver::PoseReceiver(const tools::Config & config, bool open_udp) : config_(config)
{
  if (config_.pose_mode != "udp" || !open_udp) return;
  socket_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (socket_ < 0) throw std::runtime_error("无法创建姿态UDP接口");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(config_.pose_port);
  if (
    inet_pton(AF_INET, config_.pose_bind_address.c_str(), &address.sin_addr) != 1 ||
    bind(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
    close(socket_);
    socket_ = -1;
    throw std::runtime_error("无法绑定姿态UDP地址: " + config_.pose_bind_address);
  }
}

PoseReceiver::~PoseReceiver()
{
  if (socket_ >= 0) close(socket_);
}

bool PoseReceiver::parse(const std::string & packet, target_position::Pose & pose)
{
  target_position::Pose value;
  std::istringstream stream(packet);
  std::string tag;
  if (!(stream >> tag >> value.timestamp_ns) || tag != "POSE" || value.timestamp_ns <= 0) {
    return false;
  }
  for (int i = 0; i < 4; i++) {
    if (!(stream >> value.q_body2world[i])) return false;
  }
  stream >> std::ws;
  if (!stream.eof()) {
    for (int i = 0; i < 3; i++) {
      if (!(stream >> value.position_in_world[i])) return false;
    }
    value.position_valid = true;
    stream >> std::ws;
    if (!stream.eof()) return false;
  }
  const double norm = cv::norm(value.q_body2world);
  if (!std::isfinite(norm) || norm < 0.5 || norm > 1.5) return false;
  if (!cv::checkRange(cv::Mat(value.position_in_world))) return false;
  value.q_body2world /= norm;
  pose = value;
  return true;
}

bool PoseReceiver::push(const target_position::Pose & pose)
{
  if (
    pose.timestamp_ns <= 0 ||
    (!poses_.empty() && pose.timestamp_ns <= poses_.back().timestamp_ns)) {
    return false;
  }
  const double norm = cv::norm(pose.q_body2world);
  if (!std::isfinite(norm) || norm < 0.5 || norm > 1.5) return false;
  if (!cv::checkRange(cv::Mat(pose.position_in_world))) return false;
  auto normalized = pose;
  normalized.q_body2world /= norm;
  poses_.push_back(normalized);
  while (poses_.size() > 512 ||
         poses_.back().timestamp_ns - poses_.front().timestamp_ns > 3000000000LL) {
    poses_.pop_front();
  }
  return true;
}

void PoseReceiver::poll()
{
  if (socket_ < 0) return;
  // 限制单帧处理量，避免异常输入让视觉主循环饥饿。
  for (int i = 0; i < 512; i++) {
    char buffer[512];
    const auto count = recv(socket_, buffer, sizeof(buffer), MSG_TRUNC);
    if (count < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      if (errno == EINTR) continue;
      throw std::runtime_error(std::string("读取姿态失败: ") + std::strerror(errno));
    }
    if (count == 0 || count >= static_cast<int>(sizeof(buffer))) continue;
    target_position::Pose pose;
    if (!parse(std::string(buffer, count), pose)) continue;
    const auto age_ns = tools::monotonic_ns() - pose.timestamp_ns;
    if (age_ns < -1000000000LL || age_ns > 3000000000LL) continue;
    push(pose);
  }
}

bool PoseReceiver::sample(
  int64_t timestamp_ns, target_position::Pose & pose, std::string & reason) const
{
  if (config_.pose_mode == "none") {
    reason = "pose_disabled";
    return false;
  }
  if (config_.pose_mode == "static") {
    pose = target_position::Pose{};
    pose.timestamp_ns = timestamp_ns;
    pose.q_body2world = config_.static_q_body2world / cv::norm(config_.static_q_body2world);
    return true;
  }
  if (poses_.empty()) {
    reason = "pose_missing";
    return false;
  }
  const int64_t gap_ns = static_cast<int64_t>(config_.max_pose_gap_ms * 1e6);
  const auto next = std::lower_bound(
    poses_.begin(), poses_.end(), timestamp_ns,
    [](const target_position::Pose & value, int64_t time) { return value.timestamp_ns < time; });
  if (next != poses_.end() && next->timestamp_ns == timestamp_ns) {
    pose = *next;
    return true;
  }
  // 不外推、不把最新姿态强行配给旧图像；无双侧样本时拒绝世界坐标。
  if (next == poses_.begin() || next == poses_.end()) {
    reason = "pose_not_bracketed";
    return false;
  }
  const auto previous = std::prev(next);
  if (next->timestamp_ns - previous->timestamp_ns > gap_ns) {
    reason = "pose_gap_too_large";
    return false;
  }
  const double alpha = static_cast<double>(timestamp_ns - previous->timestamp_ns) /
                       static_cast<double>(next->timestamp_ns - previous->timestamp_ns);
  auto a = previous->q_body2world;
  auto b = next->q_body2world;
  double cosine = a.dot(b);
  if (cosine < 0) {
    b = -b;
    cosine = -cosine;
  }
  cosine = std::clamp(cosine, -1.0, 1.0);
  pose = target_position::Pose{};
  pose.timestamp_ns = timestamp_ns;
  if (cosine > 0.9995) {
    pose.q_body2world = a * (1 - alpha) + b * alpha;
  } else {
    const double angle = std::acos(cosine);
    pose.q_body2world =
      (a * std::sin((1 - alpha) * angle) + b * std::sin(alpha * angle)) / std::sin(angle);
  }
  pose.q_body2world /= cv::norm(pose.q_body2world);
  pose.position_valid = previous->position_valid && next->position_valid;
  if (pose.position_valid) {
    pose.position_in_world =
      previous->position_in_world * (1 - alpha) + next->position_in_world * alpha;
  }
  return true;
}
}  // namespace io
