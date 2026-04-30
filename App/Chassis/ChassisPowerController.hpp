#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "ChassisMotorPowerModel.hpp"
#include "../Config/ChassisConfig.hpp"
#include "../Cmd/RefereeConstraintView.hpp"
#include "../Topics/MotionCommand.hpp"
#include "../../Modules/SuperCap/SuperCap.hpp"
#include "../../Modules/RLS/RLS.hpp"

namespace App {

// ==========================================================================
// 底盘功率等级查询表（硬编码，不放入 Config）
// ==========================================================================
namespace {
  constexpr uint8_t kMaxRobotLevel = 10;

  // Hero 机器人底盘功率限制（按等级，单位 W）
  constexpr uint8_t kHeroPowerLimit[kMaxRobotLevel] = {
      55, 60, 65, 70, 75, 80, 85, 90, 100, 120};

  // Infantry 机器人底盘功率限制（按等级，单位 W）
  constexpr uint8_t kInfantryPowerLimit[kMaxRobotLevel] = {
      45, 50, 55, 60, 65, 70, 75, 80, 90, 100};

  // Sentry 底盘功率限制 (W)
  constexpr uint8_t kSentryPowerLimit = 100U;

  // 电容最大能量 (J)
  constexpr float kCapEnergyMaxJ = 2100.0f;

  // 裁判系统基础功率下限 (W)
  constexpr float kRefereeMinPowerW = 43.0f;

  // sqrt(60) - sqrt(50)，供裁判在线 + 电容离线时的能量环余量计算
  constexpr float kSqrtBufferDelta = 0.6747f;  // sqrt(60) - sqrt(50)
}  // namespace

/// @brief 底盘功率控制器：HKUST 能量环 + 功率环 + RLS 自适应 + 容错降级
///
/// 控制链路：
///   1. UpdateErrorFlags   — 错误标志位更新（电机/裁判/电容断连检测）
///   2. UpdateRefereeLimits — 解析裁判功率限制与能量上限
///   3. UpdateEnergyState  — 能量环 PD 控制（电容优先，裁判缓冲 fallback）
///   4. RunRlsUpdate       — RLS 在线估计功率损耗系数
///   5. RunPowerLoop       — 预测功率 → 权重分配 → 求解力矩上限
class ChassisPowerController {
 public:
  using WheelArray = std::array<float, ChassisMotorPowerModel::kMotorCount>;
  static constexpr std::size_t kMotorCount = ChassisMotorPowerModel::kMotorCount;

  // ---- 错误标志位（与 HKUST 一致）----
  enum ErrorFlag : uint8_t {
    kMotorDisconnect = 1U << 0,
    kRefereeDisconnect = 1U << 1,
    kCapDisconnect = 1U << 2,
  };

  // ---- 功率环输入（每轮数据）----
  struct WheelObj {
    float pid_tau_nm = 0.0f;         // PID 输出的力矩指令 (Nm)
    float cur_omega_radps = 0.0f;    // 当前轮角速度 (rad/s)
    float set_omega_radps = 0.0f;    // 目标轮角速度 (rad/s)
    float max_tau_nm = 3.0f;         // 最大允许力矩 (Nm)
    bool online = true;
  };

  // ---- 能量环输入 ----
  struct EnergyInput {
    // 裁判系统
    bool referee_online = false;
    float chassis_power_limit_w = 0.0f;
    float chassis_power_w = 0.0f;
    float buffer_energy = 0.0f;
    uint8_t robot_level = 1;

    // 超级电容
    bool cap_online = false;
    bool cap_output_enabled = true;
    float cap_energy_raw = 0.0f;     // 原始值 0-255
    float cap_chassis_power_w = 0.0f;
    uint16_t cap_power_limit_w = 0;
  };

