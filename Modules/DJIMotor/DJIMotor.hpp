#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: DJIMotor 是一个通用 DJI 电机能力模块，负责反馈解码、分组发帧和基础闭环接口 / DJIMotor provides generic DJI motor capability including feedback decode, grouped CAN send, and basic closed-loop interfaces
constructor_args: []
template_args: []
required_hardware: can1/can2
depends: []
=== END MANIFEST === */
// clang-format on

#include <array>
#include <cmath>
#include <cstdint>

#include "app_framework.hpp"
#include "can.hpp"
#include "libxr_cb.hpp"
#include "logger.hpp"
#include "pid.hpp"
#include "timebase.hpp"

enum class DJIMotorType : std::uint8_t {
  kM2006 = 0,
  kM3508 = 1,
  kGM6020 = 2,
};

enum class DJIMotorOuterLoop : std::uint8_t {
  kCurrent = 0,
  kSpeed = 1,
  kAngle = 2,
};

enum class DJIMotorFeedbackSource : std::uint8_t {
  kMotor = 0,
  kExternal = 1,
};

struct DJIMotorPidConfig {
  LibXR::PID<float>::Param current{};
  LibXR::PID<float>::Param speed{};
  LibXR::PID<float>::Param angle{};
};

struct DJIMotorGroupConfig {
  const char* can_name = "can1";
  std::uint32_t command_frame_id = 0x200;
};

struct DJIMotorConfig {
  const char* name = "dji_motor";
  DJIMotorType motor_type = DJIMotorType::kM3508;
  std::uint16_t feedback_id = 0x201;
  std::uint8_t command_slot = 0;
  std::int8_t direction = 1;
  float reduction_ratio = 1.0f;
  float ecd_to_deg = 360.0f / 8192.0f;
  float max_command_current = 16000.0f;
  std::uint32_t feedback_timeout_ms = 100;
  DJIMotorOuterLoop default_outer_loop = DJIMotorOuterLoop::kSpeed;
  DJIMotorPidConfig pid{};
};

struct DJIMotorFeedback {
  bool online = false;
  bool initialized = false;
  std::uint16_t angle_raw = 0;
  float rotor_angle_deg = 0.0f;
  float rotor_total_angle_deg = 0.0f;
  float rotor_speed_rpm = 0.0f;
  float rotor_speed_degps = 0.0f;
  std::int16_t real_current = 0;
  std::uint8_t temperature = 0;
  float output_angle_deg = 0.0f;
  float output_total_angle_deg = 0.0f;
  float output_speed_rpm = 0.0f;
  float output_speed_degps = 0.0f;
  std::uint32_t last_feedback_time_ms = 0;
};

struct DJIMotorState {
  bool enabled = false;
  bool safe_stopped = true;
  DJIMotorOuterLoop outer_loop = DJIMotorOuterLoop::kSpeed;
  DJIMotorFeedbackSource angle_feedback_source = DJIMotorFeedbackSource::kMotor;
  DJIMotorFeedbackSource speed_feedback_source = DJIMotorFeedbackSource::kMotor;
  float reference = 0.0f;
  float target_speed_degps = 0.0f;
  std::int16_t target_current = 0;
  DJIMotorFeedback feedback{};
};

class DJIMotor;

class DJIMotorGroup : public LibXR::Application {
 public:
  static constexpr std::size_t kMaxMotorsPerGroup = 4;

  DJIMotorGroup(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
                const DJIMotorGroupConfig& config);

  void OnMonitor() override;

  void RegisterMotor(DJIMotor& motor);

 private:
  static constexpr float kDefaultControlDtS = 0.001f;
  static constexpr float kMaxControlDtS = 0.05f;

  static void OnCanRxStatic(bool, DJIMotorGroup* self,
                            const LibXR::CAN::ClassicPack& pack);

  float ComputeDeltaTimeSeconds();

  void OnCanFeedback(const LibXR::CAN::ClassicPack& pack);

