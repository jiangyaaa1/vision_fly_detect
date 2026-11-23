#ifndef TARGET_POSITION__SOLVER_HPP
#define TARGET_POSITION__SOLVER_HPP

#include <string>

#include "target.hpp"
#include "tools/config.hpp"

namespace target_position
{
// 已知尺寸平面矩形的 PnP：不依赖 MaixCAM 默认关闭的 OpenCV calib3d。
// 去畸变 -> 单应性分解 -> SO(3)投影 -> 多初值重投影最小二乘。
class Solver
{
public:
  explicit Solver(const tools::Config & config);

  bool solve(Target & target, std::string & reason) const;
  void transform(Target & target, const Pose * pose) const;

private:
  tools::Config config_;
};
}  // namespace target_position

#endif  // TARGET_POSITION__SOLVER_HPP
