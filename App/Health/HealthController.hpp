#pragma once

#include "../../Modules/CANBridge/CANBridge.hpp"
#include "../../Modules/DT7/DT7.hpp"
#include "../../Modules/MasterMachine/MasterMachine.hpp"
#include "../../Modules/Referee/Referee.hpp"
#include "../../Modules/SuperCap/SuperCap.hpp"
#include "../../Modules/VT13/VT13.hpp"
#include "../Robot/ControllerBase.hpp"
#include "../Topics/ChassisState.hpp"
#include "../Topics/GimbalState.hpp"
#include "../Topics/InsState.hpp"
#include "../Topics/RobotMode.hpp"
#include "../Topics/ShootState.hpp"
#include "../Topics/SystemHealth.hpp"

namespace App {

class HealthController : public ControllerBase {
 public:
  HealthController(LibXR::ApplicationManager& appmgr,
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
                   CANBridge& can_bridge);

  void OnMonitor() override;

 private:
  void PullTopicData();
  SystemHealth BuildSystemHealth();

  LibXR::Topic& system_health_topic_;

  ChassisState chassis_state_{};
  GimbalState gimbal_state_{};
  ShootState shoot_state_{};
  InsState ins_state_{};
  RobotMode robot_mode_{};
  DT7State primary_remote_state_{};
  VT13State secondary_remote_state_{};
  MasterMachineState master_machine_state_{};
  RefereeState referee_state_{};
  SuperCapState super_cap_state_{};
  CANBridgeState can_bridge_state_{};

  bool chassis_has_been_ready_ = false;
  bool gimbal_has_been_ready_ = false;
  bool shoot_has_been_ready_ = false;

  LibXR::Topic::ASyncSubscriber<ChassisState> chassis_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<GimbalState> gimbal_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<ShootState> shoot_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<InsState> ins_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<RobotMode> robot_mode_subscriber_;
  LibXR::Topic::ASyncSubscriber<DT7State> primary_remote_subscriber_;
  LibXR::Topic::ASyncSubscriber<VT13State> secondary_remote_subscriber_;
  LibXR::Topic::ASyncSubscriber<MasterMachineState> master_machine_subscriber_;
  LibXR::Topic::ASyncSubscriber<RefereeState> referee_subscriber_;
  LibXR::Topic::ASyncSubscriber<SuperCapState> super_cap_subscriber_;
  LibXR::Topic::ASyncSubscriber<CANBridgeState> can_bridge_subscriber_;
};

}  // namespace App