  // ---- 功率环输出 ----
  struct Output {
    WheelArray limited_tau_nm{};
    ChassisPowerAllocation power_allocation{};
    float active_power_limit_w = 0.0f;
    float base_max_power_w = 0.0f;
    float full_max_power_w = 0.0f;
    float power_upper_limit_w = 0.0f;
    float measured_power_w = 0.0f;
    float estimated_power_w = 0.0f;
    float estimated_cap_energy_raw = 0.0f;  // 0-255
    float k1 = 0.22f;
    float k2 = 1.2f;
    uint8_t error_flags = 0;
    bool power_limited = false;
    bool energy_loop_active = false;
  };

  explicit ChassisPowerController(
      Config::ChassisPowerControllerConfig config =
          Config::kChassisPowerControllerConfig)
      : config_(config),
        rls_(Module::RLS::RlsEstimator::Config{config.rls_delta,
                                               config.rls_lambda}) {
    k1_ = config_.k1_init;
    k2_ = config_.k2_init;
    k3_ = config_.k3;
  }

  // ---- 每个控制周期调用一次（主入口）----
  // energy_input: 裁判+电容数据
  // wheels: 各轮的 PID 输出和速度
  // dt_s: 本周期时间步长
  Output Update(const EnergyInput& energy_input,
                const std::array<WheelObj, kMotorCount>& wheels,
                float dt_s) {
    Output output{};

    // 1. 错误标志更新
    UpdateErrorFlags(energy_input, wheels);
    output.error_flags = error_flags_;

    // 2. 裁判系统功率限制（必须先执行，后继能量环依赖 referee_max_power_）
    UpdateRefereeLimits(energy_input);

    // 3. 能量环 PD + 电容离线估算
    UpdateEnergyState(energy_input, dt_s);

    // 4. RLS 自适应参数更新
    RunRlsUpdate(energy_input, wheels, dt_s);

    // 5. 确定当前有效功率上限
    float effective_limit = base_max_power_;
    if (user_configured_max_power_ > 0.0f) {
      // 用户手动设定了上限，钳位在 [base, full] 之间
      effective_limit =
          std::fmax(base_max_power_,
                    std::fmin(full_max_power_, user_configured_max_power_));
    }
    effective_limit = std::fmin(effective_limit, power_upper_limit_);
    effective_limit = std::fmax(effective_limit, min_max_power_configured_);

    output.active_power_limit_w = effective_limit;
    output.base_max_power_w = base_max_power_;
    output.full_max_power_w = full_max_power_;
    output.power_upper_limit_w = power_upper_limit_;
    output.k1 = k1_;
    output.k2 = k2_;
    output.energy_loop_active = energy_loop_active_;
    output.estimated_cap_energy_raw = estimated_cap_energy_raw_;
    output.estimated_power_w = estimated_cap_power_w_;

    // 6. 获取实际测量功率（优先电容数据，裁判数据为 fallback）
    output.measured_power_w =
        energy_input.cap_online
            ? energy_input.cap_chassis_power_w
            : energy_input.chassis_power_w;

    // 7. 功率环核心：预测 -> 分配 -> 求解力矩上限
    RunPowerLoop(wheels, effective_limit, dt_s, output);

    // 8. 非有限值保护
    for (auto& v : output.limited_tau_nm) {
      if (!std::isfinite(v)) v = 0.0f;
    }

    return output;
  }

  // ---- 设置用户配置的最大功率（钳位于 [baseMaxPower, fullMaxPower]）----
  void SetMaxPowerConfigured(float max_power_w) {
    user_configured_max_power_ = std::fmax(0.0f, max_power_w);
  }

  // 0=battery, 1=cap
  void SetMode(uint8_t mode) { mode_ = mode; }

  void SetRlsEnabled(bool enabled) { rls_enabled_ = enabled; }

  // ---- 获取内部状态（调试/遥测）----
  float GetK1() const { return k1_; }
  float GetK2() const { return k2_; }
  uint8_t GetErrorFlags() const { return error_flags_; }

