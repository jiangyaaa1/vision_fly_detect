#include "result_writer.hpp"

#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace tools
{
namespace
{
void vector_json(std::ostream & output, const cv::Vec3d & value)
{
  output << '[' << value[0] << ',' << value[1] << ',' << value[2] << ']';
}

std::string escape_json(const std::string & input)
{
  std::string output;
  for (unsigned char character : input) {
    if (character == '"' || character == '\\') output += '\\';
    if (character < 32) {
      output += ' ';  // 状态文本中的换行不应破坏JSON Lines。
    } else {
      output += static_cast<char>(character);
    }
  }
  return output;
}
}  // namespace

ResultWriter::ResultWriter(const std::string & path) : output_(&std::cout)
{
  if (!path.empty()) {
    file_.open(path, std::ios::out | std::ios::app);
    if (!file_) throw std::runtime_error("无法打开输出文件: " + path);
    output_ = &file_;
  }
}

void ResultWriter::write(
  int64_t timestamp_ns, int64_t completed_ns, const target_position::Target * target,
  const std::string & status)
{
  auto & out = *output_;
  out << std::setprecision(10) << "{\"timestamp_ns\":" << timestamp_ns
      << ",\"completed_ns\":" << completed_ns << ",\"status\":\"" << escape_json(status)
      << "\",\"unit\":\"m\",\"world_frame\":\"NED\""
      << ",\"valid\":" << (target ? "true" : "false")
      << ",\"world_valid\":" << (target && target->world_valid ? "true" : "false")
      << ",\"absolute_valid\":" << (target && target->absolute_valid ? "true" : "false");
  if (target) {
    out << ",\"class_id\":" << target->detection.class_id
        << ",\"color_id\":" << target->detection.color_id
        << ",\"confidence\":" << target->detection.score << ",\"corners_px\":[";
    for (size_t i = 0; i < target->points.size(); i++) {
      if (i) out << ',';
      out << '[' << target->points[i].x << ',' << target->points[i].y << ']';
    }
    out << "],\"xyz_in_camera\":";
    vector_json(out, target->xyz_in_camera);
    out << ",\"xyz_in_body\":";
    vector_json(out, target->xyz_in_body);
    out << ",\"reprojection_error_px\":" << target->reprojection_error;
  }
  out << ",\"relative_xyz_in_world\":";
  if (target && target->world_valid)
    vector_json(out, target->relative_xyz_in_world);
  else
    out << "null";
  out << ",\"xyz_in_world\":";
  if (target && target->absolute_valid)
    vector_json(out, target->xyz_in_world);
  else
    out << "null";
  out << "}\n";
  out.flush();
  if (!out) throw std::runtime_error("写入定位结果失败");
}
}  // namespace tools
