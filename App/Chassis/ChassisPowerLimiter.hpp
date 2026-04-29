#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "ChassisMotorPowerModel.hpp"
#include "../Cmd/RefereeConstraintView.hpp"
#include "../Config/ChassisConfig.hpp"
#include "../Topics/MotionCommand.hpp"
#include "../../Modules/SuperCap/SuperCap.hpp"

namespace App {

class ChassisPowerLimiter {
 public:
  using WheelSpeedArray = std::array<float, 4>;

  struct Input {
    const MotionCommand& command;
    const RefereeConstraintView& referee;
    const SuperCapState& super_cap;
    const WheelSpeedArray& current_wheel_speed_radps;
    const WheelSpeedArray& target_wheel_speed_radps;
    const WheelSpeedArray& wheel_tau_ref_nm;
    const WheelSpeedArray& current_cmd_raw;
    const WheelSpeedArray& motor_speed_rpm;
  };

  struct Output {
    MotionCommand command{};
    WheelSpeedArray target_wheel_speed_radps{0.0f, 0.0f, 0.0f, 0.0f};
    WheelSpeedArray limited_wheel_tau_ref_nm{0.0f, 0.0f, 0.0f, 0.0f};
    ChassisPowerAllocation motor_power_allocation{};
    float motion_scale = 1.0f;
    float active_power_limit_w = 0.0f;
    bool power_limited = false;
    bool motor_power_limited = false;
  };

  explicit constexpr ChassisPowerLimiter(
      Config::ChassisPowerLimiterConfig config =
          Config::kChassisPowerLimiterConfig)
      : config_(config) {}

  Output Apply(const Input& input) {
    Output output{};
    output.command = input.command;
    output.target_wheel_speed_radps = input.target_wheel_speed_radps;
    output.limited_wheel_tau_ref_nm = input.wheel_tau_ref_nm;

    const float desired_scale = ResolveDesiredScale(input);
    const float scale = UpdateFilteredScale(desired_scale);

    output.motion_scale = scale;
    output.active_power_limit_w = ResolveActivePowerLimit(input);
    // allocated_current_cmd_raw 仅作为 donor 功率分配诊断暴露，当前不接管 DJIMotor 发帧。
    output.motor_power_allocation = AllocateChassisMotorPower(
        input.current_cmd_raw, input.motor_speed_rpm,
        output.active_power_limit_w);
    output.motor_power_limited = output.motor_power_allocation.power_limited;
    output.power_limited =
        input.referee.power_limited || scale < config_.full_scale_epsilon ||
        output.motor_power_limited;

    ScaleCommand(output.command, scale);
    ScaleWheelTargets(output.target_wheel_speed_radps, scale);
    ScaleWheelTorques(output.limited_wheel_tau_ref_nm, scale);

    output.command.power_limited = output.power_limited;
    output.command.chassis_power_limit_w = output.active_power_limit_w;
    return output;
  }

