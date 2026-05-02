#include "ShootController.hpp"

#include <algorithm>
#include <cmath>

#include "../Config/AppConfig.hpp"

namespace App {

namespace {

constexpr float kRpmToDegpsScale = 6.0f;
constexpr float kSingleShotAngleToleranceDeg = 2.0f;
constexpr float kSingleShotSpeedToleranceDegps = 180.0f;
constexpr float kLoaderJammedSpeedToleranceDegps = 60.0f;
constexpr std::uint32_t kJamDetectTimeoutMs = 120;
constexpr std::uint32_t kJamRecoveryDurationMs = 120;
constexpr float kJamRecoverySpeedRpm = -600.0f;

float ClampNonNegative(float value) { return value > 0.0f ? value : 0.0f; }

constexpr bool IsSingleLikeMode(LoaderModeType mode) {
  return mode == LoaderModeType::kSingle || mode == LoaderModeType::kBurst;
}

constexpr float ResolveMappedBulletSpeedMps(float requested_bullet_speed_mps) {
  if (requested_bullet_speed_mps <= 0.0f) {
    return 0.0f;
  }
  if (requested_bullet_speed_mps <= 16.5f) {
    return 15.0f;
  }
  if (requested_bullet_speed_mps <= 24.0f) {
    return 18.0f;
  }
  return 30.0f;
}

constexpr float ResolveEstimatedHeat(const FireCommand& fire_command) {
  if (fire_command.shooter_heat_limit <= fire_command.remaining_heat) {
    return 0.0f;
  }
  return static_cast<float>(fire_command.shooter_heat_limit -
                            fire_command.remaining_heat);
}

constexpr std::uint8_t ResolveRequestedSingleLikeShotCount(
    LoaderModeType mode, std::uint8_t burst_count) {
  if (mode == LoaderModeType::kSingle) {
    return 1;
  }
  if (mode == LoaderModeType::kBurst) {
    return burst_count > 0 ? burst_count : 1;
  }
  return 0;
}

constexpr float ResolveSingleLikeCadenceHz(LoaderModeType mode,
                                           float requested_shoot_rate_hz) {
  if (!IsSingleLikeMode(mode)) {
    return 0.0f;
  }
  if (requested_shoot_rate_hz > 0.0f) {
    return requested_shoot_rate_hz;
  }
  return mode == LoaderModeType::kBurst ? 10.0f : 8.0f;
}

constexpr std::uint32_t ResolveSingleLikeDeadTimeMs(
    LoaderModeType mode, float requested_shoot_rate_hz) {
  const float cadence_hz =
      ResolveSingleLikeCadenceHz(mode, requested_shoot_rate_hz);
  if (cadence_hz <= 0.0f) {
    return 0U;
  }
  return static_cast<std::uint32_t>((1000.0f / cadence_hz) + 0.999f);
}

constexpr float ResolveTargetWheelSpeedRpm(
    const Config::ShootFrictionConfig& friction_config,
    float requested_bullet_speed_mps) {
  return ResolveMappedBulletSpeedMps(requested_bullet_speed_mps) *
         friction_config.bullet_speed_to_wheel_rpm_scale;
}

}  // namespace

ShootController::ShootController(LibXR::ApplicationManager& appmgr,
                                 const FireCommand& fire_command,
                                 LibXR::Topic& robot_mode_topic,
                                 LibXR::Topic& shoot_state_topic,
                                 DJIMotor& left_friction_motor,
                                 DJIMotor& right_friction_motor,
                                 DJIMotor& loader_motor)
    : ControllerBase(appmgr),
      fire_command_(fire_command),
      shoot_state_topic_(shoot_state_topic),
      left_friction_motor_(left_friction_motor),
      right_friction_motor_(right_friction_motor),
      loader_motor_(loader_motor),
      robot_mode_subscriber_(robot_mode_topic),
      referee_subscriber_(AppConfig::kRefereeStateTopicName) {
  robot_mode_subscriber_.StartWaiting();
  referee_subscriber_.StartWaiting();
}

void ShootController::PullTopicData() {
  if (robot_mode_subscriber_.Available()) {
    robot_mode_ = robot_mode_subscriber_.GetData();
    robot_mode_subscriber_.StartWaiting();
  }

  if (referee_subscriber_.Available()) {
    referee_state_ = referee_subscriber_.GetData();
    referee_subscriber_.StartWaiting();
  }

}

bool ShootController::IsRobotReady() const {
  return robot_mode_.is_enabled && !robot_mode_.is_emergency_stop &&
         robot_mode_.robot_mode != RobotModeType::kSafe;
}

bool ShootController::ShouldEnableFriction(bool robot_ready) const {
  const bool friction_requested =
      fire_command_.friction_enable || fire_command_.fire_enable;
  return robot_ready && robot_mode_.fire_enable && friction_requested;
}

LoaderModeType ShootController::ResolveLoaderMode(bool fire_enabled) const {
  return fire_enabled ? fire_command_.loader_mode : LoaderModeType::kStop;
}

bool ShootController::IsLoaderActive(LoaderModeType mode, bool friction_enabled,
                                     std::uint32_t now_ms) const {
  if (!friction_enabled) {
    return false;
  }

  switch (mode) {
    case LoaderModeType::kSingle:
      return single_step_active_ ||
             (pending_single_shots_ > 0 &&
              now_ms >= next_single_step_allowed_ms_);
    case LoaderModeType::kBurst:
      return single_step_active_ ||
             (pending_single_shots_ > 0 &&
              now_ms >= next_single_step_allowed_ms_);
    case LoaderModeType::kContinuous:
    case LoaderModeType::kReverse:
      return true;
    case LoaderModeType::kStop:
    default:
      return false;
  }
}

void ShootController::ApplyFrictionCommands(bool friction_enabled) {
  if (!friction_enabled) {
    left_friction_motor_.Stop();
    right_friction_motor_.Stop();
    return;
  }

  const float target_bullet_speed_mps =
      ResolveMappedBulletSpeedMps(
          ClampNonNegative(fire_command_.target_bullet_speed_mps));
  const float target_wheel_speed_rpm =
      ResolveTargetWheelSpeedRpm(friction_config_,
                                                      target_bullet_speed_mps);
  const float target_wheel_speed_degps = RpmToDegps(target_wheel_speed_rpm);

  left_friction_motor_.Enable();
  right_friction_motor_.Enable();
  left_friction_motor_.SetOuterLoop(DJIMotorOuterLoop::kSpeed);
  right_friction_motor_.SetOuterLoop(DJIMotorOuterLoop::kSpeed);
  left_friction_motor_.SetReference(target_wheel_speed_degps);
  right_friction_motor_.SetReference(target_wheel_speed_degps);
}

void ShootController::ApplyLoaderCommands(bool friction_enabled,
                                          LoaderModeType loader_mode,
                                          std::uint32_t now_ms) {
  if (!friction_enabled || loader_mode == LoaderModeType::kStop) {
    ResetLoaderSequence();
    jam_recovery_active_ = false;
    jam_stall_start_ms_ = 0;
    loader_motor_.Stop();
    return;
  }

  if (jam_recovery_active_) {
    loader_motor_.Enable();
    loader_motor_.SetOuterLoop(DJIMotorOuterLoop::kSpeed);
    loader_motor_.SetReference(RpmToDegps(kJamRecoverySpeedRpm));
    return;
  }

  if (!IsSingleLikeMode(loader_mode)) {
    ResetLoaderSequence();
    loader_motor_.Enable();
    loader_motor_.SetOuterLoop(DJIMotorOuterLoop::kSpeed);

    if (loader_mode == LoaderModeType::kContinuous) {
      const float continuous_speed_rpm =
          (fire_command_.shoot_rate_hz > 0.0f)
              ? fire_command_.shoot_rate_hz * 360.0f
              : loader_config_.continuous_speed_rpm;
      loader_motor_.SetReference(RpmToDegps(continuous_speed_rpm));
      return;
    }

    const float reverse_speed_rpm =
        (fire_command_.shoot_rate_hz > 0.0f)
            ? (-fire_command_.shoot_rate_hz * 180.0f)
            : loader_config_.reverse_speed_rpm;
    loader_motor_.SetReference(RpmToDegps(reverse_speed_rpm));
    return;
  }

  if (!single_request_latched_) {
    single_request_latched_ = true;
  }

  if (ConsumeShotRequest(loader_mode)) {
    QueueSingleShots(ResolveRequestedSingleLikeShotCount(
        loader_mode, fire_command_.burst_count));
  }

  StepSingleShotSequence(loader_mode, now_ms);
}

void ShootController::ResetLoaderSequence() {
  single_request_latched_ = false;
  single_step_active_ = false;
  pending_single_shots_ = 0;
  loader_target_angle_deg_ = 0.0f;
  next_single_step_allowed_ms_ = 0;
}

bool ShootController::ConsumeShotRequest(LoaderModeType mode) {
  if (!IsSingleLikeMode(mode)) {
    last_shot_request_seq_ = fire_command_.shot_request_seq;
    return false;
  }

  if (fire_command_.shot_request_seq == 0U) {
    return false;
  }

  const bool has_new_request =
      fire_command_.shot_request_seq != last_shot_request_seq_;
  if (has_new_request) {
    last_shot_request_seq_ = fire_command_.shot_request_seq;
  }
  return has_new_request;
}

void ShootController::QueueSingleShots(std::uint8_t shot_count) {
  if (shot_count == 0) {
    return;
  }

  const std::uint16_t queued_shots =
      static_cast<std::uint16_t>(pending_single_shots_) +
      static_cast<std::uint16_t>(shot_count);
  pending_single_shots_ =
      static_cast<std::uint8_t>(std::min<std::uint16_t>(queued_shots, 255));
}

void ShootController::StepSingleShotSequence(LoaderModeType mode,
                                             std::uint32_t now_ms) {
  if (!single_step_active_) {
    if (pending_single_shots_ == 0) {
      loader_motor_.Stop();
      return;
    }
    if (now_ms < next_single_step_allowed_ms_) {
      loader_motor_.Stop();
      return;
    }

    loader_motor_.Enable();
    loader_motor_.SetOuterLoop(DJIMotorOuterLoop::kAngle);
    loader_target_angle_deg_ =
        loader_motor_.GetFeedback().output_total_angle_deg +
        loader_config_.single_step_angle_deg;
    loader_motor_.SetReference(loader_target_angle_deg_);
    single_step_active_ = true;
    return;
  }

  loader_motor_.Enable();
  loader_motor_.SetOuterLoop(DJIMotorOuterLoop::kAngle);
  loader_motor_.SetReference(loader_target_angle_deg_);
  if (!IsLoaderAtTarget()) {
    return;
  }

  single_step_active_ = false;
  if (pending_single_shots_ > 0) {
    --pending_single_shots_;
  }
  next_single_step_allowed_ms_ =
      now_ms + ResolveSingleLikeDeadTimeMs(
                   mode, ClampNonNegative(fire_command_.shoot_rate_hz));

  if (pending_single_shots_ == 0) {
    loader_motor_.Stop();
  }
}

bool ShootController::IsLoaderAtTarget() const {
  const auto& feedback = loader_motor_.GetFeedback();
  if (!feedback.online) {
    return false;
  }

  const float angle_error_deg =
      std::fabs(loader_target_angle_deg_ - feedback.output_total_angle_deg);
  const float speed_abs_degps = std::fabs(feedback.output_speed_degps);
  return angle_error_deg <= kSingleShotAngleToleranceDeg &&
         speed_abs_degps <= kSingleShotSpeedToleranceDegps;
}

float ShootController::ResolveLoaderRateScaleRpm(LoaderModeType mode) const {
  switch (mode) {
    case LoaderModeType::kSingle:
    case LoaderModeType::kBurst:
      return 120.0f;
    case LoaderModeType::kContinuous:
      return 360.0f;
    case LoaderModeType::kReverse:
      return 180.0f;
    case LoaderModeType::kStop:
    default:
      return 0.0f;
  }
}

float ShootController::RpmToDegps(float rpm) { return rpm * kRpmToDegpsScale; }

float ShootController::DegpsToRpm(float degps) {
  return degps / kRpmToDegpsScale;
}

void ShootController::UpdateShootState() {
  const RefereeConstraintView referee_constraints =
      BuildRefereeConstraintView(referee_state_);
  const bool robot_ready = IsRobotReady();
  const bool friction_enabled = ShouldEnableFriction(robot_ready);
  const bool fire_gate_open =
      fire_command_.referee_allows_fire || fire_command_.ignore_referee_fire_gate;
  const bool fire_enabled =
      friction_enabled && fire_command_.fire_enable && robot_mode_.fire_enable &&
      fire_gate_open;
  const LoaderModeType requested_loader_mode = ResolveLoaderMode(fire_enabled);
  const std::uint32_t now_ms = LibXR::Thread::GetTime();
  const auto& loader_feedback = loader_motor_.GetFeedback();

  if (!IsSingleLikeMode(requested_loader_mode)) {
    single_request_latched_ = false;
  }

  if (jam_recovery_active_ && now_ms >= jam_recovery_end_ms_) {
    jam_recovery_active_ = false;
    jam_stall_start_ms_ = 0;
  }

  if (!jam_recovery_active_ && friction_enabled &&
      requested_loader_mode != LoaderModeType::kStop && loader_feedback.online) {
    const bool stalled_loader =
        std::fabs(loader_feedback.output_speed_degps) <=
            kLoaderJammedSpeedToleranceDegps &&
        ((IsSingleLikeMode(requested_loader_mode) &&
          single_step_active_ && !IsLoaderAtTarget()) ||
         requested_loader_mode == LoaderModeType::kContinuous);
    if (stalled_loader) {
      if (jam_stall_start_ms_ == 0U) {
        jam_stall_start_ms_ = now_ms;
      } else if ((now_ms - jam_stall_start_ms_) >= kJamDetectTimeoutMs) {
        jam_recovery_active_ = true;
        jam_recovery_end_ms_ = now_ms + kJamRecoveryDurationMs;
        ResetLoaderSequence();
      }
    } else {
      jam_stall_start_ms_ = 0;
    }
  } else if (!jam_recovery_active_) {
    jam_stall_start_ms_ = 0;
  }

  ApplyLoaderCommands(friction_enabled, requested_loader_mode, now_ms);
  ApplyFrictionCommands(friction_enabled);

  const auto& left_feedback = left_friction_motor_.GetFeedback();
  const auto& right_feedback = right_friction_motor_.GetFeedback();
  const auto& left_state = left_friction_motor_.GetState();
  const auto& right_state = right_friction_motor_.GetState();
  const auto& loader_state = loader_motor_.GetState();

  const bool friction_online = left_feedback.online && right_feedback.online;
  const bool loader_online = loader_feedback.online;
  const bool friction_on = left_state.enabled && right_state.enabled &&
                           !left_state.safe_stopped && !right_state.safe_stopped;
  const LoaderModeType published_loader_mode =
      jam_recovery_active_
          ? LoaderModeType::kReverse
          : (friction_enabled ? requested_loader_mode : LoaderModeType::kStop);
  const bool loader_active =
      loader_state.enabled && !loader_state.safe_stopped &&
      (jam_recovery_active_ ||
       IsLoaderActive(requested_loader_mode, friction_enabled, now_ms));
  const float average_friction_speed_rpm =
      0.5f * (std::fabs(left_feedback.output_speed_rpm) +
              std::fabs(right_feedback.output_speed_rpm));
  const float mapped_bullet_speed_mps =
      ResolveMappedBulletSpeedMps(
          ClampNonNegative(fire_command_.target_bullet_speed_mps));
  const float bullet_speed_mps =
      (friction_config_.bullet_speed_to_wheel_rpm_scale > 0.0f)
          ? (average_friction_speed_rpm /
             friction_config_.bullet_speed_to_wheel_rpm_scale)
          : 0.0f;
  const float loader_rate_scale_rpm =
      ResolveLoaderRateScaleRpm(published_loader_mode);
  const float shoot_rate_hz =
      (IsSingleLikeMode(requested_loader_mode) &&
       fire_enabled && !jam_recovery_active_)
          ? ResolveSingleLikeCadenceHz(
                requested_loader_mode,
                ClampNonNegative(fire_command_.shoot_rate_hz))
          : ((loader_active && loader_rate_scale_rpm > 0.0f)
                 ? (std::fabs(loader_feedback.output_speed_rpm) /
                    loader_rate_scale_rpm)
                 : 0.0f);
  const bool jammed = jam_recovery_active_;

  // 当前 `heat` 已切到 FireCommand 给出的稳定预算摘要；`jammed` 则采用保守的
  // “拨盘长时间低速且未到位 -> 反转恢复”状态机。后续若有更完整的裁判/编码器链路，
  // 再继续增强卡弹判断精度。
  shoot_state_.online = friction_online && loader_online;
  shoot_state_.ready = robot_ready && friction_on && friction_online &&
                       loader_online && !jammed;
  shoot_state_.jammed = jammed;
  shoot_state_.fire_enable = fire_enabled && loader_active && !jammed;
  shoot_state_.loader_mode = published_loader_mode;
  shoot_state_.friction_on = friction_on;
  shoot_state_.loader_active = loader_active;
  shoot_state_.bullet_speed_mps = friction_on ? bullet_speed_mps : 0.0f;
  shoot_state_.target_bullet_speed_mps =
      friction_on ? mapped_bullet_speed_mps : 0.0f;
  shoot_state_.shoot_rate_hz = shoot_rate_hz;
  FireCommand state_fire_command = fire_command_;
  state_fire_command.referee_allows_fire = referee_constraints.referee_allows_fire;
  state_fire_command.shooter_heat_limit = referee_constraints.shooter_heat_limit;
  state_fire_command.remaining_heat = referee_constraints.remaining_heat;
  shoot_state_.heat = ResolveEstimatedHeat(state_fire_command);
  shoot_state_.actuator.friction_online = friction_online;
  shoot_state_.actuator.loader_online = loader_online;
  shoot_state_.actuator.friction_enabled = friction_on;
  shoot_state_.actuator.loader_enabled =
      loader_state.enabled && !loader_state.safe_stopped;
  shoot_state_.actuator.loader_active = loader_active;
  shoot_state_.actuator.friction_left_speed_rpm =
      left_feedback.output_speed_rpm;
  shoot_state_.actuator.friction_right_speed_rpm =
      right_feedback.output_speed_rpm;
  shoot_state_.actuator.loader_speed_rpm = loader_feedback.output_speed_rpm;
}

void ShootController::OnMonitor() {
  PullTopicData();
  UpdateShootState();

  shoot_state_topic_.Publish(shoot_state_);
}

}  // namespace App
