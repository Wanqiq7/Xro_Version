#include <cassert>
#include <cmath>

#include "App/Cmd/OperatorInputSnapshot.hpp"
#include "App/Cmd/RemoteInputMapper.hpp"

namespace {

void ExpectNear(float actual, float expected) {
  assert(std::fabs(actual - expected) < 0.0001f);
}

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

  assert(snapshot.yaw_rate_degps == 0.0f);
  assert(snapshot.pitch_rate_degps == 0.0f);
  assert(!snapshot.fire_enabled);
  assert(!snapshot.friction_enabled);
  assert(!snapshot.ignore_referee_fire_gate);
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

void ExpectDT7OfflineStaysSafeAndClearsExecutionIntent() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::DT7InputState input;
  input.online = true;
  input.left_switch = App::RemoteSwitchPosition::kUp;
  input.right_switch = App::RemoteSwitchPosition::kUp;
  input.dial = App::InputConfig::kDialEmergencyThreshold;

  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.friction_enabled);
  assert(snapshot.fire_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kContinuous);
  assert(snapshot.ignore_referee_fire_gate);

  input.online = false;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.request_safe_mode);
  assert(snapshot.emergency_latched);
  assert(!snapshot.has_active_source);
  assert(!snapshot.request_manual_mode);
  assert(!snapshot.friction_enabled);
  assert(!snapshot.fire_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(!snapshot.ignore_referee_fire_gate);

  input.online = true;
  input.right_switch = App::RemoteSwitchPosition::kDown;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(!snapshot.friction_enabled);
  assert(!snapshot.fire_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(!snapshot.ignore_referee_fire_gate);
}

void ExpectDT7DialSelectsManualOrSafe() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::DT7InputState input;
  input.online = true;
  input.left_switch = App::RemoteSwitchPosition::kMiddle;
  input.right_switch = App::RemoteSwitchPosition::kMiddle;
  input.dial = App::InputConfig::kDialEmergencyThreshold;

  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.has_active_source);
  assert(snapshot.control_source == App::ControlSource::kRemote);
  assert(snapshot.request_manual_mode);
  assert(!snapshot.request_safe_mode);
  assert(!snapshot.emergency_latched);
  assert(snapshot.requested_aim_mode == App::AimModeType::kManual);

  input.dial = App::InputConfig::kDialEmergencyThreshold + 1;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.request_safe_mode);
  assert(snapshot.emergency_latched);
  assert(!snapshot.request_manual_mode);
  assert(!snapshot.friction_enabled);
  assert(!snapshot.fire_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(!snapshot.ignore_referee_fire_gate);
}

void ExpectDT7LeftSwitchMapsMotionModesAndUsesSticks() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::DT7InputState input;
  input.online = true;
  input.dial = App::InputConfig::kDialEmergencyThreshold;
  input.right_switch = App::RemoteSwitchPosition::kDown;
  input.left_y = 330;
  input.left_x = -330;
  input.right_x = 220;
  input.right_y = -220;
  input.keyboard_key_code = App::kDt7KeyW | App::kDt7KeyA;
  input.mouse_x_delta = -330;
  input.mouse_y_delta = 330;

  input.left_switch = App::RemoteSwitchPosition::kUp;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.control_source == App::ControlSource::kRemote);
  assert(snapshot.requested_motion_mode == App::MotionModeType::kSpin);
  assert(snapshot.target_vx_mps > 0.0f);
  assert(snapshot.target_vy_mps < 0.0f);
  assert(snapshot.yaw_rate_degps > 0.0f);
  assert(snapshot.pitch_rate_degps > 0.0f);
  ExpectNear(snapshot.yaw_rate_degps,
             App::InputConfig::kMaxYawRateDegps / 3.0f);
  ExpectNear(snapshot.pitch_rate_degps,
             App::InputConfig::kMaxPitchRateDegps / 3.0f);

  input.left_switch = App::RemoteSwitchPosition::kMiddle;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.requested_motion_mode == App::MotionModeType::kFollowGimbal);

  input.left_switch = App::RemoteSwitchPosition::kDown;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.requested_motion_mode == App::MotionModeType::kIndependent);
}

void ExpectDT7RightSwitchMapsShootIntent() {
  App::InputLatchState latch;
  App::OperatorInputSnapshot snapshot = App::BuildSafeInputSnapshot();
  App::DT7InputState input;
  input.online = true;
  input.dial = App::InputConfig::kDialEmergencyThreshold;
  input.left_switch = App::RemoteSwitchPosition::kMiddle;

  input.right_switch = App::RemoteSwitchPosition::kUp;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.friction_enabled);
  assert(snapshot.fire_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kContinuous);
  assert(snapshot.ignore_referee_fire_gate);
  assert(snapshot.target_shoot_rate_hz == App::InputConfig::kDefaultShootRateHz);
  assert(snapshot.shoot_rate_scale == 1.0f);

  input.right_switch = App::RemoteSwitchPosition::kMiddle;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(snapshot.friction_enabled);
  assert(!snapshot.fire_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(!snapshot.ignore_referee_fire_gate);
  assert(snapshot.target_shoot_rate_hz == 0.0f);
  assert(snapshot.shoot_rate_scale == 0.0f);

  input.right_switch = App::RemoteSwitchPosition::kDown;
  App::ApplyDT7Input(input, latch, snapshot);
  assert(!snapshot.friction_enabled);
  assert(!snapshot.fire_enabled);
  assert(snapshot.requested_loader_mode == App::LoaderModeType::kStop);
  assert(!snapshot.ignore_referee_fire_gate);
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
  ExpectDT7OfflineStaysSafeAndClearsExecutionIntent();
  ExpectDT7DialSelectsManualOrSafe();
  ExpectDT7LeftSwitchMapsMotionModesAndUsesSticks();
  ExpectDT7RightSwitchMapsShootIntent();
  ExpectVT13ModesAndRequests();
  ExpectOfflineClearsVT13ExecutionLatches();
  return 0;
}
