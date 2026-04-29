#include <cassert>

#include "App/Cmd/OperatorInputSnapshot.hpp"
#include "App/Cmd/RemoteInputMapper.hpp"

namespace {

void ExpectDefaultSnapshotIsSafe() {
  const App::OperatorInputSnapshot snapshot;

  assert(!snapshot.has_active_source);
  assert(snapshot.control_source == App::ControlSource::kUnknown);
  assert(snapshot.request_safe_mode);
  assert(!snapshot.request_manual_mode);
  assert(snapshot.emergency_latched);

  assert(snapshot.requested_motion_mode == App::MotionModeType::kStop);
  assert(snapshot.requested_aim_mode == App::AimModeType::kHold);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(snapshot.requested_burst_count == 0);
  assert(!snapshot.lid_open);
  assert(snapshot.chassis_speed_scale == 0.0f);
  assert(!snapshot.power_boost_requested);

  assert(snapshot.yaw_delta_deg == 0.0f);
  assert(snapshot.pitch_delta_deg == 0.0f);
  assert(!snapshot.fire_enabled);
  assert(!snapshot.friction_enabled);
  assert(snapshot.target_vx_mps == 0.0f);
  assert(snapshot.target_vy_mps == 0.0f);
  assert(snapshot.target_wz_radps == 0.0f);
  assert(!snapshot.track_target);

  assert(!snapshot.manual_keys.z);
  assert(!snapshot.manual_keys.e);
  assert(!snapshot.manual_keys.r);
  assert(!snapshot.manual_keys.f);
  assert(!snapshot.manual_keys.c);
  assert(!snapshot.manual_mouse.left_button);
  assert(!snapshot.manual_mouse.right_button);
  assert(!snapshot.manual_toggles.fire_pressed);
}

void ExpectPrimarySourceWinsWhenOnline() {
  assert(App::SelectInputSource(true, false) ==
         App::InputSourceSelection::primary_dt7);
  assert(App::SelectInputSource(true, true) ==
         App::InputSourceSelection::primary_dt7);
}

void ExpectSecondarySourceOnlyWhenPrimaryOffline() {
  assert(App::SelectInputSource(false, true) ==
         App::InputSourceSelection::secondary_vt13);
}

void ExpectNoSourceWhenBothOffline() {
  assert(App::SelectInputSource(false, false) ==
         App::InputSourceSelection::none);
}

void ExpectDT7DialEmergencyLatchAndRightSwitchUpClears() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::DT7InputState input;
  input.online = true;
  input.left_switch = App::RemoteSwitchPosition::kDown;
  input.right_switch = App::RemoteSwitchPosition::kDown;
  input.dial = 400;

  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.emergency_latched);
  assert(snapshot.request_safe_mode);

  input.dial = 0;
  input.right_switch = App::RemoteSwitchPosition::kUp;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(!snapshot.emergency_latched);
  assert(!snapshot.request_safe_mode);
  assert(snapshot.requested_motion_mode == App::MotionModeType::kIndependent);
}

void ExpectDT7KeyEdgesToggleAndDoNotRepeatWhileHeld() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::DT7InputState input;
  input.online = true;
  input.left_switch = App::RemoteSwitchPosition::kUp;
  input.right_switch = App::RemoteSwitchPosition::kUp;

  input.keyboard_key_code =
      App::kDt7KeyF | App::kDt7KeyZ | App::kDt7KeyE | App::kDt7KeyC |
      App::kDt7KeyR;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.friction_enabled);
  assert(snapshot.target_bullet_speed_mps == 18.0f);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kSingle);
  assert(snapshot.chassis_speed_scale == 0.6f);
  assert(snapshot.lid_open);

  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.friction_enabled);
  assert(snapshot.target_bullet_speed_mps == 18.0f);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kSingle);
  assert(snapshot.chassis_speed_scale == 0.6f);
  assert(snapshot.lid_open);

  input.keyboard_key_code = 0;
  App::ApplyDT7Input(input, latch, snapshot);
  input.keyboard_key_code =
      App::kDt7KeyF | App::kDt7KeyZ | App::kDt7KeyE | App::kDt7KeyC |
      App::kDt7KeyR;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(!snapshot.friction_enabled);
  assert(snapshot.target_bullet_speed_mps == 30.0f);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kBurst);
  assert(snapshot.chassis_speed_scale == 0.8f);
  assert(!snapshot.lid_open);
}