 private:
  static constexpr float Clamp01(float value) {
    if (value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
  }

  static constexpr float ScaleByPowerLimit(
      float chassis_power_limit_w,
      const Config::ChassisPowerLimiterConfig& config) {
    if (chassis_power_limit_w >= config.full_power_limit_w) {
      return 1.0f;
    }
    if (chassis_power_limit_w >= config.high_power_limit_w) {
      return config.high_power_scale;
    }
    if (chassis_power_limit_w >= config.medium_power_limit_w) {
      return config.medium_power_scale;
    }
    if (chassis_power_limit_w > 0.0f) {
      return config.low_power_scale;
    }
    return config.stop_scale;
  }

  static constexpr float ScaleByCapEnergy(
      float cap_energy_percent,
      const Config::ChassisPowerLimiterConfig& config) {
    if (cap_energy_percent >= config.full_energy_percent) {
      return 1.0f;
    }
    if (cap_energy_percent >= config.high_energy_percent) {
      return config.high_energy_scale;
    }
    if (cap_energy_percent >= config.medium_energy_percent) {
      return config.medium_energy_scale;
    }
    if (cap_energy_percent >= config.low_energy_percent) {
      return config.low_energy_scale;
    }
    if (cap_energy_percent > 0.0f) {
      return config.critical_energy_scale;
    }
    return config.stop_scale;
  }

  static bool IsUsableSuperCap(const SuperCapState& super_cap) {
    return super_cap.online &&
           !SuperCapProtocol::IsOutputDisabled(super_cap.error_code);
  }

  float ResolveDesiredScale(const Input& input) const {
    float scale = Clamp01(input.referee.motion_scale);

    if (IsUsableSuperCap(input.super_cap)) {
      scale = std::min(scale, ScaleByCapEnergy(
                                  input.super_cap.cap_energy_percent, config_));

      if (input.super_cap.chassis_power_limit_w > 0U) {
        scale = std::min(
            scale, ScaleByPowerLimit(
                       static_cast<float>(
                           input.super_cap.chassis_power_limit_w),
                       config_));
      }

      const float active_power_limit_w = ResolveActivePowerLimit(input);
      if (active_power_limit_w > 0.0f &&
          input.super_cap.chassis_power_w >
              active_power_limit_w + config_.power_overshoot_margin_w) {
        scale = std::min(scale, config_.overshoot_scale);
      }

      if (input.referee.buffer_energy_valid &&
          input.referee.buffer_energy < config_.low_buffer_energy_j) {
        scale = std::min(scale, config_.low_buffer_energy_scale);
      }
    }

    const float requested_delta = MaxAbsWheelDelta(
        input.current_wheel_speed_radps, input.target_wheel_speed_radps);
    if (requested_delta > config_.large_wheel_delta_radps) {
      scale = std::min(scale, config_.large_delta_scale);
    }

    return Clamp01(scale);
  }

  float ResolveActivePowerLimit(const Input& input) const {
    float limit_w = input.referee.referee_online
                        ? input.referee.chassis_power_limit_w
                        : 0.0f;

    if (IsUsableSuperCap(input.super_cap) &&
        input.super_cap.chassis_power_limit_w > 0U) {
      const float cap_limit_w =
          static_cast<float>(input.super_cap.chassis_power_limit_w);
      limit_w = limit_w > 0.0f ? std::min(limit_w, cap_limit_w) : cap_limit_w;
    }

    return limit_w;
  }

  float UpdateFilteredScale(float desired_scale) {
    desired_scale = Clamp01(desired_scale);
    if (desired_scale < filtered_scale_) {
      filtered_scale_ = desired_scale;
    } else {
      filtered_scale_ = std::min(
          desired_scale, filtered_scale_ + config_.recovery_step_per_monitor);
    }
    return filtered_scale_;
  }

  static float MaxAbsWheelDelta(const WheelSpeedArray& current,
                                const WheelSpeedArray& target) {
    float max_delta = 0.0f;
    for (std::size_t index = 0; index < current.size(); ++index) {
      max_delta = std::max(max_delta, std::fabs(target[index] - current[index]));
    }
    return max_delta;
  }

  static void ScaleCommand(MotionCommand& command, float scale) {
    if (scale <= 0.0f) {
      command.motion_mode = MotionModeType::kStop;
      command.vx_mps = 0.0f;
      command.vy_mps = 0.0f;
      command.wz_radps = 0.0f;
      return;
    }

    command.vx_mps *= scale;
    command.vy_mps *= scale;
    command.wz_radps *= scale;
  }

  static void ScaleWheelTargets(WheelSpeedArray& target_wheel_speed_radps,
                                float scale) {
    for (auto& wheel_speed : target_wheel_speed_radps) {
      wheel_speed *= scale;
    }
  }

  static void ScaleWheelTorques(WheelSpeedArray& wheel_tau_ref_nm,
                                float scale) {
    for (auto& tau : wheel_tau_ref_nm) {
      tau *= scale;
    }
  }

  Config::ChassisPowerLimiterConfig config_{};
  float filtered_scale_ = 1.0f;
};

}  // namespace App
