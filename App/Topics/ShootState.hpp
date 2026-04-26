#pragma once

#include <cstdint>

#include "FireCommand.hpp"

namespace App {

// 执行器级反馈：用于区分上层状态汇总与底层执行器观测。
struct ShootActuatorFeedback {
  bool friction_online = false;
  bool loader_online = false;
  bool friction_enabled = false;
  bool loader_enabled = false;
  bool loader_active = false;
  float friction_left_speed_rpm = 0.0f;
  float friction_right_speed_rpm = 0.0f;
  float loader_speed_rpm = 0.0f;
};

// ShootState Topic：描述射击机构对上层可见的最小状态反馈。
struct ShootState {
  bool online = false;
  bool ready = false;
  bool jammed = false;
  bool fire_enable = false;
  LoaderModeType loader_mode = LoaderModeType::kStop;
  bool friction_on = false;
  bool loader_active = false;
  float bullet_speed_mps = 0.0f;
  float target_bullet_speed_mps = 0.0f;
  float shoot_rate_hz = 0.0f;
  float heat = 0.0f;
  ShootActuatorFeedback actuator{};
};

}  // namespace App
