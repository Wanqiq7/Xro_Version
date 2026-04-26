#pragma once

namespace App {

// ChassisState Topic：描述底盘当前运动状态。
//
// 字段分为三层：
// 1. `feedback_*`：真实传感器或执行器反馈；
// 2. `estimated_*`：当前阶段的估计值；
// 3. `command_*`：上层下发的命令镜像。
//
// 为兼容既有消费方，`vx_mps/vy_mps/wz_radps/yaw_deg` 继续保留：
// - `wz_radps` 优先使用真实陀螺仪反馈；
// - `yaw_deg` 当前是基于陀螺仪积分得到的相对航向估计；
// - `vx_mps/vy_mps` 当前仍是估计值，不应视为真实轮速里程结果。
struct ChassisState {
  bool online = false;
  bool ready = false;
  bool feedback_online = false;
  bool velocity_feedback_valid = false;
  bool yaw_feedback_valid = false;

  float vx_mps = 0.0f;
  float vy_mps = 0.0f;
  float wz_radps = 0.0f;
  float yaw_deg = 0.0f;

  float estimated_vx_mps = 0.0f;
  float estimated_vy_mps = 0.0f;
  float feedback_wz_radps = 0.0f;
  float relative_yaw_deg = 0.0f;

  float command_vx_mps = 0.0f;
  float command_vy_mps = 0.0f;
  float command_wz_radps = 0.0f;
};

}  // namespace App
