#pragma once

#include "../../Modules/Referee/Referee.hpp"
#include "../Robot/ControllerBase.hpp"
#include "OperatorInputSnapshot.hpp"
#include "RefereeConstraintView.hpp"
#include "../Topics/AimCommand.hpp"
#include "../Topics/FireCommand.hpp"
#include "../Topics/MotionCommand.hpp"
#include "../Topics/RobotMode.hpp"

namespace App {

class DecisionController : public ControllerBase {
 public:
  DecisionController(LibXR::ApplicationManager& appmgr,
                     const OperatorInputSnapshot& operator_input,
                     MotionCommand& motion_command,
                     AimCommand& aim_command,
                     FireCommand& fire_command,
                     LibXR::Topic& robot_mode_topic,
                     Referee& referee);

  void OnMonitor() override;

 private:
  void PullTopicData();
  RobotMode BuildRobotMode(const OperatorInputSnapshot& input) const;
  MotionCommand BuildMotionCommand(const OperatorInputSnapshot& input,
                                   const RobotMode& robot_mode) const;
  AimCommand BuildAimCommand(const OperatorInputSnapshot& input,
                             const RobotMode& robot_mode) const;
  FireCommand BuildFireCommand(const OperatorInputSnapshot& input,
                               const RobotMode& robot_mode,
                               const RefereeConstraintView& constraints) const;

  const OperatorInputSnapshot& operator_input_;
  MotionCommand& motion_command_;
  AimCommand& aim_command_;
  FireCommand& fire_command_;
  LibXR::Topic& robot_mode_topic_;
  Referee& referee_;
  RefereeState referee_state_{};
  LibXR::Topic::ASyncSubscriber<RefereeState> referee_subscriber_;
};

}  // namespace App
