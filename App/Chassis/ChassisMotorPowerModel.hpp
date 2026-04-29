#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace App {

struct ChassisMotorPowerModel {
  static constexpr std::size_t kMotorCount = 4;

  // 这些系数来自 donor PowerControl，用于估算 M3508 底盘电机机械功率。
  static constexpr float kTorqueCoef = 0.0003662109375f;
  static constexpr float kPowerCoef = 187.0f / 3591.0f / 9.55f;
  static constexpr std::array<float, kMotorCount> kK1{
      1.23e-07f, 1.23e-07f, 1.23e-07f, 1.23e-07f};
  static constexpr std::array<float, kMotorCount> kK2{
      1.453e-07f, 1.453e-07f, 1.453e-07f, 1.453e-07f};
  static constexpr std::array<float, kMotorCount> kConstant{
      4.081f, 4.081f, 4.081f, 4.081f};
};

struct ChassisPowerAllocation {
  using Array = std::array<float, ChassisMotorPowerModel::kMotorCount>;

  Array estimated_power_w{0.0f, 0.0f, 0.0f, 0.0f};
  Array positive_power_w{0.0f, 0.0f, 0.0f, 0.0f};
  Array allocated_power_w{0.0f, 0.0f, 0.0f, 0.0f};
  Array allocated_current_cmd_raw{0.0f, 0.0f, 0.0f, 0.0f};
  float total_positive_power_w = 0.0f;
  float active_power_limit_w = 0.0f;
  bool power_limited = false;
};

inline float SanitizeFinite(float value, float fallback = 0.0f) {
  return std::isfinite(value) ? value : fallback;
}

inline float EstimateMotorPowerW(std::size_t motor_index,
                                 float current_cmd_raw, float speed_rpm) {
  const std::size_t index =
      std::min(motor_index, ChassisMotorPowerModel::kMotorCount - 1U);
  current_cmd_raw = SanitizeFinite(current_cmd_raw);
  speed_rpm = SanitizeFinite(speed_rpm);

  return ChassisMotorPowerModel::kK1[index] * current_cmd_raw *
             current_cmd_raw +
         ChassisMotorPowerModel::kK2[index] * speed_rpm * speed_rpm +
         ChassisMotorPowerModel::kPowerCoef * speed_rpm * current_cmd_raw *
             ChassisMotorPowerModel::kTorqueCoef +
         ChassisMotorPowerModel::kConstant[index];
}

inline float SolveCurrentCommandForPower(std::size_t motor_index,
                                         float speed_rpm,
                                         float allocated_power_w,
                                         float original_current_cmd_raw) {
  const std::size_t index =
      std::min(motor_index, ChassisMotorPowerModel::kMotorCount - 1U);
  speed_rpm = SanitizeFinite(speed_rpm);
  allocated_power_w = std::max(0.0f, SanitizeFinite(allocated_power_w));
  original_current_cmd_raw = SanitizeFinite(original_current_cmd_raw);

  const float a = ChassisMotorPowerModel::kK1[index];
  const float b = ChassisMotorPowerModel::kTorqueCoef *
                  ChassisMotorPowerModel::kPowerCoef * speed_rpm;
  const float c = ChassisMotorPowerModel::kK2[index] * speed_rpm * speed_rpm -
                  allocated_power_w + ChassisMotorPowerModel::kConstant[index];
  if (a <= 0.0f) {
    return original_current_cmd_raw;
  }

  float discriminant = b * b - 4.0f * a * c;
  if (!std::isfinite(discriminant) || discriminant < 0.0f) {
    discriminant = 0.0f;
  }

  const float sqrt_discriminant = std::sqrt(discriminant);
  const float numerator = original_current_cmd_raw >= 0.0f
                              ? (-b + sqrt_discriminant)
                              : (-b - sqrt_discriminant);
  const float solved = numerator / (2.0f * a);
  return SanitizeFinite(solved, original_current_cmd_raw);
}

inline ChassisPowerAllocation AllocateChassisMotorPower(
    const ChassisPowerAllocation::Array& current_cmd_raw,
    const ChassisPowerAllocation::Array& speed_rpm, float power_limit_w) {
  ChassisPowerAllocation allocation{};
  allocation.active_power_limit_w = std::max(0.0f, SanitizeFinite(power_limit_w));

  for (std::size_t index = 0; index < ChassisMotorPowerModel::kMotorCount;
       ++index) {
    allocation.estimated_power_w[index] =
        EstimateMotorPowerW(index, current_cmd_raw[index], speed_rpm[index]);
    const float positive_power =
        std::max(0.0f, SanitizeFinite(allocation.estimated_power_w[index]));
    allocation.positive_power_w[index] = positive_power;
    allocation.allocated_power_w[index] = positive_power;
    allocation.allocated_current_cmd_raw[index] =
        SanitizeFinite(current_cmd_raw[index]);
    allocation.total_positive_power_w += positive_power;
  }

  if (allocation.active_power_limit_w <= 0.0f ||
      allocation.total_positive_power_w <= allocation.active_power_limit_w) {
    return allocation;
  }

  allocation.power_limited = true;
  const float ratio =
      allocation.active_power_limit_w / allocation.total_positive_power_w;
  for (std::size_t index = 0; index < ChassisMotorPowerModel::kMotorCount;
       ++index) {
    allocation.allocated_power_w[index] =
        allocation.positive_power_w[index] * ratio;
    allocation.allocated_current_cmd_raw[index] = SolveCurrentCommandForPower(
        index, speed_rpm[index], allocation.allocated_power_w[index],
        current_cmd_raw[index]);
  }

  return allocation;
}

}  // namespace App