  LibXR::CAN* can_ = nullptr;
  DJIMotorGroupConfig config_{};
  LibXR::CAN::Callback can_rx_callback_;
  std::array<DJIMotor*, kMaxMotorsPerGroup> motors_{nullptr, nullptr, nullptr,
                                                    nullptr};
  LibXR::MicrosecondTimestamp last_control_time_us_ = 0;
};

class DJIMotor {
 public:
  DJIMotor(DJIMotorGroup& group, const DJIMotorConfig& config);

  void Enable();

  void Stop();

  void SetReference(float reference);

  void SetOuterLoop(DJIMotorOuterLoop outer_loop);

  void ChangeFeed(DJIMotorOuterLoop loop, DJIMotorFeedbackSource source);

  void SetExternalAngleFeedbackDeg(float angle_deg);

  void SetExternalSpeedFeedbackDegps(float speed_degps);

  const DJIMotorConfig& Config() const;

  const DJIMotorState& GetState() const;

  const DJIMotorFeedback& GetFeedback() const;

 private:
  friend class DJIMotorGroup;

  static float NormalizeDegrees(float angle_deg);

  static float ClampAbs(float value, float limit);

  void UpdateFeedback(const LibXR::CAN::ClassicPack& pack);

  std::int16_t Step(float dt_s);

  DJIMotorGroup& group_;
  DJIMotorConfig config_{};
  DJIMotorState state_{};
  LibXR::PID<float> current_pid_;
  LibXR::PID<float> speed_pid_;
  LibXR::PID<float> angle_pid_;
  float external_angle_feedback_deg_ = 0.0f;
  float external_speed_feedback_degps_ = 0.0f;
};

inline DJIMotorGroup::DJIMotorGroup(LibXR::HardwareContainer& hw,
                                    LibXR::ApplicationManager& app,
                                    const DJIMotorGroupConfig& config)
    : can_(hw.template FindOrExit<LibXR::CAN>({config.can_name})),
      config_(config),
      last_control_time_us_(LibXR::Timebase::GetMicroseconds()) {
  can_rx_callback_ = LibXR::CAN::Callback::Create(OnCanRxStatic, this);
  app.Register(*this);
}

inline void DJIMotorGroup::OnMonitor() {
  const float dt_s = ComputeDeltaTimeSeconds();
  LibXR::CAN::ClassicPack pack{};
  pack.id = config_.command_frame_id;
  pack.type = LibXR::CAN::Type::STANDARD;
  pack.dlc = 8;
  for (auto& byte : pack.data) {
    byte = 0;
  }

  bool has_registered_motor = false;
  for (auto* motor : motors_) {
    if (motor == nullptr) {
      continue;
    }

    has_registered_motor = true;
    const std::int16_t target_current = motor->Step(dt_s);
    const std::size_t offset =
        static_cast<std::size_t>(motor->Config().command_slot) * 2U;
    if (offset + 1U >= 8U) {
      continue;
    }
    pack.data[offset] = static_cast<std::uint8_t>((target_current >> 8) & 0xFF);
    pack.data[offset + 1U] = static_cast<std::uint8_t>(target_current & 0xFF);
  }

  if (has_registered_motor) {
    (void)can_->AddMessage(pack);
  }
}

inline void DJIMotorGroup::RegisterMotor(DJIMotor& motor) {
  const std::size_t slot = static_cast<std::size_t>(motor.Config().command_slot);
  if (slot >= kMaxMotorsPerGroup) {
    XR_LOG_ERROR("DJIMotor slot overflow: %s -> %u", motor.Config().name,
                 motor.Config().command_slot);
    return;
  }
  if (motors_[slot] != nullptr) {
    XR_LOG_ERROR("DJIMotor slot conflict: %u", motor.Config().command_slot);
    return;
  }
  motors_[slot] = &motor;
  can_->Register(can_rx_callback_, LibXR::CAN::Type::STANDARD,
                 LibXR::CAN::FilterMode::ID_RANGE, motor.Config().feedback_id,
                 motor.Config().feedback_id);
}

