#include "GimbalController.hpp"

#include "../Config/AppConfig.hpp"
#include "../Config/GimbalConfig.hpp"
#include "../../Modules/DJIMotor/DJIMotor.hpp"
#include "../../Modules/DMMotor/DMMotor.hpp"
#include "GimbalMath.hpp"

namespace App {

namespace {

constexpr float kDegToRad = GimbalMath::kDegToRad;

float ClampPitchCommandDeg(float pitch_deg) {
  if (pitch_deg < Config::kPitchCommandMinDeg) {
    return Config::kPitchCommandMinDeg;
  }
  if (pitch_deg > Config::kPitchCommandMaxDeg) {
    return Config::kPitchCommandMaxDeg;
  }
  return pitch_deg;
}

}  // namespace

GimbalController::GimbalController(LibXR::ApplicationManager& appmgr,
                                   const AimCommand& aim_command,
                                   LibXR::Topic& robot_mode_topic,
                                   LibXR::Topic& gimbal_state_topic,
                                   LibXR::Topic::Domain& app_topic_domain,
                                   DJIMotor& yaw_motor, DMMotor& pitch_motor)
    : ControllerBase(appmgr),
      aim_command_(aim_command),
      gimbal_state_topic_(gimbal_state_topic),
      yaw_motor_(yaw_motor),
      pitch_motor_(pitch_motor),
      robot_mode_subscriber_(robot_mode_topic),
      ins_state_subscriber_(AppConfig::kInsStateTopicName, &app_topic_domain) {
  robot_mode_subscriber_.StartWaiting();
  ins_state_subscriber_.StartWaiting();
}

void GimbalController::PullTopicData() {
  if (robot_mode_subscriber_.Available()) {
    robot_mode_ = robot_mode_subscriber_.GetData();
    robot_mode_subscriber_.StartWaiting();
  }

  if (ins_state_subscriber_.Available()) {
    ins_state_ = ins_state_subscriber_.GetData();
    ins_state_subscriber_.StartWaiting();
  }
}

bool GimbalController::IsRobotReady() const {
  return robot_mode_.is_enabled && !robot_mode_.is_emergency_stop &&
         robot_mode_.robot_mode != RobotModeType::kSafe;
}

void GimbalController::SyncYawMotorStateSummary() {
  const DJIMotorFeedback& feedback = yaw_motor_.GetFeedback();
  const DJIMotorState& motor_state = yaw_motor_.GetState();

  yaw_state_.feedback_online = feedback.online;
  yaw_state_.feedback_initialized = feedback.initialized;
  yaw_state_.enabled = motor_state.enabled;
  yaw_state_.safe_stopped = motor_state.safe_stopped;
}

void GimbalController::SyncPitchMotorStateSummary() {
  const DMMotorFeedback& feedback = pitch_motor_.GetFeedback();
  const DMMotorState& motor_state = pitch_motor_.GetState();
  pitch_state_.feedback_online = feedback.online;
  pitch_state_.feedback_initialized = feedback.initialized;
  pitch_state_.enabled = motor_state.enabled;
  pitch_state_.safe_stopped = motor_state.safe_stopped;
}

void GimbalController::ApplyYawMotorControl(bool robot_ready) {
  const bool feedback_ready = ins_state_.online && ins_state_.gyro_online;
  const bool output_enabled = robot_ready && feedback_ready;
  yaw_state_.commanded_position = aim_command_.yaw_deg;
  yaw_state_.measured_position = ins_state_.yaw_deg;
  yaw_state_.measured_rate = ins_state_.yaw_rate_degps;

  // yaw 使用统一 INS 外反馈注入；反馈未在线时禁止把默认零值送入闭环。
  yaw_motor_.SetExternalAngleFeedbackDeg(yaw_state_.measured_position);
  yaw_motor_.SetExternalSpeedFeedbackDegps(yaw_state_.measured_rate);

  if (!output_enabled) {
    yaw_motor_.Stop();
    SyncYawMotorStateSummary();
    yaw_state_.status = AxisStatus::kDisabled;
    return;
  }

  yaw_motor_.Enable();
  yaw_motor_.SetOuterLoop(DJIMotorOuterLoop::kAngle);
  yaw_motor_.ChangeFeed(DJIMotorOuterLoop::kAngle,
                        DJIMotorFeedbackSource::kExternal);
  yaw_motor_.ChangeFeed(DJIMotorOuterLoop::kSpeed,
                        DJIMotorFeedbackSource::kExternal);
  yaw_motor_.SetReference(yaw_state_.commanded_position);

  SyncYawMotorStateSummary();
  yaw_state_.status = yaw_state_.feedback_online
                          ? AxisStatus::kTracking
                          : AxisStatus::kWaitingFeedback;
}

void GimbalController::ApplyPitchMotorControl(bool robot_ready) {
  const float clamped_pitch_deg = ClampPitchCommandDeg(aim_command_.pitch_deg);
  const bool feedback_ready =
      ins_state_.online && ins_state_.gyro_online && ins_state_.accl_online;
  const bool output_enabled = robot_ready && feedback_ready;
  pitch_state_.commanded_position = clamped_pitch_deg;
  pitch_state_.measured_position = ins_state_.pitch_deg;
  pitch_state_.measured_rate = ins_state_.pitch_rate_degps;

  pitch_motor_.SetExternalAngleFeedbackRad(ins_state_.pitch_deg * kDegToRad);
  pitch_motor_.SetExternalSpeedFeedbackRadps(ins_state_.pitch_rate_degps *
                                             kDegToRad);

  if (!output_enabled) {
    pitch_motor_.Stop();
    SyncPitchMotorStateSummary();
    pitch_state_.status = AxisStatus::kDisabled;
    return;
  }

  pitch_motor_.Enable();
  pitch_motor_.SetOuterLoop(DMMotorOuterLoop::kAngle);
  pitch_motor_.ChangeFeed(DMMotorOuterLoop::kAngle,
                          DMMotorFeedbackSource::kExternal);
  pitch_motor_.ChangeFeed(DMMotorOuterLoop::kSpeed,
                          DMMotorFeedbackSource::kExternal);
  pitch_motor_.SetReference(clamped_pitch_deg * kDegToRad);

  SyncPitchMotorStateSummary();
  pitch_state_.status = pitch_state_.feedback_online
                            ? AxisStatus::kTracking
                            : AxisStatus::kWaitingFeedback;
}

void GimbalController::UpdateGimbalState(bool robot_ready) {
  const bool yaw_feedback_ready = ins_state_.online && ins_state_.gyro_online;
  const bool pitch_feedback_ready =
      ins_state_.online && ins_state_.gyro_online && ins_state_.accl_online;
  const bool yaw_tracking = yaw_state_.status == AxisStatus::kTracking;
  const bool pitch_tracking =
      pitch_state_.status == AxisStatus::kTracking;
  const bool yaw_ready = yaw_feedback_ready && yaw_state_.feedback_online &&
                         yaw_state_.feedback_initialized && yaw_tracking &&
                         yaw_state_.enabled && !yaw_state_.safe_stopped;
  const bool pitch_ready = pitch_feedback_ready && pitch_state_.feedback_online &&
                           pitch_state_.feedback_initialized &&
                           pitch_tracking && pitch_state_.enabled &&
                           !pitch_state_.safe_stopped;

  gimbal_state_.online = yaw_state_.feedback_online ||
                         pitch_state_.feedback_online || yaw_state_.enabled ||
                         pitch_state_.enabled || ins_state_.online;
  gimbal_state_.ready =
      robot_ready && ins_state_.online && yaw_ready && pitch_ready;
  gimbal_state_.yaw_deg = ins_state_.yaw_deg;
  gimbal_state_.pitch_deg = ins_state_.pitch_deg;
  gimbal_state_.yaw_rate_degps = ins_state_.yaw_rate_degps;
  gimbal_state_.pitch_rate_degps = ins_state_.pitch_rate_degps;
  gimbal_state_.relative_yaw_deg = GimbalMath::ComputeRelativeYawDeg(
      yaw_motor_.GetFeedback().output_angle_deg, Config::kYawChassisAlignDeg,
      Config::kYawRelativeDirection);
  gimbal_state_.target_yaw_deg = yaw_state_.commanded_position;
  gimbal_state_.target_pitch_deg = pitch_state_.commanded_position;
  gimbal_state_.yaw_error_deg = GimbalMath::WrapAngleDeg(
      gimbal_state_.target_yaw_deg - gimbal_state_.yaw_deg);
  gimbal_state_.pitch_error_deg =
      gimbal_state_.target_pitch_deg - gimbal_state_.pitch_deg;
}

void GimbalController::OnMonitor() {
  PullTopicData();

  const bool robot_ready = IsRobotReady();
  ApplyYawMotorControl(robot_ready);
  ApplyPitchMotorControl(robot_ready);
  UpdateGimbalState(robot_ready);

  gimbal_state_topic_.Publish(gimbal_state_);
}

}  // namespace App
