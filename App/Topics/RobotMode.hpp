#pragma once

#include <cstdint>

namespace App {

// 机器人主模式。
enum class RobotModeType : uint8_t {
  kSafe = 0,
  kManual = 1,
  kAutoAim = 2,
  kAutoMove = 3,
  kCalibration = 4,
};

// 控制来源。
enum class ControlSource : uint8_t {
  kUnknown = 0,
  kRemote = 1,
  kKeyboardMouse = 2,
  kHost = 3,
};

// 底盘运动模式。
enum class MotionModeType : uint8_t {
  kStop = 0,
  kFollowGimbal = 1,
  kIndependent = 2,
  kSpin = 3,
};

// 瞄准模式。
enum class AimModeType : uint8_t {
  kHold = 0,
  kManual = 1,
  kAutoTrack = 2,
};

// RobotMode Topic：描述上层决策后的主模式与子模式选择。
struct RobotMode {
  RobotModeType robot_mode = RobotModeType::kSafe;
  ControlSource control_source = ControlSource::kUnknown;
  MotionModeType motion_mode = MotionModeType::kStop;
  AimModeType aim_mode = AimModeType::kHold;
  bool fire_enable = false;
  bool is_enabled = false;
  bool is_emergency_stop = false;
};

}  // namespace App
