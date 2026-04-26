#pragma once

#include <Eigen/Core>
#include <array>

#include "../Config/ChassisConfig.hpp"
#include "../Robot/ControllerBase.hpp"
#include "../Cmd/RefereeConstraintView.hpp"
#include "../Topics/ChassisState.hpp"
#include "../Topics/GimbalState.hpp"
#include "../Topics/MotionCommand.hpp"
#include "../Topics/RobotMode.hpp"
#include "../../Modules/Referee/Referee.hpp"

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
                    DJIMotor& front_left_motor, DJIMotor& front_right_motor,
                    DJIMotor& rear_left_motor, DJIMotor& rear_right_motor);

  void OnMonitor() override;

 private:
  using ImuVector = Eigen::Matrix<float, 3, 1>;
  using WheelMotorArray = std::array<DJIMotor*, 4>;
  using WheelSpeedArray = std::array<float, 4>;

  void PullTopicData();
  void UpdateRelativeYawEstimate(LibXR::MicrosecondTimestamp now_us);
  void UpdateWheelControl();
  void UpdateChassisState(LibXR::MicrosecondTimestamp now_us);

  const MotionCommand& motion_command_;
  LibXR::Topic& chassis_state_topic_;

  RobotMode robot_mode_{};
  MotionCommand applied_motion_command_{};
  ChassisState chassis_state_{};
  GimbalState gimbal_state_{};
  RefereeState referee_state_{};
  WheelMotorArray wheel_motors_;
  WheelSpeedArray target_wheel_speed_radps_{0.0f, 0.0f, 0.0f, 0.0f};
  ImuVector gyro_data_ = ImuVector::Zero();
  ImuVector accl_data_ = ImuVector::Zero();
  LibXR::MicrosecondTimestamp last_yaw_update_us_ = 0;
  LibXR::MicrosecondTimestamp last_estimate_update_us_ = 0;
  VelocityEstimateState estimate_state_{};
  bool has_gyro_sample_ = false;
  bool output_enabled_ = false;
  Config::MecanumChassisConfig actuator_config_ = Config::kMecanumChassisConfig;

  LibXR::Topic::ASyncSubscriber<RobotMode> robot_mode_subscriber_;
  LibXR::Topic::ASyncSubscriber<GimbalState> gimbal_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<RefereeState> referee_subscriber_;
  LibXR::Topic::ASyncSubscriber<ImuVector> gyro_subscriber_;
  LibXR::Topic::ASyncSubscriber<ImuVector> accl_subscriber_;
};

}  // namespace App
