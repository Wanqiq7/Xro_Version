#pragma once

#include <cstdint>

#include "../Cmd/RefereeConstraintView.hpp"
#include "ChassisState.hpp"
#include "GimbalState.hpp"
#include "MotionCommand.hpp"
#include "RobotMode.hpp"
#include "ShootState.hpp"

namespace App {

// 双板底盘桥命令：显式宽度的当前工程 wire contract。
// 该结构只承接旧双板底盘控制的领域语义，不复用旧 packed ABI。
struct ChassisBoardCommand {
  std::uint8_t mode = 0;
  std::uint8_t flags = 0;
  std::uint16_t chassis_speed_buffer = 0;
  float vx_mps = 0.0f;
  float vy_mps = 0.0f;
  float wz_radps = 0.0f;
  float gimbal_relative_yaw_deg = 0.0f;
};

// 双板底盘桥反馈：承接旧双板底盘反馈的高价值字段。
// 热量、弹速和敌方颜色仍来源于 App 层裁判/发射契约。
struct ChassisBoardFeedback {
  std::uint8_t online_flags = 0;
  std::uint8_t enemy_color = 0;
  std::uint16_t remaining_heat = 0;
  float bullet_speed_mps = 0.0f;
  float vx_mps = 0.0f;
  float vy_mps = 0.0f;
  float wz_radps = 0.0f;
};

inline constexpr std::uint8_t kChassisBoardFlagEnabled = 1U << 0U;
inline constexpr std::uint8_t kChassisBoardFlagEmergencyStop = 1U << 1U;
inline constexpr std::uint8_t kChassisBoardFlagFieldRelative = 1U << 2U;
inline constexpr std::uint8_t kChassisBoardOnlineChassis = 1U << 0U;
inline constexpr std::uint8_t kChassisBoardOnlineShoot = 1U << 1U;
inline constexpr std::uint8_t kChassisBoardOnlineReferee = 1U << 2U;

constexpr std::uint8_t EncodeChassisBoardMode(MotionModeType mode) {
  switch (mode) {
    case MotionModeType::kSpin:
      return 1U;
    case MotionModeType::kIndependent:
      return 2U;
    case MotionModeType::kFollowGimbal:
      return 3U;
    case MotionModeType::kStop:
    default:
      return 0U;
  }
}

constexpr std::uint8_t EncodeChassisBoardFlags(
    const RobotMode& robot_mode, const MotionCommand& motion_command) {
  std::uint8_t flags = 0;
  if (robot_mode.is_enabled) {
    flags |= kChassisBoardFlagEnabled;
  }
  if (robot_mode.is_emergency_stop) {
    flags |= kChassisBoardFlagEmergencyStop;
  }
  if (motion_command.is_field_relative) {
    flags |= kChassisBoardFlagFieldRelative;
  }
  return flags;
}

constexpr std::uint8_t EncodeChassisBoardEnemyColor(
    RefereeCampColor enemy_color) {
  switch (enemy_color) {
    case RefereeCampColor::kRed:
      return 1U;
    case RefereeCampColor::kBlue:
      return 2U;
    case RefereeCampColor::kUnknown:
    default:
      return 0U;
  }
}

constexpr std::uint8_t EncodeChassisBoardOnlineFlags(
    const ChassisState& chassis_state, const ShootState& shoot_state,
    const RefereeConstraintView& referee_constraints) {
  std::uint8_t flags = 0;
  if (chassis_state.online) {
    flags |= kChassisBoardOnlineChassis;
  }
  if (shoot_state.online) {
    flags |= kChassisBoardOnlineShoot;
  }
  if (referee_constraints.referee_online) {
    flags |= kChassisBoardOnlineReferee;
  }
  return flags;
}

constexpr ChassisBoardCommand BuildChassisBoardCommand(
    const RobotMode& robot_mode, const MotionCommand& motion_command,
    const GimbalState& gimbal_state,
    std::uint16_t chassis_speed_buffer = 0U) {
  return ChassisBoardCommand{
      .mode = EncodeChassisBoardMode(motion_command.motion_mode),
      .flags = EncodeChassisBoardFlags(robot_mode, motion_command),
      .chassis_speed_buffer = chassis_speed_buffer,
      .vx_mps = motion_command.vx_mps,
      .vy_mps = motion_command.vy_mps,
      .wz_radps = motion_command.wz_radps,
      .gimbal_relative_yaw_deg = gimbal_state.relative_yaw_deg,
  };
}

constexpr ChassisBoardFeedback BuildChassisBoardFeedback(
    const ChassisState& chassis_state, const ShootState& shoot_state,
    const RefereeConstraintView& referee_constraints) {
  return ChassisBoardFeedback{
      .online_flags = EncodeChassisBoardOnlineFlags(
          chassis_state, shoot_state, referee_constraints),
      .enemy_color =
          EncodeChassisBoardEnemyColor(referee_constraints.enemy_color),
      .remaining_heat = referee_constraints.remaining_heat,
      .bullet_speed_mps = shoot_state.bullet_speed_mps,
      .vx_mps = chassis_state.vx_mps,
      .vy_mps = chassis_state.vy_mps,
      .wz_radps = chassis_state.wz_radps,
  };
}

}  // namespace App
