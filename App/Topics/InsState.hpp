#pragma once

#include <array>
#include <cstdint>

namespace App {

struct Vector3f {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

// INS 姿态状态 Topic：作为控制器切换到统一姿态观测前的公共契约。
struct InsState {
  bool online = false;
  bool gyro_online = false;
  bool accl_online = false;

  float yaw_deg = 0.0f;
  float pitch_deg = 0.0f;
  float roll_deg = 0.0f;

  float yaw_rate_degps = 0.0f;
  float pitch_rate_degps = 0.0f;
  float roll_rate_degps = 0.0f;

  float motion_accl_x = 0.0f;
  float motion_accl_y = 0.0f;
  float motion_accl_z = 0.0f;

  std::uint32_t last_update_ms = 0;

  // 扩展姿态字段追加在旧布局之后，降低串口同步端字段错位风险。
  float yaw_total_deg = 0.0f;
  std::array<float, 4> quaternion{1.0f, 0.0f, 0.0f, 0.0f};
  Vector3f gyro_bias_radps{};
  Vector3f corrected_gyro_radps{};
  Vector3f motion_accel_body_g{};
  Vector3f motion_accel_nav_g{};
  Vector3f motion_accel_world_g{};
};

}  // namespace App