inline void DJIMotorGroup::OnCanRxStatic(bool, DJIMotorGroup* self,
                                         const LibXR::CAN::ClassicPack& pack) {
  self->OnCanFeedback(pack);
}

inline float DJIMotorGroup::ComputeDeltaTimeSeconds() {
  const auto now_us = LibXR::Timebase::GetMicroseconds();
  const auto delta = now_us - last_control_time_us_;
  last_control_time_us_ = now_us;

  const float dt_s = delta.ToSecondf();
  if (!std::isfinite(dt_s) || dt_s <= 0.0f || dt_s > kMaxControlDtS) {
    return kDefaultControlDtS;
  }
  return dt_s;
}

inline void DJIMotorGroup::OnCanFeedback(const LibXR::CAN::ClassicPack& pack) {
  for (auto* motor : motors_) {
    if (motor == nullptr) {
      continue;
    }
    if (motor->Config().feedback_id != pack.id) {
      continue;
    }
    motor->UpdateFeedback(pack);
    break;
  }
}

inline DJIMotor::DJIMotor(DJIMotorGroup& group, const DJIMotorConfig& config)
    : group_(group),
      config_(config),
      current_pid_(config.pid.current),
      speed_pid_(config.pid.speed),
      angle_pid_(config.pid.angle) {
  state_.outer_loop = config.default_outer_loop;
  group_.RegisterMotor(*this);
}

inline void DJIMotor::Enable() {
  state_.enabled = true;
  state_.safe_stopped = false;
}

inline void DJIMotor::Stop() {
  state_.enabled = false;
  state_.safe_stopped = true;
  state_.reference = 0.0f;
  state_.target_speed_degps = 0.0f;
  state_.target_current = 0;
  current_pid_.Reset();
  speed_pid_.Reset();
  angle_pid_.Reset();
}

inline void DJIMotor::SetReference(float reference) { state_.reference = reference; }

inline void DJIMotor::SetOuterLoop(DJIMotorOuterLoop outer_loop) {
  if (state_.outer_loop == outer_loop) {
    return;
  }
  state_.outer_loop = outer_loop;
  current_pid_.Reset();
  speed_pid_.Reset();
  angle_pid_.Reset();
}

inline void DJIMotor::ChangeFeed(DJIMotorOuterLoop loop,
                                 DJIMotorFeedbackSource source) {
  switch (loop) {
    case DJIMotorOuterLoop::kAngle:
      state_.angle_feedback_source = source;
      break;
    case DJIMotorOuterLoop::kSpeed:
      state_.speed_feedback_source = source;
      break;
    case DJIMotorOuterLoop::kCurrent:
    default:
      break;
  }
}

inline void DJIMotor::SetExternalAngleFeedbackDeg(float angle_deg) {
  external_angle_feedback_deg_ = angle_deg;
}

inline void DJIMotor::SetExternalSpeedFeedbackDegps(float speed_degps) {
  external_speed_feedback_degps_ = speed_degps;
}

inline const DJIMotorConfig& DJIMotor::Config() const { return config_; }

inline const DJIMotorState& DJIMotor::GetState() const { return state_; }

inline const DJIMotorFeedback& DJIMotor::GetFeedback() const {
  return state_.feedback;
}

inline float DJIMotor::NormalizeDegrees(float angle_deg) {
  float normalized = std::fmod(angle_deg, 360.0f);
  if (normalized < 0.0f) {
    normalized += 360.0f;
  }
  return normalized;
}

inline float DJIMotor::ClampAbs(float value, float limit) {
  if (value > limit) {
    return limit;
  }
  if (value < -limit) {
    return -limit;
  }
  return value;
}

