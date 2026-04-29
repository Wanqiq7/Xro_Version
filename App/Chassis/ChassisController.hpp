#pragma once

#include <array>

#include "ChassisPowerLimiter.hpp"
#include "../Config/ChassisConfig.hpp"
#include "../Robot/ControllerBase.hpp"
#include "../Cmd/RefereeConstraintView.hpp"
#include "../Topics/ChassisState.hpp"
#include "../Topics/GimbalState.hpp"
#include "../Topics/InsState.hpp"
#include "../Topics/MotionCommand.hpp"
#include "../Topics/RobotMode.hpp"
#include "../../Modules/Referee/Referee.hpp"
#include "../../Modules/SuperCap/SuperCap.hpp"

class DJIMotor;

namespace App {

struct VelocityEstimateState {
  float vx_mps = 0.0f;
  float vy_mps = 0.0f;
  float wz_radps = 0.0f;
};

class ChassisController : public ControllerBase {
 public:
  ChassisController(LibXR::ApplicationManager& appmgr,
                    const MotionCommand& motion_command,
                    LibXR::Topic& robot_mode_topic,
                    LibXR::Topic& gimbal_state_topic,
                    LibXR::Topic& chassis_state_topic,
                    LibXR::Topic::Domain& app_topic_domain,
                    DJIMotor& front_left_motor, DJIMotor& front_right_motor,
                    DJIMotor& rear_left_motor, DJIMotor& rear_right_motor,
                    SuperCap& super_cap);

  void OnMonitor() override;

 private:
  using WheelMotorArray = std::array<DJIMotor*, 4>;
  using WheelSpeedArray = std::array<float, 4>;

  void PullTopicData();
  void UpdateForceControl(const RefereeConstraintView& referee);
  void UpdateChassisState(LibXR::MicrosecondTimestamp now_us);
  void SendSuperCapCommand(const RefereeConstraintView& referee);

  const MotionCommand& motion_command_;
  LibXR::Topic& chassis_state_topic_;

  RobotMode robot_mode_{};
  MotionCommand applied_motion_command_{};
  ChassisState chassis_state_{};
  GimbalState gimbal_state_{};
  InsState ins_state_{};
  RefereeState referee_state_{};
  SuperCapState super_cap_state_{};
  WheelMotorArray wheel_motors_;
  LibXR::MicrosecondTimestamp last_estimate_update_us_ = 0;
  LibXR::MicrosecondTimestamp last_force_update_us_ = 0;
  VelocityEstimateState estimate_state_{};
  bool output_enabled_ = false;
  Config::MecanumChassisConfig actuator_config_ = Config::kMecanumChassisConfig;
  Config::ChassisPowerLimiterConfig power_limiter_config_ =
      Config::kChassisPowerLimiterConfig;
  Config::ChassisForceControlConfig force_config_ =
      Config::kChassisForceControlConfig;
  ChassisPowerLimiter power_limiter_{};
  SuperCap* super_cap_ = nullptr;
  std::uint32_t super_cap_cmd_counter_ = 0;

  LibXR::PID<float> force_x_pid_;
  LibXR::PID<float> force_y_pid_;
  LibXR::PID<float> torque_z_pid_;
  VelocityEstimateState force_estimate_state_{};
  std::array<float, 4> target_wheel_omega_radps_{};
  std::array<float, 4> wheel_tau_ref_nm_{};

  LibXR::Topic::ASyncSubscriber<RobotMode> robot_mode_subscriber_;
  LibXR::Topic::ASyncSubscriber<GimbalState> gimbal_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<InsState> ins_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<RefereeState> referee_subscriber_;
  LibXR::Topic::ASyncSubscriber<SuperCapState> super_cap_subscriber_;
};

}  // namespace App
