#pragma once

#include "../../Modules/DJIMotor/DJIMotor.hpp"
#include "../../Modules/DMMotor/DMMotor.hpp"

namespace App::Config {

// yaw 电机单圈角度与底盘正方向对齐时的角度，需上板按机械零位标定。
inline constexpr float kYawChassisAlignDeg = 0.0f;
inline constexpr float kYawRelativeDirection = 1.0f;
// pitch 轴先给出保守 bring-up 保护限位，后续按机构实测重新标定。
inline constexpr float kPitchCommandMinDeg = -25.0f;
inline constexpr float kPitchCommandMaxDeg = 25.0f;

// GM6020 使用 0x1FF 组，motor_number 5-8 对应 CAN ID 0x205-0x208
inline constexpr DJIMotorGroupConfig kGimbalMotorGroupConfig{
    .can_name = "can1",
    .command_frame_id = 0x1FF,
};

inline constexpr DJIMotorPidConfig kYawMotorPid{
    .current = LibXR::PID<float>::Param{},
    .speed =
        LibXR::PID<float>::Param{
            .k = 1.0f,
            .p = 3200.0f,
            .i = 250.0f,
            .d = 20.0f,
            .i_limit = 8.0f,
            .out_limit = 20000.0f,
            .cycle = false,
        },
    .angle =
        LibXR::PID<float>::Param{
            .k = 1.0f,
            .p = 8.0f,
            .i = 0.0f,
            .d = 0.3f,
            .i_limit = 0.0f,
            .out_limit = 720.0f,
            .cycle = true,
        },
};

// GM6020 motor_number: 5-8，feedback_id = 0x200 + motor_number，command_slot = motor_number - 5
constexpr DJIMotorConfig MakeGimbalMotorConfig(
    const char* name, std::uint8_t motor_number, std::int8_t direction,
    const DJIMotorPidConfig& pid) {
  return DJIMotorConfig{
      .name = name,
      .motor_type = DJIMotorType::kGM6020,
      .feedback_id = static_cast<std::uint16_t>(0x200 + motor_number),
      .command_slot = static_cast<std::uint8_t>(motor_number - 5),
      .direction = direction,
      .reduction_ratio = 1.0f,
      .ecd_to_deg = 360.0f / 8192.0f,
      .max_command_current = 20000.0f,
      .feedback_timeout_ms = 100,
      .default_outer_loop = DJIMotorOuterLoop::kAngle,
      .pid = pid,
  };
}

inline constexpr DJIMotorConfig kYawMotorConfig =
    MakeGimbalMotorConfig("yaw_motor", 5, 1, kYawMotorPid);

inline constexpr DMMotorConfig kPitchDMMotorConfig{
    .name = "pitch_motor",
    .can_name = "can1",
    .tx_id = 0x006,
    .feedback_id = 0x016,
    .direction = 1,
    .feedback_timeout_ms = 100,
    .default_outer_loop = DMMotorOuterLoop::kAngle,
    .mit_limit =
        {
            .angle_min = -12.5f,
            .angle_max = 12.5f,
            .velocity_min = -45.0f,
            .velocity_max = 45.0f,
            .torque_min = -18.0f,
            .torque_max = 18.0f,
            .kp_min = 0.0f,
            .kp_max = 500.0f,
            .kd_min = 0.0f,
            .kd_max = 5.0f,
        },
    .pid =
        {
            .speed =
                LibXR::PID<float>::Param{
                    .k = 1.0f,
                    .p = 1.15f,
                    .i = 0.0f,
                    .d = 0.0f,
                    .i_limit = 0.0f,
                    .out_limit = 3.0f,
                    .cycle = false,
                },
            .angle =
                LibXR::PID<float>::Param{
                    .k = 1.0f,
                    .p = 20.0f,
                    .i = 0.0f,
                    .d = 0.0f,
                    .i_limit = 0.0f,
                    .out_limit = 12.0f,
                    .cycle = false,
                },
        },
};

}  // namespace App::Config
