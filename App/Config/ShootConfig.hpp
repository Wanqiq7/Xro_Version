#pragma once

#include <cstdint>

#include "../../Modules/DJIMotor/DJIMotor.hpp"

namespace App::Config {

struct ShootFrictionConfig {
  const char* can_name = "can2";
  std::uint8_t left_motor_id = 1;
  std::uint8_t right_motor_id = 2;
  std::uint32_t command_frame_id = 0x200;
  float bullet_speed_to_wheel_rpm_scale = 1000.0f;
  std::uint32_t monitor_timeout_ms = 100;
};

struct ShootLoaderConfig {
  const char* can_name = "can2";
  std::uint8_t motor_id = 3;
  std::uint32_t command_frame_id = 0x200;
  float continuous_speed_rpm = 3000.0f;
  float reverse_speed_rpm = -2000.0f;
  float single_step_speed_rpm = 1800.0f;
  float single_step_angle_deg = 36.0f;
  std::uint32_t monitor_timeout_ms = 100;
};

inline constexpr ShootFrictionConfig kShootFrictionConfig{};
inline constexpr ShootLoaderConfig kShootLoaderConfig{};

// motor_number: 1-4 对应 CAN ID 0x201-0x204（组基址 0x200）
inline constexpr DJIMotorGroupConfig kShootMotorGroupConfig{
    .can_name = "can2",
    .command_frame_id = 0x200,
};

inline constexpr DJIMotorPidConfig kShootFrictionPid{
    .current = LibXR::PID<float>::Param{},
    .speed =
        LibXR::PID<float>::Param{
            .k = 1.0f,
            .p = 10.0f,
            .i = 0.8f,
            .d = 0.0f,
            .i_limit = 2500.0f,
            .out_limit = 16000.0f,
            .cycle = false,
        },
    .angle = LibXR::PID<float>::Param{},
};

inline constexpr DJIMotorPidConfig kShootLoaderPid{
    .current = LibXR::PID<float>::Param{},
    .speed =
        LibXR::PID<float>::Param{
            .k = 1.0f,
            .p = 10.0f,
            .i = 0.8f,
            .d = 0.0f,
            .i_limit = 2500.0f,
            .out_limit = 16000.0f,
            .cycle = false,
        },
    .angle =
        LibXR::PID<float>::Param{
            .k = 1.0f,
            .p = 3.0f,
            .i = 0.0f,
            .d = 0.0f,
            .i_limit = 0.0f,
            .out_limit = 7200.0f,
            .cycle = false,
        },
};

constexpr DJIMotorConfig MakeShootMotorConfig(
    const char* name, std::uint8_t motor_number, std::int8_t direction,
    DJIMotorOuterLoop outer_loop, const DJIMotorPidConfig& pid) {
  return DJIMotorConfig{
      .name = name,
      .motor_type = DJIMotorType::kM3508,
      .feedback_id = static_cast<std::uint16_t>(
          kShootMotorGroupConfig.command_frame_id + motor_number),
      .command_slot = static_cast<std::uint8_t>(motor_number - 1),
      .direction = direction,
      .reduction_ratio = 1.0f,
      .ecd_to_deg = 360.0f / 8192.0f,
      .max_command_current = 16000.0f,
      .feedback_timeout_ms = 100,
      .default_outer_loop = outer_loop,
      .pid = pid,
  };
}

inline constexpr DJIMotorConfig kShootLeftFrictionMotorConfig =
    MakeShootMotorConfig("shoot_friction_left", 1, 1,
                         DJIMotorOuterLoop::kSpeed, kShootFrictionPid);

inline constexpr DJIMotorConfig kShootRightFrictionMotorConfig =
    MakeShootMotorConfig("shoot_friction_right", 2, -1,
                         DJIMotorOuterLoop::kSpeed, kShootFrictionPid);

inline constexpr DJIMotorConfig kShootLoaderMotorConfig =
    MakeShootMotorConfig("shoot_loader", 3, 1,
                         DJIMotorOuterLoop::kSpeed, kShootLoaderPid);

}  // namespace App::Config
