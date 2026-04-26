#pragma once

#include <algorithm>
#include <cstdint>

#include "../../Modules/Referee/Referee.hpp"

namespace App {

struct RefereeConstraintView {
  // bring-up 语义：裁判离线不阻断底盘低层运动验证，但不放开发射许可。
  bool referee_online = false;
  bool match_gate_active = false;
  bool fire_gate_active = false;
  bool power_gate_active = false;
  bool power_limited = false;
  bool referee_allows_fire = false;
  float chassis_power_limit_w = 0.0f;
  float motion_scale = 1.0f;
  float max_fire_rate_hz = 0.0f;
  std::uint16_t shooter_heat_limit = 0;
  std::uint16_t remaining_heat = 0;
};

inline constexpr float kStopMotionScale = 0.0f;
inline constexpr float kCriticalMotionScale = 0.25f;
inline constexpr float kLowMotionScale = 0.4f;
inline constexpr float kMediumMotionScale = 0.6f;
inline constexpr float kHighMotionScale = 0.8f;
inline constexpr float kPowerLimitedFireRateHz = 1.0f;
inline constexpr float kMediumHeatFireRateHz = 2.0f;
inline constexpr float kUnlimitedFireRateHz = 1000.0f;

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

  if (referee_state.shooter_cooling_value == 0U) {
    return kPowerLimitedFireRateHz;
  }

  if (referee_state.remaining_heat <= referee_state.shooter_cooling_value) {
    return kPowerLimitedFireRateHz;
  }
  if (referee_state.remaining_heat <=
      static_cast<std::uint16_t>(referee_state.shooter_cooling_value * 2U)) {
    return kMediumHeatFireRateHz;
  }
  return kUnlimitedFireRateHz;
}

constexpr RefereeConstraintView BuildRefereeConstraintView(
    const RefereeState& referee_state) {
  if (!referee_state.online) {
    return RefereeConstraintView{};
  }

  const float motion_scale =
      std::min(ResolvePowerLimitMotionScale(referee_state.chassis_power_limit_w),
               std::min(ResolveBufferMotionScale(referee_state.buffer_energy),
                        ResolveEnergyMotionScale(
                            referee_state.remaining_energy_bits)));
  const float max_fire_rate_hz = ResolveMaxFireRateHz(referee_state);

  return RefereeConstraintView{
      .referee_online = true,
      .match_gate_active = !referee_state.match_started,
      .fire_gate_active = max_fire_rate_hz < kUnlimitedFireRateHz,
      .power_gate_active = motion_scale < 1.0f,
      .power_limited = true,
      .referee_allows_fire = max_fire_rate_hz > 0.0f,
      .chassis_power_limit_w = referee_state.chassis_power_limit_w,
      .motion_scale = motion_scale,
      .max_fire_rate_hz = max_fire_rate_hz,
      .shooter_heat_limit = referee_state.shooter_heat_limit,
      .remaining_heat = referee_state.remaining_heat,
  };
}

}  // namespace App
