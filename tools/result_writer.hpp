#ifndef TOOLS__RESULT_WRITER_HPP
#define TOOLS__RESULT_WRITER_HPP

#include <fstream>
#include <ostream>

#include "tasks/target_position/target.hpp"

namespace tools
{
class ResultWriter
{
public:
  explicit ResultWriter(const std::string & path);
  void write(
    int64_t timestamp_ns, int64_t completed_ns, const target_position::Target * target,
    const std::string & status);

private:
  std::ofstream file_;
  std::ostream * output_;
};
}  // namespace tools

#endif  // TOOLS__RESULT_WRITER_HPP