 private:
  // ========================================================================
  // 错误标志更新
  // ========================================================================
  void UpdateErrorFlags(
      const EnergyInput& input,
      const std::array<WheelObj, kMotorCount>& wheels) {
    error_flags_ = 0;

    if (!input.referee_online) {
      error_flags_ |= kRefereeDisconnect;
    }
    if (!input.cap_online) {
      error_flags_ |= kCapDisconnect;
    }

    // 电机断连检测：累计离线周期，超时后置位
    for (std::size_t i = 0; i < kMotorCount; ++i) {
      if (!wheels[i].online) {
        if (motor_disconnect_counters_[i] < config_.motor_disconnect_timeout) {
          ++motor_disconnect_counters_[i];
        }
        if (motor_disconnect_counters_[i] >= config_.motor_disconnect_timeout) {
          error_flags_ |= kMotorDisconnect;
        }
      } else {
        motor_disconnect_counters_[i] = 0;
      }
    }
  }

  // ========================================================================
  // 裁判系统功率限制
  // ========================================================================
  void UpdateRefereeLimits(const EnergyInput& input) {
    // 1. 解析裁判功率上限
    if (input.referee_online) {
      referee_max_power_ =
          std::fmax(input.chassis_power_limit_w, kRefereeMinPowerW);
      last_robot_level_ = input.robot_level;
    } else {
      // 裁判离线：根据上次已知 robot_level 查表，默认按步兵
      const uint8_t level =
          std::min(last_robot_level_, static_cast<uint8_t>(kMaxRobotLevel));
      const uint8_t index = (level > 0U) ? (level - 1U) : 0U;
      referee_max_power_ = static_cast<float>(kInfantryPowerLimit[index]);
    }

    // 2. 计算 power_upper_limit_
    if (input.cap_online) {
      // 电容在线：裁判上限 + 电容最大输出功率
      power_upper_limit_ = referee_max_power_ + config_.cap_max_power_out;
      energy_loop_active_ = true;
    } else if (input.referee_online) {
      // 仅电容离线：裁判上限 + 能量环 Kp 余量
      power_upper_limit_ =
          referee_max_power_ + config_.energy_kp * kSqrtBufferDelta;
      energy_loop_active_ = true;
    } else {
      // 双断：功率衰减
      power_upper_limit_ =
          referee_max_power_ * config_.cap_referee_both_gg_coe;
      energy_loop_active_ = false;
    }

    // 3. 最小功率配置
    min_max_power_configured_ =
        referee_max_power_ * config_.min_max_power_ratio;
  }

