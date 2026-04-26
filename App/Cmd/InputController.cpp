#include "InputController.hpp"

#include <cstdint>

#include "../Config/BridgeConfig.hpp"
#include "../../Modules/RemoteControl/DT7.hpp"
#include "../../Modules/RemoteControl/VT13.hpp"

namespace {

constexpr float kStickMaxMagnitude = 660.0f;
constexpr float kMaxYawDeg = 180.0f;
constexpr float kMaxPitchDeg = 30.0f;
constexpr float kMaxChassisLinearSpeedMps = 2.0f;
constexpr float kDefaultBulletSpeedMps = 30.0f;
constexpr float kDefaultShootRateHz = 8.0f;

inline float NormalizeStick(int16_t value) {
  if (value > kStickMaxMagnitude) {
    return 1.0f;
  }
  if (value < -kStickMaxMagnitude) {
    return -1.0f;
  }
  return static_cast<float>(value) / kStickMaxMagnitude;
}

inline bool IsSafeRequested(const DT7State& state) {
  return !state.online ||
         state.right_switch == RemoteSwitchPosition::kDown ||
         state.right_switch == RemoteSwitchPosition::kUnknown;
}

inline bool IsManualRequested(const DT7State& state) {
  return state.online && !IsSafeRequested(state);
}

inline bool IsFrictionEnabled(const DT7State& state) {
  return IsManualRequested(state) &&
         state.left_switch == RemoteSwitchPosition::kUp;
}

inline bool IsFireEnabled(const DT7State& state) {
  return IsFrictionEnabled(state) && state.mouse_left_pressed;
}

inline bool IsSafeRequestedVT13(const VT13State& state) {
  return !state.online || state.mode == VT13Mode::kC ||
         state.mode == VT13Mode::kErr;
}

inline bool IsManualRequestedVT13(const VT13State& state) {
  return state.online &&
         (state.mode == VT13Mode::kN || state.mode == VT13Mode::kS);
}

inline bool IsFrictionEnabledVT13(const VT13State& state) {
  return state.online && state.mode == VT13Mode::kS;
}

inline bool IsFireEnabledVT13(const VT13State& state) {
  return IsFrictionEnabledVT13(state) && state.trigger;
}

inline void ApplySafeSnapshot(App::OperatorInputSnapshot& input) {
  input.has_active_source = false;
  input.request_safe_mode = true;
  input.request_manual_mode = false;
  input.fire_enabled = false;
  input.friction_enabled = false;
  input.target_bullet_speed_mps = 0.0f;
  input.target_shoot_rate_hz = 0.0f;
  input.target_vx_mps = 0.0f;
  input.target_vy_mps = 0.0f;
  input.target_wz_radps = 0.0f;
  input.target_yaw_deg = 0.0f;
  input.target_pitch_deg = 0.0f;
  input.track_target = false;
}

inline void ApplyDT7Snapshot(const DT7State& remote_state,
                             App::OperatorInputSnapshot& input) {
  input.request_safe_mode = IsSafeRequested(remote_state);
  input.request_manual_mode = IsManualRequested(remote_state);
  input.fire_enabled = IsFireEnabled(remote_state);
  input.friction_enabled = IsFrictionEnabled(remote_state);
  input.target_bullet_speed_mps =
      input.friction_enabled ? kDefaultBulletSpeedMps : 0.0f;
  input.target_shoot_rate_hz = input.fire_enabled ? kDefaultShootRateHz : 0.0f;

  input.target_vx_mps =
      NormalizeStick(remote_state.left_y) * kMaxChassisLinearSpeedMps;
  input.target_vy_mps =
      NormalizeStick(remote_state.left_x) * kMaxChassisLinearSpeedMps;
  input.target_wz_radps = 0.0f;

  input.target_yaw_deg =
      NormalizeStick(remote_state.right_x) * kMaxYawDeg;
  input.target_pitch_deg =
      -NormalizeStick(remote_state.right_y) * kMaxPitchDeg;
  input.track_target = false;
}

inline void ApplyVT13Snapshot(const VT13State& vt13_state,
                              App::OperatorInputSnapshot& input) {
  input.request_safe_mode = IsSafeRequestedVT13(vt13_state);
  input.request_manual_mode = IsManualRequestedVT13(vt13_state);
  input.fire_enabled = IsFireEnabledVT13(vt13_state);
  input.friction_enabled = IsFrictionEnabledVT13(vt13_state);
  input.target_bullet_speed_mps =
      input.friction_enabled ? kDefaultBulletSpeedMps : 0.0f;
  input.target_shoot_rate_hz = input.fire_enabled ? kDefaultShootRateHz : 0.0f;

  input.target_vx_mps = vt13_state.y * kMaxChassisLinearSpeedMps;
  input.target_vy_mps = vt13_state.x * kMaxChassisLinearSpeedMps;
  input.target_wz_radps = 0.0f;

  input.target_yaw_deg = vt13_state.yaw * kMaxYawDeg;
  input.target_pitch_deg = vt13_state.pitch * kMaxPitchDeg;
  input.track_target = false;
}

struct MasterMachineCoordinationView {
  App::Config::MasterMachineIngressMode ingress_mode =
      App::Config::MasterMachineIngressMode::kDisabled;
  bool manual_override_allowed = false;
  bool manual_override_active = false;
};

constexpr MasterMachineCoordinationView BuildMasterMachineCoordinationView(
    const App::Config::MasterMachineConfig& config,
    const MasterMachineState& master_machine_state) {
  const bool manual_override_allowed =
      config.ingress_mode ==
          App::Config::MasterMachineIngressMode::kManualOverrideOnly &&
      App::Config::MasterMachineFieldEnabled(
          config, App::Config::MasterMachineInputField::kManualOverride);
  return MasterMachineCoordinationView{
      .ingress_mode = config.ingress_mode,
      .manual_override_allowed = manual_override_allowed,
      .manual_override_active = manual_override_allowed &&
                                master_machine_state.online &&
                                master_machine_state.request_manual_override,
  };
}

}  // namespace

