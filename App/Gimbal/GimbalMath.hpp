#pragma once

#include <cmath>

#include "libxr_def.hpp"

namespace App::GimbalMath {

inline constexpr float kDegToRad = static_cast<float>(LibXR::PI / 180.0);
inline constexpr float kRadToDeg = static_cast<float>(180.0 / LibXR::PI);

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
