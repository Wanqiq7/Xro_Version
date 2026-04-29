#pragma once

namespace App {

// GimbalState Topic：描述云台对上层可见的最小状态反馈。
struct GimbalState {
  bool online = false;
  bool ready = false;
  float yaw_deg = 0.0f;
  float pitch_deg = 0.0f;
  float yaw_rate_degps = 0.0f;
  float pitch_rate_degps = 0.0f;
  float relative_yaw_deg = 0.0f;
  float target_yaw_deg = 0.0f;
  float target_pitch_deg = 0.0f;
  float yaw_error_deg = 0.0f;
  float pitch_error_deg = 0.0f;
};

}  // namespace App
