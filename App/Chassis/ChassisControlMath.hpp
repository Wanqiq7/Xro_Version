#pragma once

#include "../Topics/GimbalState.hpp"

namespace App {

inline float SelectFollowGimbalYawErrorDeg(const GimbalState& gimbal_state) {
  // 底盘跟随云台必须使用云台相对角，不能使用 INS yaw 纯积分角。
  return gimbal_state.relative_yaw_deg;
}

}  // namespace App
