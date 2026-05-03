#include "MasterMachineBridgeController.hpp"

#include "../Config/AppConfig.hpp"

namespace App {

namespace {

constexpr std::uint32_t kTxIntervalMs = 50;

std::uint8_t ResolveDonorBulletSpeed(float bullet_speed_mps) {
  if (bullet_speed_mps >= 24.0f) {
    return static_cast<std::uint8_t>(MasterMachine::BulletSpeed::kSmallAmmo30);
  }
  if (bullet_speed_mps >= 17.0f) {
    return static_cast<std::uint8_t>(MasterMachine::BulletSpeed::kSmallAmmo18);
  }
  if (bullet_speed_mps >= 15.5f) {
    return static_cast<std::uint8_t>(MasterMachine::BulletSpeed::kBigAmmo16);
  }
  if (bullet_speed_mps >= 12.5f) {
    return static_cast<std::uint8_t>(MasterMachine::BulletSpeed::kSmallAmmo15);
  }
  if (bullet_speed_mps >= 5.0f) {
    return static_cast<std::uint8_t>(MasterMachine::BulletSpeed::kBigAmmo10);
  }
  return static_cast<std::uint8_t>(MasterMachine::BulletSpeed::kNone);
}

}  // namespace

MasterMachineBridgeController::MasterMachineBridgeController(
    LibXR::ApplicationManager& appmgr, MasterMachine& master_machine,
    Referee& referee,
    LibXR::Topic& robot_mode_topic, LibXR::Topic& gimbal_state_topic,
    LibXR::Topic& shoot_state_topic, LibXR::Topic::Domain& app_topic_domain)
    : ControllerBase(appmgr),
      master_machine_(master_machine),
      referee_(referee),
      master_machine_state_subscriber_(master_machine_.StateTopic()),
      referee_subscriber_(referee_.StateTopic()),
      robot_mode_subscriber_(robot_mode_topic),
      gimbal_state_subscriber_(gimbal_state_topic),
      shoot_state_subscriber_(shoot_state_topic),
      ins_state_subscriber_(AppConfig::kInsStateTopicName, &app_topic_domain) {
  master_machine_state_subscriber_.StartWaiting();
  referee_subscriber_.StartWaiting();
  robot_mode_subscriber_.StartWaiting();
  gimbal_state_subscriber_.StartWaiting();
  shoot_state_subscriber_.StartWaiting();
  ins_state_subscriber_.StartWaiting();
}

void MasterMachineBridgeController::PullTopicData() {
  if (master_machine_state_subscriber_.Available()) {
    master_machine_state_ = master_machine_state_subscriber_.GetData();
    master_machine_state_subscriber_.StartWaiting();
  }

  if (referee_subscriber_.Available()) {
    referee_state_ = referee_subscriber_.GetData();
    referee_constraints_ = BuildRefereeConstraintView(referee_state_);
    referee_subscriber_.StartWaiting();
  }

  if (robot_mode_subscriber_.Available()) {
    robot_mode_ = robot_mode_subscriber_.GetData();
    robot_mode_subscriber_.StartWaiting();
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
}

std::uint8_t MasterMachineBridgeController::ResolveDonorEnemyColor() const {
  if (!referee_constraints_.referee_online ||
      !referee_constraints_.self_color_known) {
    return static_cast<std::uint8_t>(MasterMachine::EnemyColor::kNone);
  }

  // donor 字段语义是 enemy_color，因此这里发送的是敌方颜色而不是己方颜色。
  switch (referee_constraints_.enemy_color) {
    case RefereeCampColor::kRed:
      return static_cast<std::uint8_t>(MasterMachine::EnemyColor::kRed);
    case RefereeCampColor::kBlue:
      return static_cast<std::uint8_t>(MasterMachine::EnemyColor::kBlue);
    case RefereeCampColor::kUnknown:
    default:
      break;
  }
  return static_cast<std::uint8_t>(MasterMachine::EnemyColor::kNone);
}

std::uint8_t MasterMachineBridgeController::ResolveDonorWorkMode() const {
  // 当前工程真实落地的主链只有 Safe / Manual，且没有 small buff / big buff
  // 业务入口。为避免把 App 内部模式误投影成 donor 私有枚举，这里显式消费
  // RobotMode，但保守地把所有当前状态统一收口到 AIM。
  switch (robot_mode_.robot_mode) {
    case RobotModeType::kManual:
      switch (robot_mode_.aim_mode) {
        case AimModeType::kManual:
        case AimModeType::kAutoTrack:
        case AimModeType::kHold:
        default:
          return static_cast<std::uint8_t>(MasterMachine::WorkMode::kAim);
      }
    case RobotModeType::kSafe:
    case RobotModeType::kAutoAim:
    case RobotModeType::kAutoMove:
    case RobotModeType::kCalibration:
    default:
      return static_cast<std::uint8_t>(MasterMachine::WorkMode::kAim);
  }
}

ErrorCode MasterMachineBridgeController::SendCurrentStatus() const {
  MasterMachine::DonorTxStatus status{};
  status.enemy_color = ResolveDonorEnemyColor();
  status.work_mode = ResolveDonorWorkMode();
  status.bullet_speed = ResolveDonorBulletSpeed(
      shoot_state_.ready ? shoot_state_.bullet_speed_mps : 0.0f);
  status.yaw =
      (ins_state_.online && ins_state_.gyro_online) ? ins_state_.yaw_deg : 0.0f;
  status.pitch =
      (ins_state_.online && ins_state_.accl_online) ? ins_state_.pitch_deg : 0.0f;
  status.roll =
      (ins_state_.online && ins_state_.accl_online) ? ins_state_.roll_deg : 0.0f;
  return master_machine_.SendDonorTxStatus(status);
}

void MasterMachineBridgeController::OnMonitor() {
  PullTopicData();

  const std::uint32_t now_ms = LibXR::Thread::GetTime();
  if ((now_ms - last_tx_time_ms_) < kTxIntervalMs) {
    return;
  }
  last_tx_time_ms_ = now_ms;

  if (SendCurrentStatus() != ErrorCode::OK) {
    return;
  }
}

}  // namespace App
