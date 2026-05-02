#pragma once

#include <cstdint>

#include "../Config/InputConfig.hpp"
#include "../Topics/FireCommand.hpp"
#include "../Topics/RobotMode.hpp"

namespace App {

// 键盘手动输入状态，为边沿检测和控制器消费保留稳定字段。
struct ManualKeyState {
  bool w = false;
  bool s = false;
  bool a = false;
  bool d = false;
  bool shift = false;
  bool z = false;
  bool e = false;
  bool r = false;
  bool f = false;
  bool c = false;
};

// 鼠标手动输入状态，坐标增量保持为轻量平铺 DTO。
struct ManualMouseState {
  bool left_button = false;
  bool right_button = false;
  int16_t x_delta = 0;
  int16_t y_delta = 0;
};

// 从按键/鼠标边沿推导出的语义开关请求。
struct ManualInputToggles {
  bool fire_pressed = false;
  bool friction_pressed = false;
  bool lid_pressed = false;
  bool power_boost_pressed = false;
};

struct OperatorInputSnapshot {
  bool has_active_source = false;
  ControlSource control_source = ControlSource::kUnknown;
  bool request_safe_mode = true;
  bool request_manual_mode = false;
  bool request_auto_aim = false;
  bool request_calibration = false;
  bool emergency_latched = true;
  MotionModeType requested_motion_mode = MotionModeType::kStop;
  AimModeType requested_aim_mode = AimModeType::kHold;
  LoaderModeType requested_loader_mode = LoaderModeType::kStop;
  uint8_t requested_burst_count = 0;
  bool lid_open = false;
  float chassis_speed_scale = 0.0f;
  bool power_boost_requested = false;

  bool fire_enabled = false;
  bool friction_enabled = false;
  // 人工遥控强制拨弹/调试语义：仅表示输入意图，不能绕过系统急停、机器人 Safe 或电机安全停机。
  bool ignore_referee_fire_gate = false;
  bool trigger_pressed = false;
  std::uint32_t shot_request_seq = 0;
  float target_bullet_speed_mps = 0.0f;
  float target_shoot_rate_hz = 0.0f;
  float shoot_rate_scale = 0.0f;

  float target_vx_mps = 0.0f;
  float target_vy_mps = 0.0f;
  float target_wz_radps = 0.0f;

  float target_yaw_deg = 0.0f;
  float target_pitch_deg = 0.0f;
  float yaw_rate_degps = 0.0f;
  float pitch_rate_degps = 0.0f;
  bool track_target = false;

  ManualKeyState manual_keys;
  ManualMouseState manual_mouse;
  ManualInputToggles manual_toggles;
};

}  // namespace App
