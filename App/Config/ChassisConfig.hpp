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

// motor_number: 1-4 对应 CAN ID 0x201-0x204（组基址 0x200）
inline constexpr DJIMotorGroupConfig kChassisMotorGroupConfig{
    .can_name = "can1",
    .command_frame_id = 0x200,
};

inline constexpr DJIMotorPidConfig kChassisWheelPid{
    .current = LibXR::PID<float>::Param{},
    .speed =
        LibXR::PID<float>::Param{
            .k = 1.0f,
            .p = 12.0f,
            .i = 1.2f,
            .d = 0.0f,
            .i_limit = 3000.0f,
            .out_limit = 16000.0f,
            .cycle = false,
        },
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
      .default_outer_loop = DJIMotorOuterLoop::kSpeed,
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
