#pragma once

namespace App {

struct OperatorInputSnapshot {
  bool has_active_source = false;
  bool request_safe_mode = true;
  bool request_manual_mode = false;

  bool fire_enabled = false;
  bool friction_enabled = false;
  float target_bullet_speed_mps = 0.0f;
  float target_shoot_rate_hz = 0.0f;

  float target_vx_mps = 0.0f;
  float target_vy_mps = 0.0f;
  float target_wz_radps = 0.0f;

  float target_yaw_deg = 0.0f;
  float target_pitch_deg = 0.0f;
  bool track_target = false;
};

}  // namespace App
