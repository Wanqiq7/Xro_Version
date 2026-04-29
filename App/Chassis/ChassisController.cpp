#include "ChassisController.hpp"

#include <cmath>

#include "../Config/AppConfig.hpp"
#include "../../Modules/DJIMotor/DJIMotor.hpp"

namespace App {

namespace {

constexpr std::size_t kWheelCount = 4;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kRpmToRadps = 2.0f * kPi / 60.0f;
constexpr float kRadpsToDegps = 180.0f / kPi;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kFollowGimbalYawGain = 1.5f;
constexpr float kSpinAngularSpeedRadps = 6.0f;
constexpr float kCommandFollowRateHz = 5.0f;
constexpr float kCommandFallbackRateHz = 5.0f;
constexpr float kDisabledDecayRateHz = 2.5f;
constexpr float kMaxYawIntegrationStepS = 0.05f;

float ClampAbs(float value, float limit) {
  if (value > limit) {
    return limit;
  }
  if (value < -limit) {
    return -limit;
  }
  return value;
}

constexpr float Clamp01(float value) {
  if (value <= 0.0f) return 0.0f;
  if (value >= 1.0f) return 1.0f;
  return value;
}

constexpr float Lerp(float from, float to, float alpha) {
  return from + (to - from) * Clamp01(alpha);
}

constexpr float WrapAngleDeg(float angle_deg) {
  while (angle_deg > 180.0f) angle_deg -= 360.0f;
  while (angle_deg < -180.0f) angle_deg += 360.0f;
  return angle_deg;
}

bool IsChassisReady(const RobotMode& robot_mode) {
  return robot_mode.is_enabled && !robot_mode.is_emergency_stop &&
         robot_mode.robot_mode != RobotModeType::kSafe;
}

MotionCommand BuildAppliedMotionCommand(
    const RobotMode& robot_mode, const MotionCommand& command,
    const Config::MecanumChassisConfig& actuator_config, float relative_yaw_deg,
    bool has_yaw_feedback, const GimbalState& gimbal_state) {
  MotionCommand applied = command;
  applied.vx_mps =
      ClampAbs(command.vx_mps, actuator_config.max_linear_speed_mps);
  applied.vy_mps =
      ClampAbs(command.vy_mps, actuator_config.max_linear_speed_mps);
  applied.wz_radps =
      ClampAbs(command.wz_radps, actuator_config.max_angular_speed_radps);

  if (!IsChassisReady(robot_mode) ||
      command.motion_mode == MotionModeType::kStop) {
    applied.motion_mode = MotionModeType::kStop;
    applied.vx_mps = 0.0f;
    applied.vy_mps = 0.0f;
    applied.wz_radps = 0.0f;
    applied.is_field_relative = false;
    return applied;
  }

  if (command.motion_mode == MotionModeType::kFollowGimbal &&
      gimbal_state.online) {
    const float yaw_error_deg = gimbal_state.yaw_deg;
    applied.wz_radps = ClampAbs(
        -kFollowGimbalYawGain * yaw_error_deg * std::fabs(yaw_error_deg) *
            kDegToRad,
        actuator_config.max_angular_speed_radps);
  } else if (command.motion_mode == MotionModeType::kSpin) {
    applied.wz_radps = ClampAbs(
        kSpinAngularSpeedRadps, actuator_config.max_angular_speed_radps);
  }

  float projection_yaw_deg = 0.0f;
  bool should_rotate = false;
  if (command.is_field_relative && has_yaw_feedback) {
    projection_yaw_deg = relative_yaw_deg;
    should_rotate = true;
  } else if (command.motion_mode == MotionModeType::kFollowGimbal &&
             gimbal_state.online) {
    projection_yaw_deg = gimbal_state.yaw_deg;
    should_rotate = true;
  }

  if (should_rotate) {
    const float yaw_rad = projection_yaw_deg * kDegToRad;
    const float cos_yaw = std::cos(yaw_rad);
    const float sin_yaw = std::sin(yaw_rad);
    const float field_vx = applied.vx_mps;
    const float field_vy = applied.vy_mps;
    applied.vx_mps = field_vx * cos_yaw + field_vy * sin_yaw;
    applied.vy_mps = -field_vx * sin_yaw + field_vy * cos_yaw;
  }

  applied.is_field_relative = false;
  return applied;
}

std::array<float, kWheelCount> SolveWheelTargets(
    const MotionCommand& command,
    const Config::MecanumChassisConfig& actuator_config) {
  const float wheel_radius = std::max(actuator_config.wheel_radius_m, 0.001f);
  const float rotation_arm =
      (actuator_config.wheel_base_m + actuator_config.track_width_m) * 0.5f;

  std::array<float, kWheelCount> wheel_speed_radps{
      (command.vx_mps - command.vy_mps - command.wz_radps * rotation_arm) /
          wheel_radius,
      (command.vx_mps + command.vy_mps + command.wz_radps * rotation_arm) /
          wheel_radius,
      (command.vx_mps + command.vy_mps - command.wz_radps * rotation_arm) /
          wheel_radius,
      (command.vx_mps - command.vy_mps + command.wz_radps * rotation_arm) /
          wheel_radius,
  };

  for (auto& wheel_speed : wheel_speed_radps) {
    wheel_speed = ClampAbs(wheel_speed, actuator_config.max_wheel_speed_radps);
  }

  return wheel_speed_radps;
}

struct BodyVelocity {
  float vx_mps = 0.0f;
  float vy_mps = 0.0f;
  float wz_radps = 0.0f;
};

struct EstimateInput {
  bool wheel_feedback_valid = false;
  bool yaw_feedback_valid = false;
  bool output_enabled = false;
  float dt_s = 0.0f;
  float wheel_vx_mps = 0.0f;
  float wheel_vy_mps = 0.0f;
  float wheel_wz_radps = 0.0f;
  float gyro_wz_radps = 0.0f;
  float command_vx_mps = 0.0f;
  float command_vy_mps = 0.0f;
  float command_wz_radps = 0.0f;
};

constexpr BodyVelocity SolveBodyVelocityFromWheelSpeed(
    const std::array<float, kWheelCount>& wheel_speed_radps,
    const Config::MecanumChassisConfig& actuator_config) {
  const float rotation_arm =
      (actuator_config.wheel_base_m + actuator_config.track_width_m) * 0.5f;
  const float wheel_radius = actuator_config.wheel_radius_m;
  const float w0 = wheel_speed_radps[0];
  const float w1 = wheel_speed_radps[1];
  const float w2 = wheel_speed_radps[2];
  const float w3 = wheel_speed_radps[3];

  return BodyVelocity{
      .vx_mps = (w0 + w1 + w2 + w3) * wheel_radius * 0.25f,
      .vy_mps = (-w0 + w1 + w2 - w3) * wheel_radius * 0.25f,
      .wz_radps = rotation_arm > 0.0f
                      ? (-w0 + w1 - w2 + w3) * wheel_radius /
                            (4.0f * rotation_arm)
                      : 0.0f,
  };
}

constexpr float IntegrateYawDeg(float previous_yaw_deg, float gyro_wz_radps,
                                float dt_s) {
  if (dt_s <= 0.0f || dt_s > kMaxYawIntegrationStepS) {
    return previous_yaw_deg;
  }
  return WrapAngleDeg(previous_yaw_deg + gyro_wz_radps * dt_s * kRadToDeg);
}

constexpr VelocityEstimateState AdvanceVelocityEstimate(
    const VelocityEstimateState& previous, const EstimateInput& input) {
  VelocityEstimateState next = previous;

  if (input.wheel_feedback_valid) {
    next.vx_mps = input.wheel_vx_mps;
    next.vy_mps = input.wheel_vy_mps;
  } else {
    const float fallback_rate_hz =
        input.output_enabled ? kCommandFallbackRateHz : kDisabledDecayRateHz;
    const float alpha = input.dt_s * fallback_rate_hz;
    const float target_vx_mps = input.output_enabled ? input.command_vx_mps : 0.0f;
    const float target_vy_mps = input.output_enabled ? input.command_vy_mps : 0.0f;
    next.vx_mps = Lerp(previous.vx_mps, target_vx_mps, alpha);
    next.vy_mps = Lerp(previous.vy_mps, target_vy_mps, alpha);
  }

  if (input.yaw_feedback_valid) {
    next.wz_radps = input.gyro_wz_radps;
  } else if (input.wheel_feedback_valid) {
    next.wz_radps = input.wheel_wz_radps;
  } else {
    const float fallback_rate_hz =
        input.output_enabled ? kCommandFollowRateHz : kDisabledDecayRateHz;
    const float alpha = input.dt_s * fallback_rate_hz;
    const float target_wz_radps = input.output_enabled ? input.command_wz_radps : 0.0f;
    next.wz_radps = Lerp(previous.wz_radps, target_wz_radps, alpha);
  }

  return next;
}

bool AnyWheelOnline(const std::array<DJIMotor*, kWheelCount>& wheel_motors) {
  for (const auto* motor : wheel_motors) {
    if (motor != nullptr && motor->GetFeedback().online) {
      return true;
    }
  }
  return false;
}

bool AllWheelFeedbackValid(
    const std::array<DJIMotor*, kWheelCount>& wheel_motors) {
  for (const auto* motor : wheel_motors) {
    if (motor == nullptr || !motor->GetFeedback().online) {
      return false;
    }
  }
  return true;
}

std::array<float, kWheelCount> ReadWheelSpeedRadps(
    const std::array<DJIMotor*, kWheelCount>& wheel_motors) {
  std::array<float, kWheelCount> wheel_speed_radps{0.0f, 0.0f, 0.0f, 0.0f};
  for (std::size_t index = 0; index < kWheelCount; ++index) {
    if (wheel_motors[index] == nullptr) {
      continue;
    }
    wheel_speed_radps[index] =
        wheel_motors[index]->GetFeedback().output_speed_rpm * kRpmToRadps;
  }
  return wheel_speed_radps;
}

std::array<float, kWheelCount> ReadWheelCurrentCommandRaw(
    const std::array<DJIMotor*, kWheelCount>& wheel_motors) {
  std::array<float, kWheelCount> current_cmd_raw{0.0f, 0.0f, 0.0f, 0.0f};
  for (std::size_t index = 0; index < kWheelCount; ++index) {
    if (wheel_motors[index] == nullptr) {
      continue;
    }
    current_cmd_raw[index] =
        static_cast<float>(wheel_motors[index]->GetState().target_current);
  }
  return current_cmd_raw;
}

std::array<float, kWheelCount> ReadWheelRotorSpeedRpm(
    const std::array<DJIMotor*, kWheelCount>& wheel_motors) {
  std::array<float, kWheelCount> motor_speed_rpm{0.0f, 0.0f, 0.0f, 0.0f};
  for (std::size_t index = 0; index < kWheelCount; ++index) {
    if (wheel_motors[index] == nullptr) {
      continue;
    }
    motor_speed_rpm[index] = wheel_motors[index]->GetFeedback().rotor_speed_rpm;
  }
  return motor_speed_rpm;
}

void ApplyWheelTargets(
    const std::array<DJIMotor*, kWheelCount>& wheel_motors,
    const std::array<float, kWheelCount>& target_wheel_speed_radps,
    bool output_enabled) {
  for (std::size_t index = 0; index < kWheelCount; ++index) {
    auto* motor = wheel_motors[index];
    if (motor == nullptr) {
      continue;
    }
    if (!output_enabled) {
      motor->Stop();
      continue;
    }
    motor->SetOuterLoop(DJIMotorOuterLoop::kSpeed);
    motor->ChangeFeed(DJIMotorOuterLoop::kSpeed, DJIMotorFeedbackSource::kMotor);
    motor->Enable();
    motor->SetReference(target_wheel_speed_radps[index] * kRadpsToDegps);
  }
}

}  // namespace

ChassisController::ChassisController(LibXR::ApplicationManager& appmgr,
                                     const MotionCommand& motion_command,
                                     LibXR::Topic& robot_mode_topic,
                                     LibXR::Topic& gimbal_state_topic,
                                     LibXR::Topic& chassis_state_topic,
                                     LibXR::Topic::Domain& app_topic_domain,
                                     DJIMotor& front_left_motor,
                                     DJIMotor& front_right_motor,
                                     DJIMotor& rear_left_motor,
                                     DJIMotor& rear_right_motor,
                                     SuperCap& super_cap)
    : ControllerBase(appmgr),
      motion_command_(motion_command),
      chassis_state_topic_(chassis_state_topic),
      wheel_motors_{
          &front_left_motor,
          &front_right_motor,
          &rear_left_motor,
          &rear_right_motor,
      },
      super_cap_(&super_cap),
      robot_mode_subscriber_(robot_mode_topic),
      gimbal_state_subscriber_(gimbal_state_topic),
      ins_state_subscriber_(AppConfig::kInsStateTopicName, &app_topic_domain),
      referee_subscriber_(AppConfig::kRefereeStateTopicName),
      super_cap_subscriber_(SuperCap::kTopicName) {
  robot_mode_subscriber_.StartWaiting();
  gimbal_state_subscriber_.StartWaiting();
  ins_state_subscriber_.StartWaiting();
  referee_subscriber_.StartWaiting();
  super_cap_subscriber_.StartWaiting();

  for (auto* motor : wheel_motors_) {
    if (motor == nullptr) {
      continue;
    }
    motor->SetOuterLoop(DJIMotorOuterLoop::kSpeed);
    motor->ChangeFeed(DJIMotorOuterLoop::kSpeed, DJIMotorFeedbackSource::kMotor);
    motor->Stop();
  }
}

void ChassisController::PullTopicData() {
  applied_motion_command_ = motion_command_;

  if (robot_mode_subscriber_.Available()) {
    robot_mode_ = robot_mode_subscriber_.GetData();
    robot_mode_subscriber_.StartWaiting();
  }

  if (gimbal_state_subscriber_.Available()) {
    gimbal_state_ = gimbal_state_subscriber_.GetData();
    gimbal_state_subscriber_.StartWaiting();
  }

  if (ins_state_subscriber_.Available()) {
    ins_state_ = ins_state_subscriber_.GetData();
    ins_state_subscriber_.StartWaiting();
  }

  if (referee_subscriber_.Available()) {
    referee_state_ = referee_subscriber_.GetData();
    referee_subscriber_.StartWaiting();
  }

  if (super_cap_subscriber_.Available()) {
    super_cap_state_ = super_cap_subscriber_.GetData();
    super_cap_subscriber_.StartWaiting();
  }
}

void ChassisController::UpdateWheelControl() {
  const RefereeConstraintView referee_constraints =
      BuildRefereeConstraintView(referee_state_);
  const bool has_yaw_feedback = ins_state_.online && ins_state_.gyro_online;
  applied_motion_command_ = BuildAppliedMotionCommand(
      robot_mode_, motion_command_, actuator_config_,
      ins_state_.yaw_deg, has_yaw_feedback, gimbal_state_);
  const auto requested_wheel_speed_radps =
      SolveWheelTargets(applied_motion_command_, actuator_config_);
  const auto current_wheel_speed_radps = ReadWheelSpeedRadps(wheel_motors_);
  const auto current_cmd_raw = ReadWheelCurrentCommandRaw(wheel_motors_);
  const auto motor_speed_rpm = ReadWheelRotorSpeedRpm(wheel_motors_);
  const auto power_limited_output = power_limiter_.Apply(
      ChassisPowerLimiter::Input{
          .command = applied_motion_command_,
          .referee = referee_constraints,
          .super_cap = super_cap_state_,
          .current_wheel_speed_radps = current_wheel_speed_radps,
          .target_wheel_speed_radps = requested_wheel_speed_radps,
          .current_cmd_raw = current_cmd_raw,
          .motor_speed_rpm = motor_speed_rpm,
      });
  applied_motion_command_ = power_limited_output.command;
  output_enabled_ = IsChassisReady(robot_mode_) &&
                    applied_motion_command_.motion_mode !=
                        MotionModeType::kStop;
  target_wheel_speed_radps_ = power_limited_output.target_wheel_speed_radps;
  ApplyWheelTargets(wheel_motors_, target_wheel_speed_radps_, output_enabled_);

  SendSuperCapCommand(referee_constraints);
}

void ChassisController::SendSuperCapCommand(
    const RefereeConstraintView& referee) {
  if (super_cap_ == nullptr) {
    return;
  }

  super_cap_cmd_counter_ =
      (super_cap_cmd_counter_ + 1) % power_limiter_config_.super_cap_cmd_interval;
  if (super_cap_cmd_counter_ != 0) {
    return;
  }

  const bool enable_dcdc = referee.referee_online && referee.buffer_energy > 0;
  const auto power_limit_w =
      static_cast<std::uint16_t>(std::min(referee.chassis_power_limit_w, 65535.0f));
  const auto energy_buffer_j =
      static_cast<std::uint16_t>(std::min(
          static_cast<unsigned>(referee.buffer_energy), 65535U));

  super_cap_->SendCommand(enable_dcdc, power_limit_w, energy_buffer_j);
}

void ChassisController::UpdateChassisState(
    LibXR::MicrosecondTimestamp now_us) {
  const bool any_wheel_online = AnyWheelOnline(wheel_motors_);
  const bool wheel_feedback_valid = AllWheelFeedbackValid(wheel_motors_);
  const auto wheel_speed_radps = ReadWheelSpeedRadps(wheel_motors_);
  const auto wheel_estimate =
      SolveBodyVelocityFromWheelSpeed(
          wheel_speed_radps, actuator_config_);
  const float estimate_dt_s =
      last_estimate_update_us_ != 0
          ? (now_us - last_estimate_update_us_).ToSecondf()
          : 0.0f;
  last_estimate_update_us_ = now_us;
  estimate_state_ = AdvanceVelocityEstimate(
      estimate_state_,
      EstimateInput{
          .wheel_feedback_valid = wheel_feedback_valid,
          .yaw_feedback_valid = ins_state_.online && ins_state_.gyro_online,
          .output_enabled = output_enabled_,
          .dt_s = estimate_dt_s,
          .wheel_vx_mps = wheel_estimate.vx_mps,
          .wheel_vy_mps = wheel_estimate.vy_mps,
          .wheel_wz_radps = wheel_estimate.wz_radps,
          .gyro_wz_radps = ins_state_.yaw_rate_degps * kDegToRad,
          .command_vx_mps = applied_motion_command_.vx_mps,
          .command_vy_mps = applied_motion_command_.vy_mps,
          .command_wz_radps = applied_motion_command_.wz_radps,
      });

  chassis_state_.online = any_wheel_online;
  chassis_state_.ready = IsChassisReady(robot_mode_);
  chassis_state_.feedback_online =
      (ins_state_.online && ins_state_.gyro_online) || any_wheel_online;
  chassis_state_.velocity_feedback_valid = wheel_feedback_valid;
  chassis_state_.yaw_feedback_valid = ins_state_.online && ins_state_.gyro_online;

  chassis_state_.command_vx_mps = applied_motion_command_.vx_mps;
  chassis_state_.command_vy_mps = applied_motion_command_.vy_mps;
  chassis_state_.command_wz_radps = applied_motion_command_.wz_radps;

  chassis_state_.estimated_vx_mps = estimate_state_.vx_mps;
  chassis_state_.estimated_vy_mps = estimate_state_.vy_mps;
  chassis_state_.feedback_wz_radps = estimate_state_.wz_radps;

  chassis_state_.vx_mps = chassis_state_.estimated_vx_mps;
  chassis_state_.vy_mps = chassis_state_.estimated_vy_mps;
  chassis_state_.wz_radps = chassis_state_.feedback_wz_radps;
  chassis_state_.relative_yaw_deg =
      gimbal_state_.online ? gimbal_state_.relative_yaw_deg
                           : chassis_state_.relative_yaw_deg;
  chassis_state_.yaw_deg = chassis_state_.relative_yaw_deg;

  chassis_state_topic_.Publish(chassis_state_);
}

void ChassisController::OnMonitor() {
  PullTopicData();

  const auto now_us = LibXR::Timebase::GetMicroseconds();
  UpdateWheelControl();
  UpdateChassisState(now_us);
}

}  // namespace App
