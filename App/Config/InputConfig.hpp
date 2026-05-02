#pragma once

namespace App {

struct InputConfig {
  static constexpr float kStickMaxMagnitude = 660.0f;
  static constexpr float kMaxYawRateDegps = 660.0f;
  static constexpr float kMaxPitchRateDegps = 132.0f;
  static constexpr float kMaxChassisLinearSpeedMps = 2.0f;
  static constexpr float kDefaultBulletSpeedMps = 30.0f;
  static constexpr float kDefaultShootRateHz = 8.0f;
  static constexpr float kDialFrictionThreshold = -100.0f;
  static constexpr float kDialContinuousFireThreshold = -500.0f;
  static constexpr float kDialEmergencyThreshold = 300.0f;
};

}  // namespace App
