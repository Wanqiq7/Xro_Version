#pragma once

#include <algorithm>
#include <cstdint>

#include "../../Modules/Referee/Referee.hpp"

namespace App {

enum class RefereeCampColor : std::uint8_t {
  kUnknown = 0,
  kRed = 1,
  kBlue = 2,
};

struct RefereeConstraintView {
  // bring-up 语义：裁判离线不阻断底盘低层运动验证，但不放开发射许可。
  bool referee_online = false;
  bool match_gate_active = false;
  bool fire_gate_active = false;
  bool power_gate_active = false;
  bool power_limited = false;
  bool referee_allows_fire = false;
  bool robot_id_valid = false;
  bool robot_alive = false;
  bool hp_valid = false;
  bool shooter_heat_valid = false;
  bool buffer_energy_valid = false;
  bool self_color_known = false;
  float chassis_power_w = 0.0f;
  float chassis_power_limit_w = 0.0f;
  float motion_scale = 1.0f;
  float max_fire_rate_hz = 0.0f;
  float shooter_heat_ratio = 0.0f;
  float remaining_heat_ratio = 0.0f;
  std::uint16_t shooter_heat_limit = 0;
  std::uint16_t shooter_heat = 0;
  std::uint16_t remaining_heat = 0;
  std::uint16_t shooter_42mm_heat = 0;
  std::uint16_t shooter_42mm_heat_limit = 0;
  std::uint16_t remaining_42mm_heat = 0;
  bool shooter_42mm_heat_valid = false;
  float shooter_42mm_heat_ratio = 0.0f;
  float remaining_42mm_heat_ratio = 0.0f;
  std::uint16_t robot_hp = 0;
  std::uint16_t buffer_energy = 0;
  std::uint16_t cooling_rate = 0;
  std::uint8_t robot_id = 0;
  std::uint8_t robot_level = 0;
  std::uint8_t hurt_armor_id = 0;
  std::uint8_t hurt_type = 0;
  std::uint8_t remaining_energy_bits = 0;
  std::uint8_t recovery_buff = 0;
  std::uint8_t defence_buff = 0;
  std::uint8_t vulnerability_buff = 0;
  std::uint16_t attack_buff = 0;
  RefereeCampColor self_color = RefereeCampColor::kUnknown;
  RefereeCampColor enemy_color = RefereeCampColor::kUnknown;
};

inline constexpr float kStopMotionScale = 0.0f;
inline constexpr float kCriticalMotionScale = 0.25f;
inline constexpr float kLowMotionScale = 0.4f;
inline constexpr float kMediumMotionScale = 0.6f;
inline constexpr float kHighMotionScale = 0.8f;
inline constexpr float kPowerLimitedFireRateHz = 1.0f;
inline constexpr float kMediumHeatFireRateHz = 2.0f;
inline constexpr float kUnlimitedFireRateHz = 1000.0f;
inline constexpr std::uint8_t kRedRobotIdMin = 1U;
inline constexpr std::uint8_t kRedRobotIdMax = 99U;
inline constexpr std::uint8_t kBlueRobotIdMin = 100U;

constexpr float ClampRatio(std::uint16_t value, std::uint16_t limit) {
  if (limit == 0U) {
    return 0.0f;
  }
  const float ratio = static_cast<float>(value) / static_cast<float>(limit);
  return std::clamp(ratio, 0.0f, 1.0f);
}

constexpr RefereeCampColor ResolveSelfCampColor(std::uint8_t robot_id) {
  if (robot_id >= kBlueRobotIdMin) {
    return RefereeCampColor::kBlue;
  }
  if (robot_id >= kRedRobotIdMin && robot_id <= kRedRobotIdMax) {
    return RefereeCampColor::kRed;
  }
  return RefereeCampColor::kUnknown;
}

constexpr RefereeCampColor ResolveEnemyCampColor(RefereeCampColor self_color) {
  switch (self_color) {
    case RefereeCampColor::kRed:
      return RefereeCampColor::kBlue;
    case RefereeCampColor::kBlue:
      return RefereeCampColor::kRed;
    case RefereeCampColor::kUnknown:
    default:
      return RefereeCampColor::kUnknown;
  }
}

constexpr float ResolveEnergyMotionScale(std::uint8_t remaining_energy_bits) {
  if ((remaining_energy_bits & (1U << 1U)) != 0U ||
      (remaining_energy_bits & (1U << 0U)) != 0U) {
    return 1.0f;
  }
  if ((remaining_energy_bits & (1U << 2U)) != 0U) {
    return kHighMotionScale;
  }
  if ((remaining_energy_bits & (1U << 3U)) != 0U) {
    return kMediumMotionScale;
  }
  if ((remaining_energy_bits & (1U << 4U)) != 0U) {
    return kLowMotionScale;
  }
  if ((remaining_energy_bits & (1U << 5U)) != 0U ||
      (remaining_energy_bits & (1U << 6U)) != 0U) {
    return kCriticalMotionScale;
  }
  return kStopMotionScale;
}

constexpr float ResolveBufferMotionScale(std::uint16_t buffer_energy) {
  if (buffer_energy >= 40U) {
    return 1.0f;
  }
  if (buffer_energy >= 20U) {
    return kMediumMotionScale;
  }
  if (buffer_energy >= 10U) {
    return kLowMotionScale;
  }
  if (buffer_energy > 0U) {
    return kCriticalMotionScale;
  }
  return kStopMotionScale;
}

constexpr float ResolvePowerLimitMotionScale(float chassis_power_limit_w) {
  if (chassis_power_limit_w >= 80.0f) {
    return 1.0f;
  }
  if (chassis_power_limit_w >= 60.0f) {
    return kHighMotionScale;
  }
  if (chassis_power_limit_w >= 40.0f) {
    return kMediumMotionScale;
  }
  if (chassis_power_limit_w > 0.0f) {
    return kCriticalMotionScale;
  }
  return kStopMotionScale;
}

constexpr float ResolveMaxFireRateHz(const RefereeState& referee_state) {
  if (!referee_state.online || !referee_state.match_started ||
      !referee_state.fire_allowed || referee_state.remaining_heat == 0U) {
    return 0.0f;
  }

  const std::uint16_t cooling_rate =
      referee_state.cooling_rate > 0U ? referee_state.cooling_rate
                                      : referee_state.shooter_cooling_value;
  if (cooling_rate == 0U) {
    return kPowerLimitedFireRateHz;
  }

  if (referee_state.remaining_heat <= cooling_rate) {
    return kPowerLimitedFireRateHz;
  }
  if (referee_state.remaining_heat <=
      static_cast<std::uint16_t>(cooling_rate * 2U)) {
    return kMediumHeatFireRateHz;
  }
  // TODO: 若 robot 类型允许使用 42mm，应同时检查 referee_state.shooter_42mm_heat_limit
  // 和 remaining_42mm_heat 的热量约束，使用 std::min 取 17mm 与 42mm 发射速率的较小值。
  return kUnlimitedFireRateHz;
}

constexpr RefereeConstraintView BuildRefereeConstraintView(
    const RefereeState& referee_state) {
  if (!referee_state.online) {
    return RefereeConstraintView{};
  }

  const RefereeCampColor self_color =
      ResolveSelfCampColor(referee_state.robot_id);
  const float motion_scale =
      std::min(ResolvePowerLimitMotionScale(referee_state.chassis_power_limit_w),
               std::min(ResolveBufferMotionScale(referee_state.buffer_energy),
                        ResolveEnergyMotionScale(
                            referee_state.remaining_energy_bits)));
  const float max_fire_rate_hz = ResolveMaxFireRateHz(referee_state);
  const std::uint16_t cooling_rate =
      referee_state.cooling_rate > 0U ? referee_state.cooling_rate
                                      : referee_state.shooter_cooling_value;
  const bool shooter_heat_valid = referee_state.shooter_heat_limit > 0U;
  const bool hp_valid = referee_state.robot_id != 0U;
  const bool buffer_energy_valid = true;
  const bool power_gate_active = motion_scale < 1.0f;

  return RefereeConstraintView{
      .referee_online = true,
      .match_gate_active = !referee_state.match_started,
      .fire_gate_active = max_fire_rate_hz < kUnlimitedFireRateHz,
      .power_gate_active = power_gate_active,
      .power_limited = true,
      .referee_allows_fire = max_fire_rate_hz > 0.0f,
      .robot_id_valid = referee_state.robot_id != 0U,
      .robot_alive = referee_state.robot_hp > 0U,
      .hp_valid = hp_valid,
      .shooter_heat_valid = shooter_heat_valid,
      .buffer_energy_valid = buffer_energy_valid,
      .self_color_known = self_color != RefereeCampColor::kUnknown,
      .chassis_power_w = referee_state.chassis_power_w,
      .chassis_power_limit_w = referee_state.chassis_power_limit_w,
      .motion_scale = motion_scale,
      .max_fire_rate_hz = max_fire_rate_hz,
      .shooter_heat_ratio =
          ClampRatio(referee_state.shooter_heat, referee_state.shooter_heat_limit),
      .remaining_heat_ratio = ClampRatio(referee_state.remaining_heat,
                                         referee_state.shooter_heat_limit),
      .shooter_heat_limit = referee_state.shooter_heat_limit,
      .shooter_heat = referee_state.shooter_heat,
      .remaining_heat = referee_state.remaining_heat,
      .shooter_42mm_heat = referee_state.shooter_42mm_heat,
      .shooter_42mm_heat_limit = referee_state.shooter_42mm_heat_limit,
      .remaining_42mm_heat = referee_state.remaining_42mm_heat,
      .shooter_42mm_heat_valid = referee_state.shooter_42mm_heat_limit > 0U,
      .shooter_42mm_heat_ratio =
          ClampRatio(referee_state.shooter_42mm_heat, referee_state.shooter_42mm_heat_limit),
      .remaining_42mm_heat_ratio = ClampRatio(referee_state.remaining_42mm_heat,
                                              referee_state.shooter_42mm_heat_limit),
      .robot_hp = referee_state.robot_hp,
      .buffer_energy = referee_state.buffer_energy,
      .cooling_rate = cooling_rate,
      .robot_id = referee_state.robot_id,
      .robot_level = referee_state.robot_level,
      .hurt_armor_id = referee_state.hurt_armor_id,
      .hurt_type = referee_state.hurt_type,
      .remaining_energy_bits = referee_state.remaining_energy_bits,
      .recovery_buff = referee_state.recovery_buff,
      .defence_buff = referee_state.defence_buff,
      .vulnerability_buff = referee_state.vulnerability_buff,
      .attack_buff = referee_state.attack_buff,
      .self_color = self_color,
      .enemy_color = ResolveEnemyCampColor(self_color),
  };
}

}  // namespace App
