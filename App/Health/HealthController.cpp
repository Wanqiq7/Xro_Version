#include "HealthController.hpp"

#include <cstdint>

namespace {

constexpr std::uint32_t ToFlag(App::SystemHealthFault fault) {
  return static_cast<std::uint32_t>(fault);
}

constexpr std::uint32_t ToFlag(App::SystemHealthWarning warning) {
  return static_cast<std::uint32_t>(warning);
}

constexpr App::SafeAction MaxSafeAction(App::SafeAction lhs,
                                        App::SafeAction rhs) {
  return static_cast<std::uint8_t>(lhs) >= static_cast<std::uint8_t>(rhs)
             ? lhs
             : rhs;
}

}  // namespace

namespace App {

HealthController::HealthController(LibXR::ApplicationManager& appmgr,
                                   LibXR::Topic& system_health_topic,
                                   LibXR::Topic& chassis_state_topic,
                                   LibXR::Topic& gimbal_state_topic,
                                   LibXR::Topic& shoot_state_topic,
                                   LibXR::Topic& ins_state_topic,
                                   LibXR::Topic& robot_mode_topic,
                                   DT7& primary_remote_control,
                                   VT13& secondary_remote_control,
                                   MasterMachine& master_machine,
                                   Referee& referee,
                                   SuperCap& super_cap,
                                   CANBridge& can_bridge)
    : ControllerBase(appmgr),
      system_health_topic_(system_health_topic),
      chassis_state_subscriber_(chassis_state_topic),
      gimbal_state_subscriber_(gimbal_state_topic),
      shoot_state_subscriber_(shoot_state_topic),
      ins_state_subscriber_(ins_state_topic),
      robot_mode_subscriber_(robot_mode_topic),
      primary_remote_subscriber_(primary_remote_control.StateTopic()),
      secondary_remote_subscriber_(secondary_remote_control.StateTopic()),
      master_machine_subscriber_(master_machine.StateTopic()),
      referee_subscriber_(referee.StateTopic()),
      super_cap_subscriber_(super_cap.StateTopic()),
      can_bridge_subscriber_(can_bridge.StateTopic()) {
  chassis_state_subscriber_.StartWaiting();
  gimbal_state_subscriber_.StartWaiting();
  shoot_state_subscriber_.StartWaiting();
  ins_state_subscriber_.StartWaiting();
  robot_mode_subscriber_.StartWaiting();
  primary_remote_subscriber_.StartWaiting();
  secondary_remote_subscriber_.StartWaiting();
  master_machine_subscriber_.StartWaiting();
  referee_subscriber_.StartWaiting();
  super_cap_subscriber_.StartWaiting();
  can_bridge_subscriber_.StartWaiting();
}

void HealthController::PullTopicData() {
  if (chassis_state_subscriber_.Available()) {
    chassis_state_ = chassis_state_subscriber_.GetData();
    chassis_state_subscriber_.StartWaiting();
  }

  if (gimbal_state_subscriber_.Available()) {
    gimbal_state_ = gimbal_state_subscriber_.GetData();
    gimbal_state_subscriber_.StartWaiting();
  }

  if (shoot_state_subscriber_.Available()) {
    shoot_state_ = shoot_state_subscriber_.GetData();
    shoot_state_subscriber_.StartWaiting();
  }

  if (ins_state_subscriber_.Available()) {
    ins_state_ = ins_state_subscriber_.GetData();
    ins_state_subscriber_.StartWaiting();
  }

  if (robot_mode_subscriber_.Available()) {
    robot_mode_ = robot_mode_subscriber_.GetData();
    robot_mode_subscriber_.StartWaiting();
  }

  if (primary_remote_subscriber_.Available()) {
    primary_remote_state_ = primary_remote_subscriber_.GetData();
    primary_remote_subscriber_.StartWaiting();
  }

  if (secondary_remote_subscriber_.Available()) {
    secondary_remote_state_ = secondary_remote_subscriber_.GetData();
    secondary_remote_subscriber_.StartWaiting();
  }

  if (master_machine_subscriber_.Available()) {
    master_machine_state_ = master_machine_subscriber_.GetData();
    master_machine_subscriber_.StartWaiting();
  }

  if (referee_subscriber_.Available()) {
    referee_state_ = referee_subscriber_.GetData();
    referee_subscriber_.StartWaiting();
  }

  if (super_cap_subscriber_.Available()) {
    super_cap_state_ = super_cap_subscriber_.GetData();
    super_cap_subscriber_.StartWaiting();
  }

  if (can_bridge_subscriber_.Available()) {
    can_bridge_state_ = can_bridge_subscriber_.GetData();
    can_bridge_subscriber_.StartWaiting();
  }
}

SystemHealth HealthController::BuildSystemHealth() {
  SystemHealth health{};
  health.online = true;
  health.input_online = primary_remote_state_.online ||
                        secondary_remote_state_.online ||
                        master_machine_state_.online;
  health.ins_online =
      ins_state_.online && ins_state_.gyro_online && ins_state_.accl_online;
  health.chassis_ready = chassis_state_.ready;
  health.gimbal_ready = gimbal_state_.ready;
  health.shoot_ready = shoot_state_.ready;
  health.referee_online = referee_state_.online;
  health.super_cap_online = super_cap_state_.online;
  health.can_bridge_online = can_bridge_state_.online;

  if (health.chassis_ready) {
    chassis_has_been_ready_ = true;
  }
  if (health.gimbal_ready) {
    gimbal_has_been_ready_ = true;
  }
  if (health.shoot_ready) {
    shoot_has_been_ready_ = true;
  }

  if (!health.input_online) {
    health.fault_flags |= ToFlag(SystemHealthFault::kInputLost);
    health.safe_action =
        MaxSafeAction(health.safe_action, SafeAction::kEmergencyStop);
    health.emergency_stop_requested = true;
  }

  if (!health.ins_online) {
    health.fault_flags |= ToFlag(SystemHealthFault::kInsLost);
    health.safe_action =
        MaxSafeAction(health.safe_action, SafeAction::kEmergencyStop);
    health.emergency_stop_requested = true;
  }

  const bool actuator_output_expected =
      robot_mode_.is_enabled && !robot_mode_.is_emergency_stop;
  const bool shoot_output_expected = actuator_output_expected &&
                                     robot_mode_.fire_enable;

  if (actuator_output_expected && chassis_has_been_ready_ &&
      !health.chassis_ready) {
    health.fault_flags |= ToFlag(SystemHealthFault::kChassisFault);
    health.safe_action =
        MaxSafeAction(health.safe_action, SafeAction::kEmergencyStop);
    health.emergency_stop_requested = true;
  }

  if (actuator_output_expected && gimbal_has_been_ready_ &&
      !health.gimbal_ready) {
    health.fault_flags |= ToFlag(SystemHealthFault::kGimbalFault);
    health.safe_action =
        MaxSafeAction(health.safe_action, SafeAction::kEmergencyStop);
    health.emergency_stop_requested = true;
  }

  if ((shoot_output_expected && shoot_has_been_ready_ && !health.shoot_ready) ||
      shoot_state_.jammed) {
    health.fault_flags |= ToFlag(SystemHealthFault::kShootFault);
    // 发射链路故障只关闭发射能力，不直接要求底盘/云台急停。
    health.safe_action =
        MaxSafeAction(health.safe_action, SafeAction::kDisableShoot);
  }

  if (!health.referee_online) {
    health.warning_flags |= ToFlag(SystemHealthWarning::kRefereeLost);
  }

  if (!health.super_cap_online) {
    health.warning_flags |= ToFlag(SystemHealthWarning::kSuperCapLost);
    health.safe_action =
        MaxSafeAction(health.safe_action, SafeAction::kLimitOutput);
  }

  if (!health.can_bridge_online) {
    health.warning_flags |= ToFlag(SystemHealthWarning::kCanBridgeLost);
  }

  if (health.emergency_stop_requested) {
    health.level = HealthLevel::kEmergencyStop;
  } else if (health.fault_flags != 0u) {
    health.level = HealthLevel::kFault;
  } else if (health.warning_flags != 0u) {
    health.level = HealthLevel::kWarning;
  } else {
    health.level = HealthLevel::kOk;
  }

  health.buzzer_alarm_requested =
      health.level == HealthLevel::kFault ||
      health.level == HealthLevel::kEmergencyStop;
  health.last_update_ms =
      static_cast<std::uint32_t>(LibXR::Timebase::GetMilliseconds());
  return health;
}

void HealthController::OnMonitor() {
  PullTopicData();
  SystemHealth health = BuildSystemHealth();
  system_health_topic_.Publish(health);
}

}  // namespace App
