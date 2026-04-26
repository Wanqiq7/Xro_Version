#pragma once

#include <cstdint>

namespace App {

// 供弹模式。
enum class LoaderModeType : uint8_t {
  kStop = 0,
  kSingle = 1,
  kBurst = 2,
  kContinuous = 3,
  kReverse = 4,
};

// FireCommand Topic：描述射击链路闭环消费所需的最小开火意图。
// 新字段均提供默认值，以兼容当前仍只写入旧字段的生产者。
struct FireCommand {
  bool fire_enable = false;
  bool friction_enable = false;
  LoaderModeType loader_mode = LoaderModeType::kStop;
  float target_bullet_speed_mps = 0.0f;
  float shoot_rate_hz = 0.0f;
  bool referee_allows_fire = false;
  std::uint16_t shooter_heat_limit = 0;
  std::uint16_t remaining_heat = 0;
  uint8_t burst_count = 0;
};

}  // namespace App
