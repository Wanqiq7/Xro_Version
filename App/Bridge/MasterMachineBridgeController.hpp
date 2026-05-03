#pragma once

#include "../../Modules/MasterMachine/MasterMachine.hpp"
#include "../../Modules/Referee/Referee.hpp"
#include "../Robot/ControllerBase.hpp"
#include "../Referee/RefereeConstraintView.hpp"
#include "../Topics/GimbalState.hpp"
#include "../Topics/InsState.hpp"
#include "../Topics/RobotMode.hpp"
#include "../Topics/ShootState.hpp"

namespace App {

class MasterMachineBridgeController : public ControllerBase {
 public:
  MasterMachineBridgeController(LibXR::ApplicationManager& appmgr,
                                MasterMachine& master_machine,
                                Referee& referee,
                                LibXR::Topic& robot_mode_topic,
                                LibXR::Topic& gimbal_state_topic,
                                LibXR::Topic& shoot_state_topic,
                                LibXR::Topic::Domain& app_topic_domain);

  void OnMonitor() override;

 private:
  void PullTopicData();
  ErrorCode SendCurrentStatus() const;
  std::uint8_t ResolveDonorEnemyColor() const;
  std::uint8_t ResolveDonorWorkMode() const;

  MasterMachine& master_machine_;
  Referee& referee_;
  MasterMachineState master_machine_state_{};
  RefereeState referee_state_{};
  RefereeConstraintView referee_constraints_{};
  RobotMode robot_mode_{};
  GimbalState gimbal_state_{};
  ShootState shoot_state_{};
  InsState ins_state_{};
  std::uint32_t last_tx_time_ms_ = 0;

  LibXR::Topic::ASyncSubscriber<MasterMachineState> master_machine_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<RefereeState> referee_subscriber_;
  LibXR::Topic::ASyncSubscriber<RobotMode> robot_mode_subscriber_;
  LibXR::Topic::ASyncSubscriber<GimbalState> gimbal_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<ShootState> shoot_state_subscriber_;
  LibXR::Topic::ASyncSubscriber<InsState> ins_state_subscriber_;
};

}  // namespace App
