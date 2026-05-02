#pragma once

#include <array>
#include <cstddef>

#include "ChassisMotorPowerModel.hpp"
#include "ChassisPowerController.hpp"
#include "../Cmd/RefereeConstraintView.hpp"
#include "../Config/ChassisConfig.hpp"
#include "../Topics/MotionCommand.hpp"
#include "../../Modules/SuperCap/SuperCap.hpp"

namespace App {

/// @brief 底盘功率限制器 —— ChassisPowerController 的薄适配层
///
/// 职责：将外部输入（裁判约束、超级电容状态、PID 力矩、轮速）转换为
/// ChassisPowerController 的内部格式，调用其 Update()，再将输出映射回
/// 功率限制后的力矩与 diagnostic 字段。
///
/// 与旧版的核心差异：
/// - 不再对 target_wheel_speed_radps / MotionCommand 速度做缩放
/// - 不再使用离散档位 scale（high/medium/low）
/// - 功率限制通过钳位 wheel_tau_ref_nm 直接实现
class ChassisPowerLimiter {
 public:
  using WheelSpeedArray = std::array<float, 4>;

  struct Input {
    const MotionCommand& command;
    const RefereeConstraintView& referee;
    const SuperCapState& super_cap;
    const WheelSpeedArray& current_wheel_speed_radps;   // 当前轮速 (rad/s)
    const WheelSpeedArray& target_wheel_speed_radps;    // 目标轮速 (rad/s)
    const WheelSpeedArray& wheel_tau_ref_nm;            // PID 力矩输出 (Nm)
    const WheelSpeedArray& current_cmd_raw;             // 当前电流命令（不再用于功率限制）
    const WheelSpeedArray& motor_speed_rpm;             // 电机转速 RPM（不再用于功率限制）
  };

  struct Output {
    MotionCommand command{};
    WheelSpeedArray target_wheel_speed_radps{};
    WheelSpeedArray limited_wheel_tau_ref_nm{};   // 功率限制后的力矩
    ChassisPowerAllocation motor_power_allocation{};
    float motion_scale = 1.0f;
    float active_power_limit_w = 0.0f;
    bool power_limited = false;
    bool motor_power_limited = false;
    // 能量环和 RLS 状态暴露
    float base_max_power_w = 0.0f;
    float full_max_power_w = 0.0f;
    float estimated_cap_energy_raw = 0.0f;
    float k1 = 0.22f;
    float k2 = 1.2f;
    uint8_t error_flags = 0;
  };

  explicit ChassisPowerLimiter(
      Config::ChassisPowerLimiterConfig config =
          Config::kChassisPowerLimiterConfig)
      : config_(config) {}

  /// @brief 功率限制主入口
  /// @param input 外部输入（裁判约束、超级电容状态、PID 力矩、轮速）
  /// @param dt_s 距上次调用时间 (s)，供能量环积分和 RLS 遗忘
  /// @param output 功率限制后的力矩与 diagnostic 信息
  void Apply(const Input& input, float dt_s, Output& output) {
    output = Output{};

    // ── 1. 构建能量输入 ──
    ChassisPowerController::EnergyInput energy_input{};
    energy_input.referee_online = input.referee.referee_online;
    energy_input.chassis_power_limit_w = input.referee.chassis_power_limit_w;
    energy_input.chassis_power_w = input.referee.chassis_power_w;
    energy_input.buffer_energy =
        static_cast<float>(input.referee.buffer_energy);
    energy_input.robot_level = input.referee.robot_level;

    energy_input.cap_online =
        input.super_cap.online &&
        !SuperCapProtocol::IsOutputDisabled(input.super_cap.error_code);
    energy_input.cap_output_enabled =
        !SuperCapProtocol::IsOutputDisabled(input.super_cap.error_code);
    energy_input.cap_energy_raw =
        input.super_cap.cap_energy_percent * 2.55f;  // 0-100% → 0-255 原始值
    energy_input.cap_chassis_power_w =
        input.super_cap.chassis_power_w;
    energy_input.cap_power_limit_w =
        input.super_cap.chassis_power_limit_w;

    // ── 2. 构建四轮对象 ──
    std::array<ChassisPowerController::WheelObj, ChassisPowerController::kMotorCount> wheels;
    for (std::size_t i = 0; i < wheels.size(); ++i) {
      wheels[i].pid_tau_nm = input.wheel_tau_ref_nm[i];
      wheels[i].cur_omega_radps = input.current_wheel_speed_radps[i];
      wheels[i].set_omega_radps = input.target_wheel_speed_radps[i];
      // 最大力矩限制来自力控配置
      wheels[i].max_tau_nm = Config::kChassisForceControlConfig.max_wheel_tau_ref_nm;
      // 电机在线判定：超级电容正常且该轮有速度反馈
      // 默认在线，降级时可通过 super_cap state 判断
      wheels[i].online = true;
    }

    // ── 3. 调用功率控制器 ──
    const auto pwr_output = power_controller_.Update(energy_input, wheels, dt_s);

    // ── 4. 填充输出 ──
    output.limited_wheel_tau_ref_nm = pwr_output.limited_tau_nm;
    output.motor_power_allocation = pwr_output.power_allocation;
    output.active_power_limit_w = pwr_output.active_power_limit_w;
    output.power_limited = pwr_output.power_limited;
    output.motor_power_limited = pwr_output.power_limited;
    output.base_max_power_w = pwr_output.base_max_power_w;
    output.full_max_power_w = pwr_output.full_max_power_w;
    output.estimated_cap_energy_raw = pwr_output.estimated_cap_energy_raw;
    output.k1 = pwr_output.k1;
    output.k2 = pwr_output.k2;
    output.error_flags = pwr_output.error_flags;

    // 不再缩放速度：直接拷贝原始 command 和 target
    output.command = input.command;
    output.target_wheel_speed_radps = input.target_wheel_speed_radps;
    output.motion_scale = 1.0f;

    // 在 MotionCommand 上设置功率限制标志
    output.command.power_limited = output.power_limited;
    output.command.chassis_power_limit_w = output.active_power_limit_w;

  }

  /// @brief 兼容旧调用方式；新代码优先使用带 Output 引用的重载。
  Output Apply(const Input& input, float dt_s) {
    Output output{};
    Apply(input, dt_s, output);
    return output;
  }

 private:
  Config::ChassisPowerLimiterConfig config_{};
  ChassisPowerController power_controller_{};
};

}  // namespace App
