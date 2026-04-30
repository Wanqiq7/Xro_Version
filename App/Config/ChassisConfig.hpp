#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../Modules/DJIMotor/DJIMotor.hpp"

namespace App::Config {

enum class WheelIndex : std::size_t {
  kFrontLeft = 0,
  kFrontRight = 1,
  kRearLeft = 2,
  kRearRight = 3,
};

enum class MotorDirection : int8_t {
  kNormal = 1,
  kReversed = -1,
};

struct WheelMotorConfig {
  WheelIndex wheel = WheelIndex::kFrontLeft;
  std::uint16_t motor_id = 0;
  MotorDirection direction = MotorDirection::kNormal;
};

struct MecanumChassisConfig {
  const char* can_name = "can1";
  float wheel_base_m = 0.300f;
  float track_width_m = 0.240f;
  float wheel_radius_m = 0.076f;
  float gear_ratio = 19.0f;
  float max_linear_speed_mps = 3.0f;
  float max_angular_speed_radps = 6.0f;
  float max_wheel_speed_radps = 80.0f;
  float open_loop_current_limit = 8000.0f;
  std::uint32_t feedback_timeout_ms = 100;
  std::uint32_t command_frame_id = 0x200;
  std::array<WheelMotorConfig, 4> wheel_motors{{
      {WheelIndex::kFrontLeft, 1, MotorDirection::kNormal},
      {WheelIndex::kFrontRight, 2, MotorDirection::kReversed},
      {WheelIndex::kRearLeft, 3, MotorDirection::kNormal},
      {WheelIndex::kRearRight, 4, MotorDirection::kReversed},
  }};
};

inline constexpr MecanumChassisConfig kMecanumChassisConfig{};

// 底盘力控 PID 配置（底盘级 3 个 PID，替代原 4 路轮速 PID）
struct ChassisForceControlPidConfig {
  LibXR::PID<float>::Param force_x{
      .p = 525.0f,
      .i = 0.0f,
      .d = 0.0f,
      .i_limit = 0.0f,
      .out_limit = 200.0f,
  };
  LibXR::PID<float>::Param force_y{
      .p = 625.0f,
      .i = 0.0f,
      .d = 0.0f,
      .i_limit = 0.0f,
      .out_limit = 200.0f,
  };
  LibXR::PID<float>::Param torque_z{
      .p = 80.0f,
      .i = 0.0f,
      .d = 0.0f,
      .i_limit = 0.0f,
      .out_limit = 50.0f,
  };
};

// Stribeck 摩擦补偿配置
struct ChassisFrictionConfig {
  float dynamic_tau_nm = 0.15f;
  float omega_threshold_radps = 2.0f;
  float speed_feedback_gain = 0.05f;
};

// 底盘力控完整配置
struct ChassisForceControlConfig {
  float max_control_force_n = 200.0f;
  float max_control_torque_nm = 50.0f;
  float max_wheel_tau_ref_nm = 3.0f;
  float torque_constant_nm_per_a = 0.3f;
  float velocity_lpf_alpha = 0.3f;
  float torque_feedforward_coeff = 0.5f;
  ChassisForceControlPidConfig pid{};
  ChassisFrictionConfig friction{};
};

inline constexpr ChassisForceControlConfig kChassisForceControlConfig{};

struct ChassisPowerLimiterConfig {
  float stop_scale = 0.0f;
  float low_power_scale = 0.35f;
  float medium_power_scale = 0.60f;
  float high_power_scale = 0.85f;
  float critical_energy_scale = 0.25f;
  float low_energy_scale = 0.45f;
  float medium_energy_scale = 0.65f;
  float high_energy_scale = 0.85f;
  float overshoot_scale = 0.50f;
  float large_delta_scale = 0.80f;
  float full_scale_epsilon = 0.999f;
  float medium_power_limit_w = 40.0f;
  float high_power_limit_w = 60.0f;
  float full_power_limit_w = 80.0f;
  float low_energy_percent = 10.0f;
  float medium_energy_percent = 20.0f;
  float high_energy_percent = 40.0f;
  float full_energy_percent = 70.0f;
  float power_overshoot_margin_w = 5.0f;
  float large_wheel_delta_radps = 60.0f;
  float recovery_step_per_monitor = 0.05f;
  std::uint32_t super_cap_cmd_interval = 1;
  float low_buffer_energy_j = 10.0f;
  float low_buffer_energy_scale = 0.40f;
};

inline constexpr ChassisPowerLimiterConfig kChassisPowerLimiterConfig{};

// ==========================================================================
// 功率控制器配置（HKUST 双环：能量环 + 功率环 + RLS）
// ==========================================================================
struct ChassisPowerControllerConfig {
  // ---- 功率模型初始参数 ----
  float k1_init = 0.22f;     // 转速损耗系数
  float k2_init = 1.2f;      // 力矩平方损耗系数
  float k3 = 2.78f;          // 静态损耗 (W)