void ExpectDT7KeyboardMouseMotionAndDeltas() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::DT7InputState input;
  input.online = true;
  input.left_switch = App::RemoteSwitchPosition::kUp;
  input.right_switch = App::RemoteSwitchPosition::kDown;
  input.keyboard_key_code = App::kDt7KeyW | App::kDt7KeyA | App::kDt7KeyShift;
  input.mouse_x_delta = 330;
  input.mouse_y_delta = -330;
  input.mouse_left_button = true;

  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.control_source == App::ControlSource::kKeyboardMouse);
  assert(snapshot.target_vx_mps > 0.0f);
  assert(snapshot.target_vy_mps > 0.0f);
  assert(snapshot.yaw_delta_deg > 0.0f);
  assert(snapshot.pitch_delta_deg < 0.0f);
  assert(snapshot.power_boost_requested);
  assert(snapshot.trigger_pressed);
  assert(snapshot.fire_enabled);
}

void ExpectOfflineClearsDT7ExecutionLatches() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::DT7InputState input;
  input.online = true;
  input.left_switch = App::RemoteSwitchPosition::kUp;
  input.right_switch = App::RemoteSwitchPosition::kUp;
  input.keyboard_key_code =
      App::kDt7KeyF | App::kDt7KeyE | App::kDt7KeyR;

  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.friction_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kSingle);
  assert(snapshot.lid_open);

  input.online = false;
  input.keyboard_key_code = 0;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.request_safe_mode);
  assert(snapshot.emergency_latched);
  assert(!snapshot.friction_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(!snapshot.lid_open);

  input.online = true;
  input.right_switch = App::RemoteSwitchPosition::kUp;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(!snapshot.friction_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(!snapshot.lid_open);
}

void ExpectVT13ModesAndRequests() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::VT13InputState input;
  input.online = true;
  input.mode = App::VT13Mode::kN;
  input.x = 330;
  input.y = -330;
  input.yaw = 100;
  input.pitch = -100;
  input.wheel = 250;

  App::ApplyVT13Input(input, latch, snapshot);
  assert(snapshot.has_active_source);
  assert(snapshot.request_manual_mode);
  assert(!snapshot.request_safe_mode);
  assert(!snapshot.friction_enabled);
  assert(snapshot.target_vx_mps > 0.0f);
  assert(snapshot.target_vy_mps < 0.0f);
  assert(snapshot.target_wz_radps > 0.0f);

  input.mode = App::VT13Mode::kS;
  input.trigger = true;
  input.photo = true;
  App::ApplyVT13Input(input, latch, snapshot);
  assert(snapshot.friction_enabled);
  assert(snapshot.trigger_pressed);
  assert(snapshot.fire_enabled);
  assert(snapshot.request_auto_aim);

  input.mode = App::VT13Mode::kC;
  input.fn = true;
  App::ApplyVT13Input(input, latch, snapshot);
  assert(snapshot.request_safe_mode);
  assert(snapshot.request_calibration);

  input.mode = App::VT13Mode::kErr;
  input.fn = false;
  input.photo = false;
  App::ApplyVT13Input(input, latch, snapshot);
  assert(snapshot.request_safe_mode);
  assert(!snapshot.request_manual_mode);
  assert(!snapshot.fire_enabled);
}

void ExpectOfflineClearsVT13ExecutionLatches() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::VT13InputState input;
  input.online = true;
  input.mode = App::VT13Mode::kS;
  input.key_f = true;
  input.key_e = true;
  input.key_r = true;

  App::ApplyVT13Input(input, latch, snapshot);
  assert(snapshot.friction_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kSingle);
  assert(snapshot.lid_open);

  input.online = false;
  input.key_f = false;
  input.key_e = false;
  input.key_r = false;
  App::ApplyVT13Input(input, latch, snapshot);
  assert(snapshot.request_safe_mode);
  assert(snapshot.emergency_latched);
  assert(!snapshot.friction_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(!snapshot.lid_open);

  input.online = true;
  input.mode = App::VT13Mode::kN;
  App::ApplyVT13Input(input, latch, snapshot);
  assert(!snapshot.friction_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(!snapshot.lid_open);
}

}  // namespace

int main() {
  ExpectDefaultSnapshotIsSafe();
  ExpectPrimarySourceWinsWhenOnline();
  ExpectSecondarySourceOnlyWhenPrimaryOffline();
  ExpectNoSourceWhenBothOffline();
  ExpectDT7DialEmergencyLatchAndRightSwitchUpClears();
  ExpectDT7KeyEdgesToggleAndDoNotRepeatWhileHeld();
  ExpectDT7KeyboardMouseMotionAndDeltas();
  ExpectOfflineClearsDT7ExecutionLatches();
  ExpectVT13ModesAndRequests();
  ExpectOfflineClearsVT13ExecutionLatches();
  return 0;
}
