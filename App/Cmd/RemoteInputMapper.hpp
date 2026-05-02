#pragma once

#include <cstdint>

#include "OperatorInputSnapshot.hpp"

namespace App {

// 当前输入源选择，primary DT7 优先于 secondary VT13。
enum class InputSourceSelection : uint8_t {
  none = 0,
  primary_dt7 = 1,
  secondary_vt13 = 2,
};

// 遥控器三段开关位置，保持协议无关。
enum class RemoteSwitchPosition : uint8_t {
  kUnknown = 0,
  kUp = 1,
  kMiddle = 2,
  kDown = 3,
};

enum class VT13Mode : uint8_t {
  kErr = 0,
  kC = 1,
  kN = 2,
  kS = 3,
};

inline constexpr std::uint16_t kDt7KeyW = 1u << 0;
inline constexpr std::uint16_t kDt7KeyS = 1u << 1;
inline constexpr std::uint16_t kDt7KeyA = 1u << 2;
inline constexpr std::uint16_t kDt7KeyD = 1u << 3;
inline constexpr std::uint16_t kDt7KeyShift = 1u << 4;
inline constexpr std::uint16_t kDt7KeyCtrl = 1u << 5;
inline constexpr std::uint16_t kDt7KeyQ = 1u << 6;
inline constexpr std::uint16_t kDt7KeyE = 1u << 7;
inline constexpr std::uint16_t kDt7KeyR = 1u << 8;
inline constexpr std::uint16_t kDt7KeyF = 1u << 9;
inline constexpr std::uint16_t kDt7KeyG = 1u << 10;
inline constexpr std::uint16_t kDt7KeyZ = 1u << 11;
inline constexpr std::uint16_t kDt7KeyX = 1u << 12;
inline constexpr std::uint16_t kDt7KeyC = 1u << 13;
inline constexpr std::uint16_t kDt7KeyV = 1u << 14;
inline constexpr std::uint16_t kDt7KeyB = 1u << 15;

struct DT7InputState {
  bool online = false;
  RemoteSwitchPosition left_switch = RemoteSwitchPosition::kUnknown;
  RemoteSwitchPosition right_switch = RemoteSwitchPosition::kUnknown;
  int16_t left_x = 0;
  int16_t left_y = 0;
  int16_t right_x = 0;
  int16_t right_y = 0;
  int16_t dial = 0;
  int16_t mouse_x_delta = 0;
  int16_t mouse_y_delta = 0;
  bool mouse_left_button = false;
  bool mouse_right_button = false;
  std::uint16_t keyboard_key_code = 0;
};

struct VT13InputState {
  bool online = false;
  VT13Mode mode = VT13Mode::kErr;
  int16_t x = 0;
  int16_t y = 0;
  int16_t yaw = 0;
  int16_t pitch = 0;
  int16_t wheel = 0;
  bool trigger = false;
  bool fn = false;
  bool photo = false;
  bool mouse_left_button = false;
  bool mouse_right_button = false;
  bool key_w = false;
  bool key_s = false;
  bool key_a = false;
  bool key_d = false;
  bool key_shift = false;
  bool key_z = false;
  bool key_e = false;
  bool key_r = false;
  bool key_f = false;
  bool key_c = false;
};

struct InputLatchState {
  bool z_pressed = false;
  bool e_pressed = false;
  bool r_pressed = false;
  bool f_pressed = false;
  bool c_pressed = false;
  bool fire_pressed = false;

