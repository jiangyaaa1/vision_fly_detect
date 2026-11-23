#ifndef TOOLS__BENCHMARK_HPP
#define TOOLS__BENCHMARK_HPP
#include "config.hpp"
namespace tools
{
// 固定图片+明确标记的模拟标定，仅测计算吞吐；不输出真实定位JSON。
int benchmark_fixture(Config config, const std::string & image_path, int frames);
}  // namespace tools
#endif