  // ========================================================================
  // 能量环 PD 控制
  // ========================================================================
  void UpdateEnergyState(const EnergyInput& input, float dt_s) {
    // 双断：关闭能量环，固定衰减
    if (!input.referee_online && !input.cap_online) {
      energy_loop_active_ = false;
      base_max_power_ =
          referee_max_power_ * config_.cap_referee_both_gg_coe;
      full_max_power_ = base_max_power_;
      pd_last_error_full_ = 0.0f;
      pd_last_error_base_ = 0.0f;
      power_buff_ = 0.0f;
      return;
    }

    energy_loop_active_ = true;

    // 决定能量来源和目标值
    float full_target, base_target, feedback;
    if (input.cap_online && input.cap_output_enabled) {
      // 优先使用电容能量（原始值 0-255）
      full_target = config_.cap_full_buff;
      base_target = config_.cap_base_buff;
      feedback = std::fmin(input.cap_energy_raw, 255.0f);
    } else {
      // 电容离线或输出禁用 → fallback 到裁判缓冲能量
      full_target = config_.referee_full_buff;
      base_target = config_.referee_base_buff;
      feedback = std::fmin(input.buffer_energy, config_.referee_max_buffer_j);
    }

    // 存储当前缓冲值（用于遥测）
    power_buff_ = feedback;

    // PD 控制器：e(t) = sqrt(target) - sqrt(feedback)
    const float sqrt_full_target =
        std::sqrt(std::fmax(full_target, 1.0f));
    const float sqrt_base_target =
        std::sqrt(std::fmax(base_target, 1.0f));
    const float sqrt_feedback = std::sqrt(std::fmax(feedback, 1.0f));

    const float e_full = sqrt_full_target - sqrt_feedback;
    const float e_base = sqrt_base_target - sqrt_feedback;

    // 微分项
    const float de_full =
        (dt_s > 1e-6f) ? (e_full - pd_last_error_full_) / dt_s : 0.0f;
    const float de_base =
        (dt_s > 1e-6f) ? (e_base - pd_last_error_base_) / dt_s : 0.0f;

    // PD 输出
    const float pd_full =
        config_.energy_kp * e_full + config_.energy_kd * de_full;
    const float pd_base =
        config_.energy_kp * e_base + config_.energy_kd * de_base;

    // full_max_power_：激进上限（能量充足时允许超出裁判限制最多 cap_max_power_out）
    full_max_power_ = referee_max_power_ - pd_full;
    full_max_power_ = std::fmax(full_max_power_, min_max_power_configured_);
    full_max_power_ = std::fmin(full_max_power_,
                                referee_max_power_ + config_.cap_max_power_out);

    // base_max_power_：保守基础
    base_max_power_ = referee_max_power_ - pd_base;
    base_max_power_ = std::fmax(base_max_power_, min_max_power_configured_);

    // 更新误差历史
    pd_last_error_full_ = e_full;
    pd_last_error_base_ = e_base;

    // 电容离线能量估算（参考 HKUST powerDaemon）
    if (!input.cap_online) {
      UpdateCapEnergyEstimate(input, dt_s);
    } else {
      // 电容在线：使用实测值
      estimated_cap_energy_raw_ = input.cap_energy_raw;
      cap_energy_runout_ = false;
    }
  }

  // ========================================================================
  // 电容离线能量估算
  // ========================================================================
  void UpdateCapEnergyEstimate(const EnergyInput& input, float dt_s) {
    if (dt_s <= 1e-6f) return;

    if (input.referee_online) {
      // 裁判在线 + 电容离线
      if (input.chassis_power_w > config_.cap_offline_energy_runout_power &&
          input.buffer_energy < config_.referee_max_buffer_j) {
        // 判定电容能量已耗尽
        cap_energy_runout_ = true;
        estimated_cap_energy_raw_ = 0.0f;
        return;
      }

      // 功率差分积分：多余功率充电，不足功率放电
      const float power_diff =
          input.chassis_power_w - estimated_cap_power_w_;
      const float cap_energy_j =
          estimated_cap_energy_raw_ * kCapEnergyMaxJ / 255.0f;
      const float new_energy_j = cap_energy_j - power_diff * dt_s;
      estimated_cap_energy_raw_ =
          std::fmax(0.0f,
                    std::fmin(255.0f, new_energy_j * 255.0f / kCapEnergyMaxJ));
    } else {
      // 双断：假定额定充电功率持续充电
      const float cap_energy_j =
          estimated_cap_energy_raw_ * kCapEnergyMaxJ / 255.0f;
      const float new_energy_j =
          cap_energy_j + config_.cap_offline_energy_target_power * dt_s;
      estimated_cap_energy_raw_ =
          std::fmax(0.0f,
                    std::fmin(255.0f, new_energy_j * 255.0f / kCapEnergyMaxJ));
    }
  }

