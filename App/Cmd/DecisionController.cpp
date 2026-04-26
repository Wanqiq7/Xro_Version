#include "DecisionController.hpp"

#include <algorithm>

namespace {

constexpr bool ShouldForceSafe(const App::OperatorInputSnapshot& input) {
  return !input.has_active_source || input.request_safe_mode;
}

constexpr App::MotionCommand SafeMotionCommand() { return {}; }
constexpr App::AimCommand SafeAimCommand() { return {}; }
constexpr App::FireCommand SafeFireCommand() { return {}; }

}  // namespace

namespace App {

DecisionController::DecisionController(LibXR::ApplicationManager& appmgr,
                                       const OperatorInputSnapshot& operator_input,
                                       MotionCommand& motion_command,
                                       AimCommand& aim_command,
                                       FireCommand& fire_command,
                                       LibXR::Topic& robot_mode_topic,
                                       Referee& referee)
    : ControllerBase(appmgr),
      operator_input_(operator_input),
      motion_command_(motion_command),
      aim_command_(aim_command),
      fire_command_(fire_command),
      robot_mode_topic_(robot_mode_topic),
      referee_(referee),
      referee_subscriber_(referee_.StateTopic()) {
  referee_subscriber_.StartWaiting();
}

void DecisionController::PullTopicData() {
  if (referee_subscriber_.Available()) {
    referee_state_ = referee_subscriber_.GetData();
    referee_subscriber_.StartWaiting();
  }
}

RobotMode DecisionController::BuildRobotMode(
    const OperatorInputSnapshot& input) const {
  RobotMode robot_mode{};

  if (ShouldForceSafe(input)) {
    robot_mode.robot_mode = RobotModeType::kSafe;
    robot_mode.motion_mode = MotionModeType::kStop;
    robot_mode.aim_mode = AimModeType::kHold;
    robot_mode.fire_enable = false;
    robot_mode.is_enabled = false;
    robot_mode.is_emergency_stop = true;
    robot_mode.control_source = ControlSource::kUnknown;
    return robot_mode;
  }

  if (input.request_manual_mode) {
    robot_mode.robot_mode = RobotModeType::kManual;
    robot_mode.control_source = ControlSource::kRemote;
    robot_mode.motion_mode = MotionModeType::kIndependent;
    robot_mode.aim_mode = AimModeType::kManual;
    robot_mode.fire_enable = input.friction_enabled;
    robot_mode.is_enabled = true;
    robot_mode.is_emergency_stop = false;
    return robot_mode;
  }

  robot_mode.robot_mode = RobotModeType::kSafe;
  robot_mode.motion_mode = MotionModeType::kStop;
  robot_mode.aim_mode = AimModeType::kHold;
  robot_mode.fire_enable = false;
  robot_mode.is_enabled = false;
  robot_mode.is_emergency_stop = false;
  robot_mode.control_source = ControlSource::kUnknown;
  return robot_mode;
}

MotionCommand DecisionController::BuildMotionCommand(
    const OperatorInputSnapshot& input, const RobotMode& robot_mode) const {
  if (!robot_mode.is_enabled || robot_mode.robot_mode != RobotModeType::kManual) {
    return SafeMotionCommand();
  }

  MotionCommand motion_command{};
  motion_command.motion_mode = robot_mode.motion_mode;
  motion_command.vx_mps = input.target_vx_mps;
  motion_command.vy_mps = input.target_vy_mps;
  motion_command.wz_radps = input.target_wz_radps;
  motion_command.is_field_relative = false;
  return motion_command;
}

AimCommand DecisionController::BuildAimCommand(
    const OperatorInputSnapshot& input, const RobotMode& robot_mode) const {
  if (!robot_mode.is_enabled || robot_mode.robot_mode != RobotModeType::kManual) {
    return SafeAimCommand();
  }

  AimCommand aim_command{};
  aim_command.aim_mode = robot_mode.aim_mode;
  aim_command.yaw_deg = input.target_yaw_deg;
  aim_command.pitch_deg = input.target_pitch_deg;
  aim_command.track_target = input.track_target;
  return aim_command;
}

FireCommand DecisionController::BuildFireCommand(
    const OperatorInputSnapshot& input, const RobotMode& robot_mode,
    const RefereeConstraintView& constraints) const {
  if (!robot_mode.is_enabled || robot_mode.robot_mode != RobotModeType::kManual) {
    return SafeFireCommand();
  }

  FireCommand fire_command{};
  fire_command.friction_enable = input.friction_enabled;
  fire_command.fire_enable = input.fire_enabled && input.friction_enabled;
  fire_command.loader_mode =
      fire_command.fire_enable ? LoaderModeType::kSingle : LoaderModeType::kStop;
  fire_command.target_bullet_speed_mps = input.target_bullet_speed_mps;
  fire_command.shoot_rate_hz = input.target_shoot_rate_hz;
  fire_command.referee_allows_fire = constraints.referee_allows_fire;
  fire_command.shooter_heat_limit = constraints.shooter_heat_limit;
  fire_command.remaining_heat = constraints.remaining_heat;
  fire_command.burst_count = fire_command.fire_enable ? 1 : 0;

  if (constraints.referee_online) {
    fire_command.shoot_rate_hz =
        std::min(fire_command.shoot_rate_hz, constraints.max_fire_rate_hz);
  }

  return fire_command;
}

void DecisionController::OnMonitor() {
  PullTopicData();

  const auto& input = operator_input_;
  const RefereeConstraintView constraints = BuildRefereeConstraintView(referee_state_);
  RobotMode robot_mode = BuildRobotMode(input);

  motion_command_ = BuildMotionCommand(input, robot_mode);
  aim_command_ = BuildAimCommand(input, robot_mode);
  fire_command_ = BuildFireCommand(input, robot_mode, constraints);
  robot_mode_topic_.Publish(robot_mode);
}

}  // namespace App