namespace App {

InputController::InputController(LibXR::ApplicationManager& appmgr,
                                 OperatorInputSnapshot& operator_input,
                                 DT7& primary_remote_control,
                                 DT7* secondary_remote_control, VT13& vt13,
                                 MasterMachine& master_machine)
    : ControllerBase(appmgr),
      operator_input_(operator_input),
      primary_remote_control_(primary_remote_control),
      secondary_remote_control_(secondary_remote_control),
      vt13_(vt13),
      master_machine_(master_machine),
      primary_remote_subscriber_(primary_remote_control_.StateTopic()),
      vt13_subscriber_(vt13_.StateTopic()),
      master_machine_subscriber_(master_machine_.StateTopic()) {
  if (secondary_remote_control_ != nullptr) {
    secondary_remote_subscriber_ =
        std::make_unique<LibXR::Topic::ASyncSubscriber<DT7State>>(
            secondary_remote_control_->StateTopic());
    secondary_remote_subscriber_->StartWaiting();
  }

  primary_remote_subscriber_.StartWaiting();
  vt13_subscriber_.StartWaiting();
  master_machine_subscriber_.StartWaiting();
}

void InputController::OnMonitor() {
  Update();
}

void InputController::Update() {
  PullInputTopicData();

  auto& input = operator_input_;
  ApplySafeSnapshot(input);
  input.has_active_source = false;

  const auto selection = SelectActiveInput();
  ApplySelectedInput(selection, input);
  input.has_active_source = selection != InputSelection::kNone;

  if (ShouldUseMasterMachineOverride()) {
    ApplyMasterMachineOverride(input);
    input.has_active_source = true;
  }
}

bool InputController::MasterMachineFieldEnabled(
    Config::MasterMachineInputField field) const {
  return Config::MasterMachineFieldEnabled(master_machine_.Config(), field);
}

void InputController::PullInputTopicData() {
  if (primary_remote_subscriber_.Available()) {
    primary_remote_state_ = primary_remote_subscriber_.GetData();
    primary_remote_subscriber_.StartWaiting();
  }

  if (secondary_remote_subscriber_ != nullptr &&
      secondary_remote_subscriber_->Available()) {
    secondary_remote_state_ = secondary_remote_subscriber_->GetData();
    secondary_remote_subscriber_->StartWaiting();
  }

  if (vt13_subscriber_.Available()) {
    vt13_state_ = vt13_subscriber_.GetData();
    vt13_subscriber_.StartWaiting();
  }

  if (master_machine_subscriber_.Available()) {
    master_machine_state_ = master_machine_subscriber_.GetData();
    master_machine_subscriber_.StartWaiting();
  }
}

InputController::InputSelection InputController::SelectActiveInput() const {
  if (primary_remote_state_.online) {
    return InputSelection::kPrimary;
  }

  if (secondary_remote_state_.online) {
    return InputSelection::kSecondary;
  }

  if (vt13_state_.online) {
    return InputSelection::kVT13;
  }

  return InputSelection::kNone;
}

bool InputController::ShouldUseMasterMachineOverride() const {
  return BuildMasterMachineCoordinationView(master_machine_.Config(),
                                            master_machine_state_)
      .manual_override_active;
}

void InputController::ApplySelectedInput(InputSelection selection,
                                         OperatorInputSnapshot& input) const {
  switch (selection) {
    case InputSelection::kPrimary:
      ApplyDT7Snapshot(primary_remote_state_, input);
      break;
    case InputSelection::kSecondary:
      ApplyDT7Snapshot(secondary_remote_state_, input);
      break;
    case InputSelection::kVT13:
      ApplyVT13Snapshot(vt13_state_, input);
      break;
    case InputSelection::kNone:
    default:
      break;
  }
}

void InputController::ApplyMasterMachineOverride(
    OperatorInputSnapshot& input) const {
  input.request_safe_mode = false;
  input.request_manual_mode = true;
  input.fire_enabled =
      MasterMachineFieldEnabled(Config::MasterMachineInputField::kFireEnable) &&
      master_machine_state_.request_fire_enable;
  input.friction_enabled = input.fire_enabled;
  input.target_bullet_speed_mps =
      input.friction_enabled ? kDefaultBulletSpeedMps
                             : 0.0f;
  input.target_shoot_rate_hz =
      input.fire_enabled ? kDefaultShootRateHz : 0.0f;
  input.target_vx_mps =
      MasterMachineFieldEnabled(Config::MasterMachineInputField::kTargetVx)
          ? master_machine_state_.target_vx_mps
          : 0.0f;
  input.target_vy_mps =
      MasterMachineFieldEnabled(Config::MasterMachineInputField::kTargetVy)
          ? master_machine_state_.target_vy_mps
          : 0.0f;
  input.target_wz_radps = 0.0f;
  input.target_yaw_deg =
      MasterMachineFieldEnabled(Config::MasterMachineInputField::kTargetYaw)
          ? master_machine_state_.target_yaw_deg
          : 0.0f;
  input.target_pitch_deg =
      MasterMachineFieldEnabled(Config::MasterMachineInputField::kTargetPitch)
          ? master_machine_state_.target_pitch_deg
          : 0.0f;
  input.track_target = false;
}

}  // namespace App
