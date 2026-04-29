#pragma once

#include <cstdint>

#include "../../Modules/DJIMotor/DJIMotor.hpp"
#include "../../Modules/Referee/Referee.hpp"
#include "../Config/ShootConfig.hpp"
#include "../Robot/ControllerBase.hpp"
#include "../Cmd/RefereeConstraintView.hpp"
#include "../Topics/FireCommand.hpp"
#include "../Topics/RobotMode.hpp"
#include "../Topics/ShootState.hpp"

namespace App {

/**
 * @brief 发射控制器
 *
 * 负责发射机构相关的状态管理、触发条件与节奏控制。
 */
class ShootController : public ControllerBase {
 public:
  ShootController(LibXR::ApplicationManager& appmgr,
                  const FireCommand& fire_command,
                  LibXR::Topic& robot_mode_topic,
                  LibXR::Topic& shoot_state_topic,
                  DJIMotor& left_friction_motor, DJIMotor& right_friction_motor,
                  DJIMotor& loader_motor);

  void OnMonitor() override;

 private:
  void PullTopicData();
  void UpdateShootState();
  bool IsRobotReady() const;
  bool ShouldEnableFriction(bool robot_ready) const;
  LoaderModeType ResolveLoaderMode(bool fire_enabled) const;
  bool IsLoaderActive(LoaderModeType mode, bool friction_enabled,
                      std::uint32_t now_ms) const;
  void ApplyFrictionCommands(bool friction_enabled);
  void ApplyLoaderCommands(bool friction_enabled, LoaderModeType loader_mode,
                           std::uint32_t now_ms);
  void ResetLoaderSequence();
  bool ConsumeShotRequest(LoaderModeType mode);
  void QueueSingleShots(std::uint8_t shot_count);
  void StepSingleShotSequence(LoaderModeType mode, std::uint32_t now_ms);
  bool IsLoaderAtTarget() const;
  float ResolveLoaderRateScaleRpm(LoaderModeType mode) const;
  static float RpmToDegps(float rpm);
  static float DegpsToRpm(float degps);

  const FireCommand& fire_command_;
  LibXR::Topic& shoot_state_topic_;
  RobotMode robot_mode_{};
  ShootState shoot_state_{};
  RefereeState referee_state_{};
  DJIMotor& left_friction_motor_;
  DJIMotor& right_friction_motor_;
  DJIMotor& loader_motor_;
  Config::ShootFrictionConfig friction_config_ = Config::kShootFrictionConfig;
  Config::ShootLoaderConfig loader_config_ = Config::kShootLoaderConfig;
  bool single_request_latched_ = false;
  bool single_step_active_ = false;
  std::uint8_t pending_single_shots_ = 0;
  std::uint32_t last_shot_request_seq_ = 0;
  float loader_target_angle_deg_ = 0.0f;
  std::uint32_t next_single_step_allowed_ms_ = 0;
  bool jam_recovery_active_ = false;
  std::uint32_t jam_stall_start_ms_ = 0;
  std::uint32_t jam_recovery_end_ms_ = 0;

  LibXR::Topic::ASyncSubscriber<RobotMode> robot_mode_subscriber_;
  LibXR::Topic::ASyncSubscriber<RefereeState> referee_subscriber_;
};

}  // namespace App