  // ========================================================================
  // RLS 自适应参数更新
  // ========================================================================
  void RunRlsUpdate(
      const EnergyInput& input,
      const std::array<WheelObj, kMotorCount>& wheels,
      float dt_s) {
    if (!rls_enabled_) return;

    // 获取实测功率
    const float measured_power =
        input.cap_online ? input.cap_chassis_power_w : input.chassis_power_w;

    // 死区保护：实测功率太小不更新
    if (std::fabs(measured_power) < config_.rls_dead_zone) return;
    // 电容断连且估算功率为负时不更新（数据不可信）
    if (!input.cap_online && estimated_cap_power_w_ < 0.0f) return;

    // 构建样本特征与有效机械功率
    float sum_abs_omega = 0.0f;
    float sum_tau_sq = 0.0f;
    float sum_mech_power = 0.0f;  // sum(tau_i * omega_i)

    for (std::size_t i = 0; i < kMotorCount; ++i) {
      const float tau = wheels[i].pid_tau_nm;
      const float omega = wheels[i].cur_omega_radps;
      sum_abs_omega += std::fabs(omega);
      sum_tau_sq += tau * tau;
      sum_mech_power += tau * omega;
    }

    // 测量损耗 = 实测电功率 - 有效机械功率 - 静态损耗 k3
    const float measured_loss = measured_power - sum_mech_power - k3_;

    // RLS 递推更新：样本 [sum(|omega|), sum(tau²)]，观测量 measured_loss
    rls_.Update(sum_abs_omega, sum_tau_sq, measured_loss, dt_s);

    // 从 RLS 提取参数
    const auto rls_state = rls_.GetParams();
    k1_ = rls_state.k1;  // |omega| 损耗系数
    k2_ = rls_state.k2;  // tau² 损耗系数

    // 更新功率估计值（用于下一周期的电容离线能量估算）
    estimated_cap_power_w_ =
        sum_mech_power + k1_ * sum_abs_omega + k2_ * sum_tau_sq + k3_;
  }

  // ========================================================================
  // 功率环核心：预测 → 分配权重 → 求解力矩上限
  // ========================================================================
  void RunPowerLoop(const std::array<WheelObj, kMotorCount>& wheels,
                    float max_power_w, float dt_s, Output& output) {
    (void)dt_s;

    // 预测各轮功率
    float predicted_power[kMotorCount]{};
    float total_positive_power = 0.0f;
    float total_negative_power_abs = 0.0f;
    float sum_error = 0.0f;

    for (std::size_t i = 0; i < kMotorCount; ++i) {
      const float tau = wheels[i].pid_tau_nm;
      const float omega = wheels[i].cur_omega_radps;

      // P_i = tau*omega + k1*|omega| + k2*tau² + k3/n
      predicted_power[i] =
          PredictMotorPowerW(tau, omega, k1_, k2_, k3_);

      output.power_allocation.estimated_power_w[i] = predicted_power[i];

      if (predicted_power[i] > 0.0f) {
        total_positive_power += predicted_power[i];
        output.power_allocation.positive_power_w[i] = predicted_power[i];
      } else {
        total_negative_power_abs += std::fabs(predicted_power[i]);
        output.power_allocation.positive_power_w[i] = 0.0f;
      }

      // 速度误差（用于误差置信度计算）
      const float speed_error =
          std::fabs(wheels[i].set_omega_radps - omega);
      output.power_allocation.speed_error_radps[i] = speed_error;
      sum_error += speed_error;
    }

    // 可分配功率 = max_power + 负功率回收（再生制动贡献）
    const float allocatable_power = max_power_w + total_negative_power_abs;
    output.power_allocation.total_positive_power_w = total_positive_power;
    output.power_allocation.active_power_limit_w = allocatable_power;

    // 判断是否需要功率限制
    if (total_positive_power <= allocatable_power) {
      // 功率充足，不限制
      for (std::size_t i = 0; i < kMotorCount; ++i) {
        output.limited_tau_nm[i] = wheels[i].pid_tau_nm;
        output.power_allocation.allocated_power_w[i] = predicted_power[i];
        output.power_allocation.allocated_tau_ref_nm[i] =
            wheels[i].pid_tau_nm;
      }
      output.power_limited = false;
      return;
    }

    // 功率不足，需要限制
    output.power_limited = true;
    output.power_allocation.power_limited = true;

    // 计算误差置信度 K_coe
    const float K_coe = ComputeConfidenceCoeff(sum_error);

    for (std::size_t i = 0; i < kMotorCount; ++i) {
      if (predicted_power[i] <= 0.0f) {
        // 负功率轮不参与分配，保持原始力矩（允许制动能量回收）
        output.limited_tau_nm[i] = wheels[i].pid_tau_nm;
        output.power_allocation.allocated_power_w[i] = predicted_power[i];
        output.power_allocation.allocated_tau_ref_nm[i] =
            wheels[i].pid_tau_nm;
        continue;
      }

      // 混合权重：K_coe * 误差权重 + (1-K_coe) * 功率权重
      const float speed_error =
          output.power_allocation.speed_error_radps[i];
      const float error_ratio =
          (sum_error > 1e-6f) ? (speed_error / sum_error) : 0.25f;
      const float power_ratio =
          (total_positive_power > 1e-6f)
              ? (predicted_power[i] / total_positive_power)
              : 0.25f;

      const float weight =
          K_coe * error_ratio + (1.0f - K_coe) * power_ratio;

      // 分配功率
      const float allocated_power = weight * allocatable_power;
      output.power_allocation.allocated_power_w[i] = allocated_power;

      // 求解该轮最大允许力矩
      float max_tau = SolveMaxTorqueForPower(
          allocated_power, wheels[i].cur_omega_radps,
          wheels[i].pid_tau_nm, k1_, k2_, k3_);

      // 钳位到物理上限
      const float max_limit = wheels[i].max_tau_nm;
      if (max_tau > max_limit) max_tau = max_limit;
      if (max_tau < -max_limit) max_tau = -max_limit;

      // 确保力矩方向与原始 PID 输出一致
      if ((wheels[i].pid_tau_nm >= 0.0f && max_tau < 0.0f) ||
          (wheels[i].pid_tau_nm < 0.0f && max_tau >= 0.0f)) {
        max_tau = 0.0f;
      }

      output.limited_tau_nm[i] = max_tau;
      output.power_allocation.allocated_tau_ref_nm[i] = max_tau;
    }
  }