  // ---- 能量环 PD 参数 ----
  float energy_kp = 50.0f;   // 能量环比例增益
  float energy_kd = 0.2f;    // 能量环微分增益
  float energy_out_limit = 300.0f;  // 能量环输出上限 (W) = MAX_CAP_POWER_OUT

  // ---- 能量缓冲目标值 ----
  float cap_full_buff = 230.0f;   // 电容满能量目标（原始值 0-255）
  float cap_base_buff = 30.0f;    // 电容基准能量目标
  float referee_full_buff = 60.0f; // 裁判系统满缓冲 (J)
  float referee_base_buff = 50.0f; // 裁判系统基准缓冲 (J)

  // ---- 功率分配参数 ----
  float error_distribution_threshold = 20.0f;  // 误差置信度上限 (rad/s)
  float prop_distribution_threshold = 15.0f;   // 误差置信度下限 (rad/s)

  // ---- RLS 自适应参数 ----
  float rls_delta = 1e-5f;       // RLS 协方差矩阵初始化值
  float rls_lambda = 0.9999f;    // RLS 遗忘因子
  float rls_dead_zone = 5.0f;    // RLS 更新死区 (W)，实测功率低于此值不更新

  // ---- 容错参数 ----
  float cap_max_power_out = 300.0f;               // 电容最大输出功率 (W)
  float cap_offline_energy_runout_power = 43.0f;  // 判定电容能量耗尽的功率阈值 (W)
  float cap_offline_energy_target_power = 37.0f;  // 电容离线时估算充电功率 (W)
  float referee_gg_coe = 0.95f;                   // 仅裁判系统断连时的功率衰减系数
  float cap_referee_both_gg_coe = 0.85f;          // 双断时的功率衰减系数
  float min_max_power_ratio = 0.8f;               // MIN_MAXPOWER = refereeMaxPower * ratio
  float referee_max_buffer_j = 60.0f;             // 裁判系统最大缓冲能量

  // ---- 电机断连容错 ----
  uint16_t motor_disconnect_timeout = 1000;  // 电机断连后仍计算功率的窗口 (ms)
};

inline constexpr ChassisPowerControllerConfig kChassisPowerControllerConfig{};

// motor_number: 1-4 对应 CAN ID 0x201-0x204（组基址 0x200）
inline constexpr DJIMotorGroupConfig kChassisMotorGroupConfig{
    .can_name = "can1",
    .command_frame_id = 0x200,
};

// 力控模式下 DJIMotor 使用 kCurrent 外环，内部速度/角度 PID 不再需要
inline constexpr DJIMotorPidConfig kChassisWheelPid{
    .current = LibXR::PID<float>::Param{},
    .speed = LibXR::PID<float>::Param{},
    .angle = LibXR::PID<float>::Param{},
};

// motor_number 1-4，自动推导 feedback_id 和 command_slot
constexpr DJIMotorConfig MakeChassisWheelMotorConfig(
    const char* name, std::uint8_t motor_number, std::int8_t direction) {
  return DJIMotorConfig{
      .name = name,
      .motor_type = DJIMotorType::kM3508,
      .feedback_id = static_cast<std::uint16_t>(
          kChassisMotorGroupConfig.command_frame_id + motor_number),
      .command_slot = static_cast<std::uint8_t>(motor_number - 1),
      .direction = direction,
      .reduction_ratio = 19.0f,
      .ecd_to_deg = 360.0f / 8192.0f,
      .max_command_current = 16000.0f,
      .feedback_timeout_ms = 100,
      .default_outer_loop = DJIMotorOuterLoop::kCurrent,
      .pid = kChassisWheelPid,
  };
}

inline constexpr std::array<DJIMotorConfig, 4> kChassisWheelMotorConfigs{{
    MakeChassisWheelMotorConfig("chassis_front_left",  1,  1),
    MakeChassisWheelMotorConfig("chassis_front_right", 2, -1),
    MakeChassisWheelMotorConfig("chassis_rear_left",   3,  1),
    MakeChassisWheelMotorConfig("chassis_rear_right",  4, -1),
}};

}  // namespace App::Config
