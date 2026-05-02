#pragma once

#include "RobotMode.hpp"

namespace App {

// AimCommand Topic：描述云台闭环消费所需的最小瞄准指令。
struct AimCommand {
  AimModeType aim_mode = AimModeType::kHold;
  float yaw_deg = 0.0f;
  float pitch_deg = 0.0f;
  float yaw_rate_degps = 0.0f;
  float pitch_rate_degps = 0.0f;
  bool track_target = false;
};

}  // namespace App