  // ========================================================================
  // 误差置信度系数 K_coe
  // sum_error 低 → K_coe ≈ 1（倾向按速度误差分配，响应控制需求）
  // sum_error 高 → K_coe ≈ 0（倾向按当前功率分配，保持稳定性）
  // ========================================================================
  float ComputeConfidenceCoeff(float sum_error) const {
    if (sum_error <= config_.prop_distribution_threshold) return 1.0f;
    if (sum_error >= config_.error_distribution_threshold) return 0.0f;

    // 线性插值
    return 1.0f -
           (sum_error - config_.prop_distribution_threshold) /
               (config_.error_distribution_threshold -
                config_.prop_distribution_threshold);
  }

  // ========================================================================
  // 成员变量
  // ========================================================================

  Config::ChassisPowerControllerConfig config_;
  Module::RLS::RlsEstimator rls_;

  // 功率模型系数（由 RLS 在线更新）
  float k1_ = 0.22f;   // |omega| 损耗系数 (W/(rad/s))
  float k2_ = 1.2f;    // tau² 损耗系数 (W/Nm²)
  float k3_ = 2.78f;   // 静态损耗总量 (W)
  bool rls_enabled_ = true;

  // 能量环状态
  float power_buff_ = 0.0f;
  float full_max_power_ = 0.0f;
  float base_max_power_ = 0.0f;
  float power_upper_limit_ = 0.0f;
  float referee_max_power_ = 0.0f;
  float user_configured_max_power_ = 0.0f;
  bool energy_loop_active_ = false;

  // PD 误差历史
  float pd_last_error_full_ = 0.0f;
  float pd_last_error_base_ = 0.0f;

  // 电容离线能量估算
  float estimated_cap_energy_raw_ = 0.0f;
  float estimated_cap_power_w_ = 0.0f;
  bool cap_energy_runout_ = false;

  // 错误标志与模式
  uint8_t error_flags_ = 0;
  uint8_t last_robot_level_ = 1;
  uint8_t mode_ = 0;
  float min_max_power_configured_ = 30.0f;

  // 电机断连计数器（累计离线周期数）
  std::array<uint16_t, kMotorCount> motor_disconnect_counters_{};
};

}  // namespace App
