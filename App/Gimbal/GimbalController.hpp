#pragma once

#include "../Robot/ControllerBase.hpp"
#include "../Topics/AimCommand.hpp"
#include "../Topics/GimbalState.hpp"
#include "../Topics/InsState.hpp"
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
 * 负责消费统一 INS 姿态、桥接控制与状态发布。
 */
class GimbalController : public ControllerBase {
 public:
  GimbalController(LibXR::ApplicationManager& appmgr,
                   const AimCommand& aim_command,
                   LibXR::Topic& robot_mode_topic,
                   LibXR::Topic& gimbal_state_topic,
                   LibXR::Topic::Domain& app_topic_domain,
                   DJIMotor& yaw_motor, DMMotor& pitch_motor);

  void OnMonitor() override;

 private:
  using YawControlState = AxisState;

  void PullTopicData();
  bool IsRobotReady() const;
  void SyncYawMotorStateSummary();
  void SyncPitchMotorStateSummary();
  float ComputeManualControlDeltaTimeSeconds();
  float ResolveYawCommandDeg(bool robot_ready, bool feedback_ready, float dt_s);
  float ResolvePitchCommandDeg(bool robot_ready, bool feedback_ready,
                               float dt_s);
  void ApplyYawMotorControl(bool robot_ready, float dt_s);
  void ApplyPitchMotorControl(bool robot_ready, float dt_s);
  void UpdateGimbalState(bool robot_ready);

  const AimCommand& aim_command_;
  LibXR::Topic& gimbal_state_topic_;
  RobotMode robot_mode_{};
  InsState ins_state_{};
  GimbalState gimbal_state_{};
  DJIMotor& yaw_motor_;
  DMMotor& pitch_motor_;
  YawControlState yaw_state_{};
  AxisState pitch_state_{};
  float manual_yaw_target_deg_ = 0.0f;
  float manual_pitch_target_deg_ = 0.0f;
  bool manual_yaw_target_initialized_ = false;
  bool manual_pitch_target_initialized_ = false;
  LibXR::MicrosecondTimestamp last_manual_control_time_us_ = 0;

  LibXR::Topic::ASyncSubscriber<RobotMode> robot_mode_subscriber_;
  LibXR::Topic::ASyncSubscriber<InsState> ins_state_subscriber_;
};

}  // namespace App
