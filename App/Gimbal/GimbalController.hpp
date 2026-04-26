#pragma once

#include <Eigen/Core>

#include "../Robot/ControllerBase.hpp"
#include "../Topics/AimCommand.hpp"
#include "../Topics/GimbalState.hpp"
#include "../Topics/RobotMode.hpp"
#include "libxr.hpp"

class DJIMotor;
class DMMotor;

namespace App {

enum class AxisStatus : uint8_t {
  kDisabled = 0,
  kWaitingFeedback,
  kTracking,
};

struct AxisState {
  AxisStatus status = AxisStatus::kDisabled;
  float commanded_position = 0.0f;
  float measured_position = 0.0f;
  float measured_rate = 0.0f;
  bool feedback_online = false;
  bool feedback_initialized = false;
  bool enabled = false;
  bool safe_stopped = true;
};

/**
 * @brief 云台控制器
 *
 * 负责云台姿态估计、桥接控制与状态发布。
 */
class GimbalController : public ControllerBase {
 public:
  GimbalController(LibXR::ApplicationManager& appmgr,
                   const AimCommand& aim_command,
                   LibXR::Topic& robot_mode_topic,
                   LibXR::Topic& gimbal_state_topic,
                   DJIMotor& yaw_motor, DMMotor& pitch_motor);

  void OnMonitor() override;

 private:
  using ImuVector = Eigen::Matrix<float, 3, 1>;
  using YawControlState = AxisState;

  void PullTopicData();
  void UpdateRelativeYawEstimate();
  void UpdateRelativePitchEstimate();
  bool IsRobotReady() const;
  void SyncYawMotorStateSummary();
  void SyncPitchMotorStateSummary();
  void ApplyYawMotorControl(bool robot_ready);
  void ApplyPitchMotorControl(bool robot_ready);
  void UpdateGimbalState(bool robot_ready);

  const AimCommand& aim_command_;
  LibXR::Topic& gimbal_state_topic_;
  RobotMode robot_mode_{};
  GimbalState gimbal_state_{};
  DJIMotor& yaw_motor_;
  DMMotor& pitch_motor_;
  YawControlState yaw_state_{};
  AxisState pitch_state_{};
  ImuVector gyro_data_ = ImuVector::Zero();
  ImuVector accl_data_ = ImuVector::Zero();
  float relative_yaw_deg_ = 0.0f;
  float relative_pitch_deg_ = 0.0f;
  bool has_gyro_sample_ = false;
  bool has_accl_sample_ = false;
  LibXR::MicrosecondTimestamp last_yaw_update_us_ = 0;
  LibXR::MicrosecondTimestamp last_pitch_update_us_ = 0;

  LibXR::Topic::ASyncSubscriber<RobotMode> robot_mode_subscriber_;
  LibXR::Topic::ASyncSubscriber<ImuVector> gyro_subscriber_;
  LibXR::Topic::ASyncSubscriber<ImuVector> accl_subscriber_;
};

}  // namespace App
