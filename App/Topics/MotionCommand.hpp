#pragma once

#include "RobotMode.hpp"

namespace App {

// MotionCommand Topic：描述底盘闭环消费所需的最小运动指令。
struct MotionCommand {
  MotionModeType motion_mode = MotionModeType::kStop;
  float vx_mps = 0.0f;
  float vy_mps = 0.0f;
  float wz_radps = 0.0f;
  bool is_field_relative = false;
  bool power_limited = false;
  float chassis_power_limit_w = 0.0f;
};

}  // namespace App