inline void DJIMotor::UpdateFeedback(const LibXR::CAN::ClassicPack& pack) {
  if (pack.dlc < 7) {
    return;
  }

  const std::uint16_t angle_raw =
      static_cast<std::uint16_t>((pack.data[0] << 8) | pack.data[1]);
  const std::int16_t speed_raw =
      static_cast<std::int16_t>((pack.data[2] << 8) | pack.data[3]);
  const std::int16_t current_raw =
      static_cast<std::int16_t>((pack.data[4] << 8) | pack.data[5]);
  const std::uint8_t temperature = pack.data[6];
  const float direction_sign =
      static_cast<float>(config_.direction >= 0 ? 1 : -1);

  if (!state_.feedback.initialized) {
    state_.feedback.angle_raw = angle_raw;
    state_.feedback.initialized = true;
  } else {
    int delta =
        static_cast<int>(angle_raw) - static_cast<int>(state_.feedback.angle_raw);
    if (delta > 4096) {
      delta -= 8192;
    } else if (delta < -4096) {
      delta += 8192;
    }
    state_.feedback.rotor_total_angle_deg +=
        static_cast<float>(delta) * config_.ecd_to_deg * direction_sign;
    state_.feedback.angle_raw = angle_raw;
  }

  const float reduction_ratio =
      (config_.reduction_ratio > 0.0f) ? config_.reduction_ratio : 1.0f;
  state_.feedback.online = true;
  state_.feedback.rotor_angle_deg = NormalizeDegrees(
      static_cast<float>(angle_raw) * config_.ecd_to_deg * direction_sign);
  state_.feedback.rotor_speed_rpm = static_cast<float>(speed_raw) * direction_sign;
  state_.feedback.rotor_speed_degps = state_.feedback.rotor_speed_rpm * 6.0f;
  state_.feedback.real_current =
      static_cast<std::int16_t>(static_cast<float>(current_raw) * direction_sign);
  state_.feedback.temperature = temperature;
  state_.feedback.output_total_angle_deg =
      state_.feedback.rotor_total_angle_deg / reduction_ratio;
  state_.feedback.output_angle_deg =
      NormalizeDegrees(state_.feedback.output_total_angle_deg);
  state_.feedback.output_speed_rpm =
      state_.feedback.rotor_speed_rpm / reduction_ratio;
  state_.feedback.output_speed_degps =
      state_.feedback.rotor_speed_degps / reduction_ratio;
  state_.feedback.last_feedback_time_ms = LibXR::Thread::GetTime();
}

inline std::int16_t DJIMotor::Step(float dt_s) {
  const std::uint32_t now_ms = LibXR::Thread::GetTime();
  if (config_.feedback_timeout_ms > 0 &&
      (now_ms - state_.feedback.last_feedback_time_ms) >
          config_.feedback_timeout_ms) {
    state_.feedback.online = false;
  }

  if (!state_.enabled) {
    state_.safe_stopped = true;
    state_.target_speed_degps = 0.0f;
    state_.target_current = 0;
    return 0;
  }

  state_.safe_stopped = false;
  const float angle_feedback_deg =
      (state_.angle_feedback_source == DJIMotorFeedbackSource::kExternal)
          ? external_angle_feedback_deg_
          : state_.feedback.output_total_angle_deg;
  const float speed_feedback_degps =
      (state_.speed_feedback_source == DJIMotorFeedbackSource::kExternal)
          ? external_speed_feedback_degps_
          : state_.feedback.output_speed_degps;

  float target_current = state_.reference;
  switch (state_.outer_loop) {
    case DJIMotorOuterLoop::kAngle:
      state_.target_speed_degps =
          angle_pid_.Calculate(state_.reference, angle_feedback_deg, dt_s);
      target_current = speed_pid_.Calculate(state_.target_speed_degps,
                                            speed_feedback_degps, dt_s);
      break;
    case DJIMotorOuterLoop::kSpeed:
      state_.target_speed_degps = state_.reference;
      target_current =
          speed_pid_.Calculate(state_.reference, speed_feedback_degps, dt_s);
      break;
    case DJIMotorOuterLoop::kCurrent:
    default:
      state_.target_speed_degps = 0.0f;
      target_current =
          current_pid_.Calculate(state_.reference,
                                 static_cast<float>(state_.feedback.real_current), dt_s);
      break;
  }

  target_current = ClampAbs(target_current, config_.max_command_current);
  state_.target_current = static_cast<std::int16_t>(target_current);
  return state_.target_current;
}
