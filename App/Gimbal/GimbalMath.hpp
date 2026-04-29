#pragma once

#include <cmath>

namespace App::GimbalMath {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

constexpr float DegreesToRadians(float degrees) { return degrees * kDegToRad; }

constexpr float RadiansToDegrees(float radians) { return radians * kRadToDeg; }

inline float WrapAngleDeg(float angle_deg) {
  while (angle_deg > 180.0f) {
    angle_deg -= 360.0f;
  }
  while (angle_deg < -180.0f) {
    angle_deg += 360.0f;
  }
  return angle_deg;
}

inline float ComputeRelativeYawDeg(float yaw_single_round_deg,
                                   float chassis_align_deg,
                                   float direction = 1.0f) {
  const float sign = direction >= 0.0f ? 1.0f : -1.0f;
  return WrapAngleDeg((yaw_single_round_deg - chassis_align_deg) * sign);
}

}  // namespace App::GimbalMath
