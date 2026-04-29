#include "InputController.hpp"

#include <cstdint>

#include "../Config/BridgeConfig.hpp"
#include "../Config/InputConfig.hpp"
#include "../../Modules/DT7/DT7.hpp"
#include "../../Modules/VT13/VT13.hpp"

namespace {

App::RemoteSwitchPosition MapDT7Switch(::RemoteSwitchPosition position) {
  switch (position) {
    case ::RemoteSwitchPosition::kUp:
      return App::RemoteSwitchPosition::kUp;
    case ::RemoteSwitchPosition::kMiddle:
      return App::RemoteSwitchPosition::kMiddle;
    case ::RemoteSwitchPosition::kDown:
      return App::RemoteSwitchPosition::kDown;
    case ::RemoteSwitchPosition::kUnknown:
    default:
      return App::RemoteSwitchPosition::kUnknown;
  }
}

App::VT13Mode MapVT13Mode(::VT13Mode mode) {
  switch (mode) {
    case ::VT13Mode::kC:
      return App::VT13Mode::kC;
    case ::VT13Mode::kN:
      return App::VT13Mode::kN;
    case ::VT13Mode::kS:
      return App::VT13Mode::kS;
    case ::VT13Mode::kErr:
    default:
      return App::VT13Mode::kErr;
  }
}

std::int16_t ScaleUnitToStick(float value) {
  if (value > 1.0f) {
    value = 1.0f;
  } else if (value < -1.0f) {
    value = -1.0f;
  }
  return static_cast<std::int16_t>(value * App::InputConfig::kStickMaxMagnitude);
}

App::DT7InputState ToDT7InputState(const DT7State& state) {
  return App::DT7InputState{
      .online = state.online,
      .left_switch = MapDT7Switch(state.left_switch),
      .right_switch = MapDT7Switch(state.right_switch),
      .left_x = state.left_x,
      .left_y = state.left_y,
      .right_x = state.right_x,
      .right_y = state.right_y,
      .dial = state.dial,
      .mouse_x_delta = state.mouse_x,
      .mouse_y_delta = state.mouse_y,
      .mouse_left_button = state.mouse_left_pressed,
      .mouse_right_button = state.mouse_right_pressed,
      .keyboard_key_code = state.keyboard_key_code,
  };
}

App::VT13InputState ToVT13InputState(const VT13State& state) {
  return App::VT13InputState{
      .online = state.online,
      .mode = MapVT13Mode(state.mode),
      .x = ScaleUnitToStick(state.x),
      .y = ScaleUnitToStick(state.y),
      .yaw = ScaleUnitToStick(state.yaw),
      .pitch = ScaleUnitToStick(state.pitch),
      .wheel = ScaleUnitToStick(state.wheel),
      .trigger = state.trigger,
      .fn = state.fn,
      .photo = state.photo,
      .mouse_left_button = state.mouse.left,
      .mouse_right_button = state.mouse.right,
      .key_w = state.key.w,
      .key_s = state.key.s,
      .key_a = state.key.a,
      .key_d = state.key.d,
      .key_shift = state.key.shift,
      .key_z = state.key.z,
      .key_e = state.key.e,
      .key_r = state.key.r,
      .key_f = state.key.f,
      .key_c = state.key.c,
  };
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
                                 VT13& secondary_remote_control,
                                 MasterMachine& master_machine)
    : ControllerBase(appmgr),
      operator_input_(operator_input),
      primary_remote_control_(primary_remote_control),
      secondary_remote_control_(secondary_remote_control),
      master_machine_(master_machine),
      primary_remote_subscriber_(primary_remote_control_.StateTopic()),
      secondary_remote_subscriber_(secondary_remote_control_.StateTopic()),
      master_machine_subscriber_(master_machine_.StateTopic()) {
  primary_remote_subscriber_.StartWaiting();
  secondary_remote_subscriber_.StartWaiting();
  master_machine_subscriber_.StartWaiting();
}

void InputController::OnMonitor() {
  Update();
}

void InputController::Update() {
  PullInputTopicData();

  auto& input = operator_input_;
  input = BuildSafeInputSnapshot();

  const auto selection = SelectActiveInput();
  ResetInactiveInputLatches(selection);
  ApplySelectedInput(selection, input);

  if (ShouldUseMasterMachineOverride()) {
    ApplyMasterMachineOverride(input);
    input.has_active_source = true;
  } else {
    ResetMasterMachineOverrideLatch();
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

  if (secondary_remote_subscriber_.Available()) {
    secondary_remote_state_ = secondary_remote_subscriber_.GetData();
    secondary_remote_subscriber_.StartWaiting();
  }

  if (master_machine_subscriber_.Available()) {
    master_machine_state_ = master_machine_subscriber_.GetData();
    master_machine_subscriber_.StartWaiting();
  }
}

InputController::InputSelection InputController::SelectActiveInput() const {
  switch (SelectInputSource(primary_remote_state_.online,
                            secondary_remote_state_.online)) {
    case InputSourceSelection::primary_dt7:
      return InputSelection::kPrimaryDT7;
    case InputSourceSelection::secondary_vt13:
      return InputSelection::kSecondaryVT13;
    case InputSourceSelection::none:
    default:
      return InputSelection::kNone;
  }
}

void InputController::ResetInactiveInputLatches(InputSelection selection) {
  if (selection != InputSelection::kPrimaryDT7) {
    ResetInputLatch(primary_latch_);
  }

  if (selection != InputSelection::kSecondaryVT13) {
    ResetInputLatch(secondary_latch_);
  }
}

void InputController::ResetMasterMachineOverrideLatch() const {
  master_machine_fire_request_latched_ = false;
}

bool InputController::ShouldUseMasterMachineOverride() const {
  return BuildMasterMachineCoordinationView(master_machine_.Config(),
                                            master_machine_state_)
      .manual_override_active;
}

void InputController::ApplySelectedInput(InputSelection selection,
                                         OperatorInputSnapshot& input) const {
  switch (selection) {
    case InputSelection::kPrimaryDT7:
      ApplyDT7Input(ToDT7InputState(primary_remote_state_), primary_latch_,
                    input);
      break;
    case InputSelection::kSecondaryVT13:
      ApplyVT13Input(ToVT13InputState(secondary_remote_state_),
                     secondary_latch_, input);
      break;
    case InputSelection::kNone:
    default:
      break;
  }
}

void InputController::ApplyMasterMachineOverride(
    OperatorInputSnapshot& input) const {
  const bool vision_target_allowed =
      MasterMachineFieldEnabled(Config::MasterMachineInputField::kVisionTarget);
  const bool vision_target_active =
      vision_target_allowed && master_machine_state_.online &&
      master_machine_state_.target_state != MasterMachineTargetState::kNoTarget;
  const bool fire_field_allowed =
      MasterMachineFieldEnabled(Config::MasterMachineInputField::kFireEnable);
  const bool auto_fire_requested =
      fire_field_allowed &&
      master_machine_state_.fire_mode == MasterMachineFireMode::kAutoFire;
  const bool fire_edge_requested =
      fire_field_allowed && master_machine_state_.request_fire_enable;
  const bool fire_request_rising_edge =
      fire_edge_requested && !master_machine_fire_request_latched_;

  master_machine_fire_request_latched_ = fire_edge_requested;

  input.control_source = ControlSource::kHost;
  input.request_safe_mode = false;
  input.request_manual_mode = true;
  input.requested_loader_mode = LoaderModeType::kStop;
  input.requested_burst_count = 0;
  input.shot_request_seq = master_machine_shot_request_seq_;
  input.trigger_pressed = fire_edge_requested;
  input.fire_enabled = auto_fire_requested || fire_edge_requested;

  if (auto_fire_requested) {
    input.requested_loader_mode = LoaderModeType::kContinuous;
  } else if (fire_edge_requested) {
    input.requested_loader_mode = LoaderModeType::kSingle;
    if (fire_request_rising_edge) {
      ++master_machine_shot_request_seq_;
      input.shot_request_seq = master_machine_shot_request_seq_;
    }
  }

  input.friction_enabled = input.fire_enabled;
  input.target_bullet_speed_mps =
      input.friction_enabled ? InputConfig::kDefaultBulletSpeedMps
                             : 0.0f;
  input.target_shoot_rate_hz =
      input.fire_enabled ? InputConfig::kDefaultShootRateHz : 0.0f;
  input.target_vx_mps =
      MasterMachineFieldEnabled(Config::MasterMachineInputField::kTargetVx)
          ? master_machine_state_.target_vx_mps
          : 0.0f;
  input.target_vy_mps =
      MasterMachineFieldEnabled(Config::MasterMachineInputField::kTargetVy)
          ? master_machine_state_.target_vy_mps
          : 0.0f;
  input.target_wz_radps = 0.0f;
  input.target_yaw_deg = 0.0f;
  input.target_pitch_deg = 0.0f;

  if (vision_target_active) {
    input.target_yaw_deg = master_machine_state_.vision_yaw_deg;
    input.target_pitch_deg = master_machine_state_.vision_pitch_deg;
    input.track_target = true;
    return;
  }

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