  bool emergency_latched = true;
  bool lid_open = false;
  bool friction_latched = false;
  std::uint32_t shot_request_seq = 0;
  std::uint8_t bullet_speed_index = 0;
  std::uint8_t loader_mode_index = 0;
  std::uint8_t chassis_speed_index = 0;
};

inline void ResetInputLatch(InputLatchState& latch) {
  latch = InputLatchState{};
}

inline InputSourceSelection SelectInputSource(bool primary_online,
                                              bool secondary_online) {
  if (primary_online) {
    return InputSourceSelection::primary_dt7;
  }

  if (secondary_online) {
    return InputSourceSelection::secondary_vt13;
  }

  return InputSourceSelection::none;
}

inline float ClampUnit(int16_t raw) {
  if (raw > InputConfig::kStickMaxMagnitude) {
    return 1.0f;
  }
  if (raw < -InputConfig::kStickMaxMagnitude) {
    return -1.0f;
  }
  return static_cast<float>(raw) / InputConfig::kStickMaxMagnitude;
}

inline bool IsKeyDown(std::uint16_t keyboard_key_code, std::uint16_t key_mask) {
  return (keyboard_key_code & key_mask) != 0u;
}

inline void ApplySafeDefaults(OperatorInputSnapshot& snapshot) {
  snapshot.has_active_source = false;
  snapshot.control_source = ControlSource::kUnknown;
  snapshot.request_safe_mode = true;
  snapshot.request_manual_mode = false;
  snapshot.request_auto_aim = false;
  snapshot.request_calibration = false;
  snapshot.requested_motion_mode = MotionModeType::kStop;
  snapshot.requested_aim_mode = AimModeType::kHold;
  snapshot.requested_loader_mode = LoaderModeType::kStop;
  snapshot.requested_burst_count = 0;
  snapshot.chassis_speed_scale = 0.0f;
  snapshot.power_boost_requested = false;
  snapshot.fire_enabled = false;
  snapshot.friction_enabled = false;
  snapshot.ignore_referee_fire_gate = false;
  snapshot.trigger_pressed = false;
  snapshot.shot_request_seq = 0;
  snapshot.target_bullet_speed_mps = 0.0f;
  snapshot.target_shoot_rate_hz = 0.0f;
  snapshot.shoot_rate_scale = 0.0f;
  snapshot.target_vx_mps = 0.0f;
  snapshot.target_vy_mps = 0.0f;
  snapshot.target_wz_radps = 0.0f;
  snapshot.target_yaw_deg = 0.0f;
  snapshot.target_pitch_deg = 0.0f;
  snapshot.yaw_rate_degps = 0.0f;
  snapshot.pitch_rate_degps = 0.0f;
  snapshot.track_target = false;
  snapshot.manual_keys = {};
  snapshot.manual_mouse = {};
  snapshot.manual_toggles = {};
}

inline OperatorInputSnapshot BuildSafeInputSnapshot() {
  OperatorInputSnapshot snapshot;
  ApplySafeDefaults(snapshot);
  snapshot.emergency_latched = true;
  snapshot.lid_open = false;
  return snapshot;
}

inline void ApplyLatchedState(const InputLatchState& latch,
                              OperatorInputSnapshot& snapshot) {
  static constexpr float kBulletSpeedOptions[] = {15.0f, 18.0f, 30.0f};
  static constexpr LoaderModeType kLoaderModeOptions[] = {
      LoaderModeType::kStop,
      LoaderModeType::kSingle,
      LoaderModeType::kBurst,
      LoaderModeType::kContinuous,
  };
  static constexpr float kChassisSpeedOptions[] = {0.4f, 0.6f, 0.8f, 1.0f};

  snapshot.emergency_latched = latch.emergency_latched;
  snapshot.lid_open = latch.lid_open;
  snapshot.friction_enabled = latch.friction_latched;
  snapshot.shot_request_seq = latch.shot_request_seq;
  snapshot.target_bullet_speed_mps = kBulletSpeedOptions[latch.bullet_speed_index];
  snapshot.requested_loader_mode = kLoaderModeOptions[latch.loader_mode_index];
  snapshot.requested_burst_count =
      snapshot.requested_loader_mode == LoaderModeType::kBurst ? 3u : 0u;
  snapshot.chassis_speed_scale = kChassisSpeedOptions[latch.chassis_speed_index];
}

inline void UpdateLatchOnRisingEdge(bool pressed,
                                    bool& pressed_latch,
                                    const void (*on_rising)()) {
  if (pressed && !pressed_latch) {
    on_rising();
  }
  pressed_latch = pressed;
}

inline void CycleBulletSpeed(InputLatchState& latch) {
  latch.bullet_speed_index =
      static_cast<std::uint8_t>((latch.bullet_speed_index + 1u) % 3u);
}

inline void CycleLoaderMode(InputLatchState& latch) {
  latch.loader_mode_index =
      static_cast<std::uint8_t>((latch.loader_mode_index + 1u) % 4u);
}

inline void CycleChassisSpeed(InputLatchState& latch) {
  latch.chassis_speed_index =
      static_cast<std::uint8_t>((latch.chassis_speed_index + 1u) % 4u);
}

inline void UpdateCommonToggleEdges(bool z_pressed, bool e_pressed,
                                    bool r_pressed, bool f_pressed,
                                    bool c_pressed, InputLatchState& latch,
                                    OperatorInputSnapshot& snapshot) {
  if (z_pressed && !latch.z_pressed) {
    CycleBulletSpeed(latch);
  }
  if (e_pressed && !latch.e_pressed) {
    CycleLoaderMode(latch);
  }
  if (r_pressed && !latch.r_pressed) {
    latch.lid_open = !latch.lid_open;
    snapshot.manual_toggles.lid_pressed = true;
  }
  if (f_pressed && !latch.f_pressed) {
    latch.friction_latched = !latch.friction_latched;
    snapshot.manual_toggles.friction_pressed = true;
  }
  if (c_pressed && !latch.c_pressed) {
    CycleChassisSpeed(latch);
  }

  latch.z_pressed = z_pressed;
  latch.e_pressed = e_pressed;
  latch.r_pressed = r_pressed;
  latch.f_pressed = f_pressed;
  latch.c_pressed = c_pressed;
}

inline void UpdateFireTriggerEdge(bool fire_pressed, InputLatchState& latch,
                                  OperatorInputSnapshot& snapshot) {
  if (fire_pressed && !latch.fire_pressed) {
    ++latch.shot_request_seq;
    snapshot.manual_toggles.fire_pressed = true;
  }
  latch.fire_pressed = fire_pressed;
  snapshot.shot_request_seq = latch.shot_request_seq;
}

inline void ApplyKeyboardMotion(bool w, bool s, bool a, bool d,
                                OperatorInputSnapshot& snapshot) {
  const float speed = snapshot.chassis_speed_scale *
                      InputConfig::kMaxChassisLinearSpeedMps;
  float vx = 0.0f;
  float vy = 0.0f;
  if (w) {
    vx += speed;
  }
  if (s) {
    vx -= speed;
  }
  if (a) {
    vy += speed;
  }
  if (d) {
    vy -= speed;
  }
  snapshot.target_vx_mps = vx;
  snapshot.target_vy_mps = vy;
}

inline void ApplyDT7Input(const DT7InputState& state, InputLatchState& latch,
                          OperatorInputSnapshot& snapshot) {
  ApplySafeDefaults(snapshot);

  if (!state.online) {
    ResetInputLatch(latch);
    latch.emergency_latched = true;
    ApplySafeDefaults(snapshot);
    snapshot.emergency_latched = true;
    return;
  }

  snapshot.has_active_source = true;
  snapshot.control_source = ControlSource::kRemote;

  if (state.dial > InputConfig::kDialEmergencyThreshold) {
    latch.emergency_latched = true;
    snapshot.emergency_latched = true;
    snapshot.request_safe_mode = true;
    return;
  }

  latch.emergency_latched = false;
  snapshot.emergency_latched = false;
  snapshot.request_safe_mode = false;
  snapshot.request_manual_mode = true;
  snapshot.requested_aim_mode = AimModeType::kManual;
  snapshot.chassis_speed_scale = 1.0f;
  snapshot.target_bullet_speed_mps = InputConfig::kDefaultBulletSpeedMps;

  if (state.left_switch == RemoteSwitchPosition::kUp) {
    snapshot.requested_motion_mode = MotionModeType::kSpin;
  } else if (state.left_switch == RemoteSwitchPosition::kMiddle) {
    snapshot.requested_motion_mode = MotionModeType::kFollowGimbal;
  } else if (state.left_switch == RemoteSwitchPosition::kDown) {
    snapshot.requested_motion_mode = MotionModeType::kIndependent;
  }

  if (state.right_switch == RemoteSwitchPosition::kUp) {
    snapshot.friction_enabled = true;
    snapshot.fire_enabled = true;
    snapshot.requested_loader_mode = LoaderModeType::kContinuous;
    snapshot.ignore_referee_fire_gate = true;
    snapshot.target_shoot_rate_hz = InputConfig::kDefaultShootRateHz;
    snapshot.shoot_rate_scale = 1.0f;
  } else if (state.right_switch == RemoteSwitchPosition::kMiddle) {
    snapshot.friction_enabled = true;
  }

  snapshot.target_vx_mps =
      ClampUnit(state.left_y) * InputConfig::kMaxChassisLinearSpeedMps;
  snapshot.target_vy_mps =
      ClampUnit(state.left_x) * InputConfig::kMaxChassisLinearSpeedMps;
  snapshot.yaw_rate_degps =
      ClampUnit(state.right_x) * InputConfig::kMaxYawRateDegps;
  snapshot.pitch_rate_degps =
      -ClampUnit(state.right_y) * InputConfig::kMaxPitchRateDegps;
}

inline void ApplyVT13Input(const VT13InputState& state, InputLatchState& latch,
                           OperatorInputSnapshot& snapshot) {
  ApplySafeDefaults(snapshot);
  ApplyLatchedState(latch, snapshot);

  if (!state.online) {
    ResetInputLatch(latch);
    latch.emergency_latched = true;
    ApplySafeDefaults(snapshot);
    snapshot.emergency_latched = true;
    return;
  }

  snapshot.has_active_source = true;
  snapshot.control_source = ControlSource::kKeyboardMouse;
  snapshot.request_manual_mode =
      state.mode == VT13Mode::kN || state.mode == VT13Mode::kS;
  snapshot.request_safe_mode =
      state.mode == VT13Mode::kC || state.mode == VT13Mode::kErr;
  latch.emergency_latched = snapshot.request_safe_mode;

  const bool z_pressed = state.key_z;
  const bool e_pressed = state.key_e;
  const bool r_pressed = state.key_r;
  const bool f_pressed = state.key_f;
  const bool c_pressed = state.key_c;
  UpdateCommonToggleEdges(z_pressed, e_pressed, r_pressed, f_pressed, c_pressed,
                          latch, snapshot);
  ApplyLatchedState(latch, snapshot);

  snapshot.manual_keys.w = state.key_w;
  snapshot.manual_keys.s = state.key_s;
  snapshot.manual_keys.a = state.key_a;
  snapshot.manual_keys.d = state.key_d;
  snapshot.manual_keys.shift = state.key_shift;
  snapshot.manual_keys.z = z_pressed;
  snapshot.manual_keys.e = e_pressed;
  snapshot.manual_keys.r = r_pressed;
  snapshot.manual_keys.f = f_pressed;
  snapshot.manual_keys.c = c_pressed;
  snapshot.manual_mouse.left_button = state.mouse_left_button;
  snapshot.manual_mouse.right_button = state.mouse_right_button;
  snapshot.power_boost_requested = state.key_shift;
  snapshot.manual_toggles.power_boost_pressed = state.key_shift;

  if (state.mode == VT13Mode::kS) {
    snapshot.friction_enabled = true;
  }

  if (snapshot.request_manual_mode) {
    if (state.key_w || state.key_s || state.key_a || state.key_d) {
      ApplyKeyboardMotion(state.key_w, state.key_s, state.key_a, state.key_d,
                          snapshot);
    } else {
      snapshot.target_vx_mps =
          ClampUnit(state.y) * InputConfig::kMaxChassisLinearSpeedMps;
      snapshot.target_vy_mps =
          ClampUnit(state.x) * InputConfig::kMaxChassisLinearSpeedMps;
    }
    snapshot.yaw_rate_degps =
        ClampUnit(state.yaw) * InputConfig::kMaxYawRateDegps;
    snapshot.pitch_rate_degps =
        ClampUnit(state.pitch) * InputConfig::kMaxPitchRateDegps;
    snapshot.target_wz_radps = ClampUnit(state.wheel);
    snapshot.requested_motion_mode = MotionModeType::kIndependent;
    snapshot.requested_aim_mode = AimModeType::kManual;
  }

  snapshot.trigger_pressed = state.trigger;
  UpdateFireTriggerEdge(state.trigger, latch, snapshot);
  snapshot.fire_enabled = snapshot.friction_enabled && state.trigger;
  snapshot.target_shoot_rate_hz =
      snapshot.fire_enabled ? InputConfig::kDefaultShootRateHz : 0.0f;
  snapshot.shoot_rate_scale = snapshot.fire_enabled ? 1.0f : 0.0f;
  snapshot.request_auto_aim = state.photo;
  snapshot.request_calibration =
      state.mode == VT13Mode::kC && state.fn && state.photo;
}

}  // namespace App
