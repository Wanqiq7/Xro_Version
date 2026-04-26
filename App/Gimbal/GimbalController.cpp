#include "GimbalController.hpp"

#include <cmath>

#include "../Config/AppConfig.hpp"
#include "../../Modules/DJIMotor/DJIMotor.hpp"
#include "../../Modules/DMMotor/DMMotor.hpp"

namespace App {

namespace {

constexpr float kRadToDeg = 57.29577951308232f;
constexpr float kDegToRad = 0.017453292519943295f;
constexpr float kMaxYawIntegrationStepS = 0.05f;
constexpr float kMaxPitchIntegrationStepS = 0.05f;
constexpr float kPitchComplementaryAlpha = 0.98f;

float WrapAngleDeg(float angle_deg) {
  while (angle_deg > 180.0f) {
    angle_deg -= 360.0f;
  }
  while (angle_deg < -180.0f) {
    angle_deg += 360.0f;
  }
  return angle_deg;
}

float ClampPitchDeg(float pitch_deg) {
  if (pitch_deg > 45.0f) {
    return 45.0f;
  }
  if (pitch_deg < -45.0f) {
    return -45.0f;
  }
  return pitch_deg;
}

float EstimatePitchFromAcclDeg(const Eigen::Matrix<float, 3, 1>& accl_data) {
  const float ax = accl_data.x();
  const float ay = accl_data.y();
  const float az = accl_data.z();
  const float horizontal_norm = std::sqrt(ax * ax + az * az);
  if (horizontal_norm <= 1e-5f && std::fabs(ay) <= 1e-5f) {
    return 0.0f;
  }
  return std::atan2(-ay, horizontal_norm) * kRadToDeg;
}

}  // namespace

GimbalController::GimbalController(LibXR::ApplicationManager& appmgr,
                                   const AimCommand& aim_command,
                                   LibXR::Topic& robot_mode_topic,
                                   LibXR::Topic& gimbal_state_topic,
                                   DJIMotor& yaw_motor, DMMotor& pitch_motor)
    : ControllerBase(appmgr),
      aim_command_(aim_command),
      gimbal_state_topic_(gimbal_state_topic),
      yaw_motor_(yaw_motor),
      pitch_motor_(pitch_motor),
      robot_mode_subscriber_(robot_mode_topic),
      gyro_subscriber_(AppConfig::kBmi088GyroTopicName),
      accl_subscriber_(AppConfig::kBmi088AcclTopicName) {
  robot_mode_subscriber_.StartWaiting();
  gyro_subscriber_.StartWaiting();
  accl_subscriber_.StartWaiting();
}

void GimbalController::PullTopicData() {
  if (robot_mode_subscriber_.Available()) {
    robot_mode_ = robot_mode_subscriber_.GetData();
    robot_mode_subscriber_.StartWaiting();
  }

  if (gyro_subscriber_.Available()) {
    gyro_data_ = gyro_subscriber_.GetData();
    gyro_subscriber_.StartWaiting();
    has_gyro_sample_ = true;
  }

  if (accl_subscriber_.Available()) {
    accl_data_ = accl_subscriber_.GetData();
    accl_subscriber_.StartWaiting();
    has_accl_sample_ = true;
  }
}

void GimbalController::UpdateRelativeYawEstimate() {
  const auto now_us = LibXR::Timebase::GetMicroseconds();
  if (has_gyro_sample_ && last_yaw_update_us_ != 0) {
    const float dt_s = (now_us - last_yaw_update_us_).ToSecondf();
    if (dt_s > 0.0f && dt_s <= kMaxYawIntegrationStepS) {
      relative_yaw_deg_ =
          WrapAngleDeg(relative_yaw_deg_ + gyro_data_.z() * dt_s * kRadToDeg);
    }
  }
  last_yaw_update_us_ = now_us;
}

void GimbalController::UpdateRelativePitchEstimate() {
  const auto now_us = LibXR::Timebase::GetMicroseconds();
  if (has_gyro_sample_ && last_pitch_update_us_ != 0) {
    const float dt_s = (now_us - last_pitch_update_us_).ToSecondf();
    if (dt_s > 0.0f && dt_s <= kMaxPitchIntegrationStepS) {
      relative_pitch_deg_ =
          ClampPitchDeg(relative_pitch_deg_ + gyro_data_.y() * dt_s * kRadToDeg);
    }
  }

  if (has_accl_sample_) {
    const float accl_pitch_deg = EstimatePitchFromAcclDeg(accl_data_);
    if (last_pitch_update_us_ == 0) {
      relative_pitch_deg_ = accl_pitch_deg;
    } else {
      relative_pitch_deg_ = ClampPitchDeg(kPitchComplementaryAlpha *
                                              relative_pitch_deg_ +
                                          (1.0f - kPitchComplementaryAlpha) *
                                              accl_pitch_deg);
    }
  }

  last_pitch_update_us_ = now_us;
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
  yaw_state_.commanded_position = aim_command_.yaw_deg;
  yaw_state_.measured_position = relative_yaw_deg_;
  yaw_state_.measured_rate = gyro_data_.z() * kRadToDeg;

  // yaw 继续保持原桥接主线：外反馈注入 + 角度参考。
  yaw_motor_.SetExternalAngleFeedbackDeg(yaw_state_.measured_position);
  yaw_motor_.SetExternalSpeedFeedbackDegps(yaw_state_.measured_rate);

  if (!robot_ready) {
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
  pitch_state_.commanded_position = aim_command_.pitch_deg;
  pitch_state_.measured_position = relative_pitch_deg_;
  pitch_state_.measured_rate = gyro_data_.y() * kRadToDeg;

  pitch_motor_.SetExternalAngleFeedbackRad(relative_pitch_deg_ * kDegToRad);
  pitch_motor_.SetExternalSpeedFeedbackRadps(gyro_data_.y());

  if (!robot_ready) {
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
  pitch_motor_.SetReference(aim_command_.pitch_deg * kDegToRad);

  SyncPitchMotorStateSummary();
  pitch_state_.status = pitch_state_.feedback_online
                            ? AxisStatus::kTracking
                            : AxisStatus::kWaitingFeedback;
}

void GimbalController::UpdateGimbalState(bool robot_ready) {
  const bool yaw_tracking = yaw_state_.status == AxisStatus::kTracking;
  const bool pitch_tracking =
      pitch_state_.status == AxisStatus::kTracking;
  const bool yaw_ready = yaw_state_.feedback_online &&
                         yaw_state_.feedback_initialized && yaw_tracking &&
                         yaw_state_.enabled && !yaw_state_.safe_stopped;
  const bool pitch_ready = pitch_state_.feedback_online &&
                           pitch_state_.feedback_initialized &&
                           pitch_tracking && pitch_state_.enabled &&
                           !pitch_state_.safe_stopped;

  // 当前 `yaw_deg / yaw_rate_degps` 代表 controller 基于 BMI088 构造的真实观测量：
  // 1. `yaw_deg` 是由 z 轴角速度积分得到的相对 yaw；
  // 2. `yaw_rate_degps` 是 z 轴角速度的实时观测；
  // 3. `pitch_deg / pitch_rate_degps` 现在同样由 BMI088 外反馈估计驱动真实执行链。
  gimbal_state_.online = yaw_state_.feedback_online ||
                         pitch_state_.feedback_online || yaw_state_.enabled ||
                         pitch_state_.enabled || has_gyro_sample_ ||
                         has_accl_sample_;
  gimbal_state_.ready =
      robot_ready && yaw_ready && pitch_ready && has_gyro_sample_;
  gimbal_state_.yaw_deg = relative_yaw_deg_;
  gimbal_state_.pitch_deg = relative_pitch_deg_;
  gimbal_state_.yaw_rate_degps = gyro_data_.z() * kRadToDeg;
  gimbal_state_.pitch_rate_degps = gyro_data_.y() * kRadToDeg;
}

void GimbalController::OnMonitor() {
  PullTopicData();
  UpdateRelativeYawEstimate();
  UpdateRelativePitchEstimate();

  const bool robot_ready = IsRobotReady();
  ApplyYawMotorControl(robot_ready);
  ApplyPitchMotorControl(robot_ready);
  UpdateGimbalState(robot_ready);

  gimbal_state_topic_.Publish(gimbal_state_);
}

}  // namespace App
