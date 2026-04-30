#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace App {

// ==========================================================================
// 电机功率模型系数（HKUST 模型）
// P_i = tau_i * omega_i + k1 * |omega_i| + k2 * tau_i^2 + k3 / n
// ==========================================================================
struct ChassisMotorPowerModel {
  static constexpr std::size_t kMotorCount = 4;

  // ---- 模型参数（默认值来自 HKUST Mecanum_and_Omni）----
  static constexpr float kDefaultK1 = 0.22f;   // 转速损耗系数 (W/(rad/s))
  static constexpr float kDefaultK2 = 1.2f;    // 力矩平方损耗系数 (W/(Nm)²)
  static constexpr float kDefaultK3 = 2.78f;   // 静态损耗 (W)
};

// ==========================================================================
// 逐轮功率分配结果
// ==========================================================================
struct ChassisPowerAllocation {
  using Array = std::array<float, ChassisMotorPowerModel::kMotorCount>;

  Array estimated_power_w{};
  Array positive_power_w{};
  Array allocated_power_w{};
  Array allocated_tau_ref_nm{};     // 功率限制后的力矩指令 (Nm)
  Array speed_error_radps{};        // 各轮速度误差
  float total_positive_power_w = 0.0f;
  float active_power_limit_w = 0.0f;
  bool power_limited = false;
};

// ==========================================================================
// 功率预测：基于当前 PID 输出和转速预测总功率
// ==========================================================================
inline float PredictMotorPowerW(float pid_tau_nm, float cur_omega_radps,
                                float k1, float k2, float k3) {
  return pid_tau_nm * cur_omega_radps + k1 * std::fabs(cur_omega_radps) +
         k2 * pid_tau_nm * pid_tau_nm + k3 / static_cast<float>(ChassisMotorPowerModel::kMotorCount);
}

// ==========================================================================
// 二次方程求解最大力矩
// 给定分配的功率 P_alloc，求解 k2*tau² + omega*tau + (k1*|omega| + k3/n - P_alloc) = 0
// 返回使 tau 符号与原始输出一致的实根
// ==========================================================================
inline float SolveMaxTorqueForPower(float allocated_power_w, float cur_omega_radps,
                                    float original_tau_nm, float k1, float k2,
                                    float k3) {
  const float c = k1 * std::fabs(cur_omega_radps) +
                  k3 / static_cast<float>(ChassisMotorPowerModel::kMotorCount) -
                  allocated_power_w;
  const float delta = cur_omega_radps * cur_omega_radps - 4.0f * k2 * c;

  if (delta < 0.0f) {
    // 虚根 → 使用近似解
    return -cur_omega_radps / (2.0f * k2);
  }
  if (std::fabs(delta) < 1e-6f) {
    // 重根
    return -cur_omega_radps / (2.0f * k2);
  }
  // 相异实根 → 选择与原始输出符号相同的根
  const float sqrt_delta = std::sqrt(delta);
  return original_tau_nm >= 0.0f ? (-cur_omega_radps + sqrt_delta) / (2.0f * k2)
                                 : (-cur_omega_radps - sqrt_delta) / (2.0f * k2);
}

}  // namespace App
