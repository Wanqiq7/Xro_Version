#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: DMMotor 是一个通用达妙 MIT 协议模块，负责 MIT 五元组打包、基础控制命令与反馈解析 / DMMotor provides generic DM MIT protocol capability including MIT tuple packing, basic control commands and feedback decoding
constructor_args: []
template_args: []
required_hardware: can1/can2
depends: []
=== END MANIFEST === */
// clang-format on

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "app_framework.hpp"
#include "can.hpp"
#include "logger.hpp"
#include "pid.hpp"
#include "thread.hpp"

enum class DMMotorControlCommand : std::uint8_t {
  kNone = 0x00u,
  kEnable = 0xFCu,
  kStop = 0xFDu,
  kZeroPosition = 0xFEu,
  kClearError = 0xFBu,
};

enum class DMMotorOuterLoop : std::uint8_t {
  kMIT = 0,
  kSpeed = 1,
  kAngle = 2,
};

enum class DMMotorFeedbackSource : std::uint8_t {
  kMotor = 0,
  kExternal = 1,
};

struct DMMotorMitLimit {
  float angle_min = -12.5f;
  float angle_max = 12.5f;
  float velocity_min = -45.0f;
  float velocity_max = 45.0f;
  float torque_min = -18.0f;
  float torque_max = 18.0f;
  float kp_min = 0.0f;
  float kp_max = 500.0f;
  float kd_min = 0.0f;
  float kd_max = 5.0f;
};

struct DMMotorPidConfig {
  LibXR::PID<float>::Param speed{};
  LibXR::PID<float>::Param angle{};
};

struct DMMotorConfig {
  const char* name = "dm_motor";
  const char* can_name = "can1";
  std::uint32_t tx_id = 0x01u;
  std::uint32_t feedback_id = 0x01u;
  std::int8_t direction = 1;
  std::uint32_t feedback_timeout_ms = 100u;
  DMMotorOuterLoop default_outer_loop = DMMotorOuterLoop::kMIT;
  DMMotorMitLimit mit_limit{};
  DMMotorPidConfig pid{};
};

struct DMMotorCommand {
  float position = 0.0f;
  float velocity = 0.0f;
  float kp = 0.0f;
  float kd = 0.0f;
  float torque = 0.0f;
  DMMotorControlCommand control = DMMotorControlCommand::kNone;
};

struct DMMotorFeedback {
  bool online = false;
  bool initialized = false;
  std::uint8_t motor_id = 0u;
  std::uint8_t motor_state = 0u;
  float angle = 0.0f;
  float total_angle = 0.0f;
  float velocity = 0.0f;
  float torque = 0.0f;
  float mos_temp = 0.0f;
  float rotor_temp = 0.0f;
  std::int32_t total_round = 0;
  std::uint32_t last_feedback_time_ms = 0u;
};

struct DMMotorState {
  bool enabled = false;
  bool safe_stopped = true;
  DMMotorOuterLoop outer_loop = DMMotorOuterLoop::kMIT;
  DMMotorFeedbackSource angle_feedback_source = DMMotorFeedbackSource::kMotor;
  DMMotorFeedbackSource speed_feedback_source = DMMotorFeedbackSource::kMotor;
  float reference = 0.0f;
  float target_speed_radps = 0.0f;
  float target_torque_nm = 0.0f;
};

class DMMotor : public LibXR::Application {
 public:
  DMMotor(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
          const DMMotorConfig& config);

  void OnMonitor() override;

  void Enable();
  void Stop();
  void Disable();
  void Hold();
  void ClearError();
  void ZeroPosition();
  void SetZeroCommand();

  void SetMITCommand(float position, float velocity, float kp, float kd,
                     float torque);

  void SetReference(float reference);
  void SetOuterLoop(DMMotorOuterLoop outer_loop);
  void ChangeFeed(DMMotorOuterLoop loop, DMMotorFeedbackSource source);
  void SetExternalAngleFeedbackRad(float angle_rad);
  void SetExternalSpeedFeedbackRadps(float speed_radps);

  const DMMotorConfig& Config() const;
  const DMMotorFeedback& GetFeedback() const;
  const DMMotorCommand& GetCommand() const;
  const DMMotorState& GetState() const;

 private:
  static constexpr float kEpsilon = 1e-6f;
  static constexpr float kDefaultControlDtS = 0.001f;
  static constexpr float kMaxControlDtS = 0.05f;
  static constexpr std::uint8_t kMotorStateDisabled = 0x00u;
  static constexpr std::uint8_t kMotorStateEnabled = 0x01u;
  static constexpr std::uint32_t kEnableRetryIntervalMs = 50u;

  static void OnCanRxStatic(bool, DMMotor* self,
                            const LibXR::CAN::ClassicPack& pack);

  static float Clamp(float value, float min_value, float max_value);

  static std::uint16_t FloatToUInt(float value, float min_value, float max_value,
                                   std::uint8_t bits);

  static float UIntToFloat(std::uint16_t value, float min_value, float max_value,
                           std::uint8_t bits);

  float ApplyDirection(float value) const;

  void UpdateOnlineStatus();

  void CheckAutoReEnable();

  float ComputeDeltaTimeSeconds();

  void StepCascadePid(float dt_s);

  void EnterSafeStop();

  void HandleControlCommand(DMMotorControlCommand command);

  void SendControlCommand(DMMotorControlCommand command);

  void SendMITCommand();

  void UpdateFeedback(const LibXR::CAN::ClassicPack& pack);

  void CheckAndNormalizeLimits();

  LibXR::CAN* can_ = nullptr;
  DMMotorConfig config_{};
  DMMotorCommand command_{};
  DMMotorFeedback feedback_{};
  DMMotorState state_{};
  LibXR::CAN::Callback can_rx_callback_;
  LibXR::PID<float> speed_pid_;
  LibXR::PID<float> angle_pid_;
  float external_angle_feedback_rad_ = 0.0f;
  float external_speed_feedback_radps_ = 0.0f;
  LibXR::MicrosecondTimestamp last_control_time_us_ = 0;
  std::uint32_t last_enable_time_ms_ = 0u;
  bool has_mit_command_ = false;
  float last_angle_ = 0.0f;
};

inline DMMotor::DMMotor(LibXR::HardwareContainer& hw,
                        LibXR::ApplicationManager& app,
                        const DMMotorConfig& config)
    : can_(hw.template FindOrExit<LibXR::CAN>({config.can_name})),
      config_(config),
      speed_pid_(config.pid.speed),
      angle_pid_(config.pid.angle) {
  state_.outer_loop = config.default_outer_loop;
  CheckAndNormalizeLimits();
  can_rx_callback_ = LibXR::CAN::Callback::Create(OnCanRxStatic, this);
  can_->Register(can_rx_callback_, LibXR::CAN::Type::STANDARD,
                 LibXR::CAN::FilterMode::ID_RANGE, config_.feedback_id,
                 config_.feedback_id);
  app.Register(*this);
}

inline void DMMotor::OnMonitor() {
  UpdateOnlineStatus();
  if (!feedback_.online) {
    EnterSafeStop();
    return;
  }

  if (state_.enabled) {
    state_.safe_stopped = false;
  }

  CheckAutoReEnable();
  if (state_.outer_loop != DMMotorOuterLoop::kMIT) {
    const float dt_s = ComputeDeltaTimeSeconds();
    if (state_.enabled) {
      StepCascadePid(dt_s);
    }
  }
  if (has_mit_command_) {
    SendMITCommand();
  }
}

inline void DMMotor::Enable() {
  command_.control = DMMotorControlCommand::kEnable;
  state_.enabled = true;
  state_.safe_stopped = false;
  last_enable_time_ms_ = LibXR::Thread::GetTime();
  HandleControlCommand(DMMotorControlCommand::kEnable);
}

inline void DMMotor::Stop() {
  Disable();
}

inline void DMMotor::Disable() {
  command_.control = DMMotorControlCommand::kStop;
  state_.enabled = false;
  state_.reference = 0.0f;
  EnterSafeStop();
  HandleControlCommand(DMMotorControlCommand::kStop);
}

inline void DMMotor::Hold() {
  SetZeroCommand();
}

inline void DMMotor::ClearError() {
  command_.control = DMMotorControlCommand::kClearError;
  HandleControlCommand(DMMotorControlCommand::kClearError);
}

inline void DMMotor::ZeroPosition() {
  command_.control = DMMotorControlCommand::kZeroPosition;
  command_.position = 0.0f;
  command_.velocity = 0.0f;
  command_.kp = 0.0f;
  command_.kd = 0.0f;
  command_.torque = 0.0f;
  has_mit_command_ = false;
  HandleControlCommand(DMMotorControlCommand::kZeroPosition);
}

inline void DMMotor::SetZeroCommand() {
  command_.position = 0.0f;
  command_.velocity = 0.0f;
  command_.kp = 0.0f;
  command_.kd = 0.0f;
  command_.torque = 0.0f;
  command_.control = DMMotorControlCommand::kNone;
  has_mit_command_ = true;
}

inline void DMMotor::SetMITCommand(float position, float velocity, float kp,
                                   float kd, float torque) {
  const DMMotorMitLimit& limit = config_.mit_limit;
  command_.position = Clamp(position, limit.angle_min, limit.angle_max);
  command_.velocity = Clamp(velocity, limit.velocity_min, limit.velocity_max);
  command_.kp = Clamp(kp, limit.kp_min, limit.kp_max);
  command_.kd = Clamp(kd, limit.kd_min, limit.kd_max);
  command_.torque = Clamp(torque, limit.torque_min, limit.torque_max);
  command_.control = DMMotorControlCommand::kNone;
  has_mit_command_ = true;
}

inline const DMMotorConfig& DMMotor::Config() const { return config_; }

inline const DMMotorFeedback& DMMotor::GetFeedback() const { return feedback_; }

inline const DMMotorCommand& DMMotor::GetCommand() const { return command_; }

inline const DMMotorState& DMMotor::GetState() const { return state_; }

inline void DMMotor::SetReference(float reference) {
  state_.reference = reference;
}

inline void DMMotor::SetOuterLoop(DMMotorOuterLoop outer_loop) {
  if (state_.outer_loop == outer_loop) {
    return;
  }
  state_.outer_loop = outer_loop;
  speed_pid_.Reset();
  angle_pid_.Reset();
  last_control_time_us_ = 0;
}

inline void DMMotor::ChangeFeed(DMMotorOuterLoop loop,
                                DMMotorFeedbackSource source) {
  switch (loop) {
    case DMMotorOuterLoop::kAngle:
      state_.angle_feedback_source = source;
      break;
    case DMMotorOuterLoop::kSpeed:
      state_.speed_feedback_source = source;
      break;
    case DMMotorOuterLoop::kMIT:
    default:
      break;
  }
}

inline void DMMotor::SetExternalAngleFeedbackRad(float angle_rad) {
  external_angle_feedback_rad_ = angle_rad;
}

inline void DMMotor::SetExternalSpeedFeedbackRadps(float speed_radps) {
  external_speed_feedback_radps_ = speed_radps;
}

inline float DMMotor::ComputeDeltaTimeSeconds() {
  const auto now_us = LibXR::Timebase::GetMicroseconds();
  if (last_control_time_us_ == 0) {
    last_control_time_us_ = now_us;
    return kDefaultControlDtS;
  }
  const auto delta = now_us - last_control_time_us_;
  last_control_time_us_ = now_us;
  const float dt_s = delta.ToSecondf();
  if (!std::isfinite(dt_s) || dt_s <= 0.0f || dt_s > kMaxControlDtS) {
    return kDefaultControlDtS;
  }
  return dt_s;
}

inline void DMMotor::StepCascadePid(float dt_s) {
  const float angle_feedback_rad =
      (state_.angle_feedback_source == DMMotorFeedbackSource::kExternal)
          ? external_angle_feedback_rad_
          : feedback_.total_angle;
  const float speed_feedback_radps =
      (state_.speed_feedback_source == DMMotorFeedbackSource::kExternal)
          ? external_speed_feedback_radps_
          : feedback_.velocity;

  float target_torque = 0.0f;
  switch (state_.outer_loop) {
    case DMMotorOuterLoop::kAngle:
      state_.target_speed_radps =
          angle_pid_.Calculate(state_.reference, angle_feedback_rad,
                               speed_feedback_radps, dt_s);
      target_torque = speed_pid_.Calculate(state_.target_speed_radps,
                                           speed_feedback_radps, dt_s);
      break;
    case DMMotorOuterLoop::kSpeed:
      state_.target_speed_radps = state_.reference;
      target_torque = speed_pid_.Calculate(state_.reference,
                                           speed_feedback_radps, dt_s);
      break;
    case DMMotorOuterLoop::kMIT:
    default:
      return;
  }

  state_.target_torque_nm = target_torque;
  SetMITCommand(0.0f, 0.0f, 0.0f, 0.0f, target_torque);
}

inline void DMMotor::EnterSafeStop() {
  has_mit_command_ = false;
  command_.position = 0.0f;
  command_.velocity = 0.0f;
  command_.kp = 0.0f;
  command_.kd = 0.0f;
  command_.torque = 0.0f;
  command_.control = DMMotorControlCommand::kNone;
  state_.safe_stopped = true;
  state_.target_speed_radps = 0.0f;
  state_.target_torque_nm = 0.0f;
  speed_pid_.Reset();
  angle_pid_.Reset();
  last_control_time_us_ = 0;
}

inline void DMMotor::OnCanRxStatic(bool, DMMotor* self,
                                   const LibXR::CAN::ClassicPack& pack) {
  self->UpdateFeedback(pack);
}

inline float DMMotor::Clamp(float value, float min_value, float max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

inline std::uint16_t DMMotor::FloatToUInt(float value, float min_value,
                                          float max_value, std::uint8_t bits) {
  if (bits == 0u || bits > 16u) {
    return 0u;
  }

  const float clamped = Clamp(value, min_value, max_value);
  const float span = max_value - min_value;
  if (!std::isfinite(span) || span <= kEpsilon) {
    return 0u;
  }

  const std::uint32_t max_int = (1u << bits) - 1u;
  const float normalized = (clamped - min_value) / span;
  const float scaled = normalized * static_cast<float>(max_int);
  if (!std::isfinite(scaled)) {
    return 0u;
  }

  const float rounded = std::round(scaled);
  if (rounded <= 0.0f) {
    return 0u;
  }
  if (rounded >= static_cast<float>(max_int)) {
    return static_cast<std::uint16_t>(max_int);
  }
  return static_cast<std::uint16_t>(rounded);
}

inline float DMMotor::UIntToFloat(std::uint16_t value, float min_value,
                                  float max_value, std::uint8_t bits) {
  if (bits == 0u || bits > 16u) {
    return min_value;
  }

  const std::uint32_t max_int = (1u << bits) - 1u;
  const std::uint16_t clamped_value =
      static_cast<std::uint16_t>(std::min<std::uint32_t>(value, max_int));
  const float span = max_value - min_value;
  if (!std::isfinite(span) || span <= kEpsilon) {
    return min_value;
  }

  const float normalized =
      static_cast<float>(clamped_value) / static_cast<float>(max_int);
  return normalized * span + min_value;
}

inline float DMMotor::ApplyDirection(float value) const {
  return (config_.direction >= 0) ? value : -value;
}

inline void DMMotor::UpdateOnlineStatus() {
  const std::uint32_t now_ms = LibXR::Thread::GetTime();
  if (config_.feedback_timeout_ms == 0u) {
    feedback_.online = feedback_.initialized;
    return;
  }

  if (!feedback_.initialized) {
    feedback_.online = false;
    return;
  }

  feedback_.online =
      (now_ms - feedback_.last_feedback_time_ms) <= config_.feedback_timeout_ms;
}

inline void DMMotor::CheckAutoReEnable() {
  if (!state_.enabled || !feedback_.online) {
    return;
  }
  if (feedback_.motor_state == kMotorStateEnabled) {
    return;
  }
  const std::uint32_t now_ms = LibXR::Thread::GetTime();
  if (last_enable_time_ms_ != 0u &&
      (now_ms - last_enable_time_ms_) < kEnableRetryIntervalMs) {
    return;
  }
  SendControlCommand(DMMotorControlCommand::kClearError);
  SendControlCommand(DMMotorControlCommand::kEnable);
  last_enable_time_ms_ = now_ms;
}

inline void DMMotor::HandleControlCommand(DMMotorControlCommand command) {
  SendControlCommand(command);
  UpdateOnlineStatus();
}

inline void DMMotor::SendControlCommand(DMMotorControlCommand command) {
  if (command == DMMotorControlCommand::kNone) {
    return;
  }

  LibXR::CAN::ClassicPack pack{};
  pack.id = config_.tx_id;
  pack.type = LibXR::CAN::Type::STANDARD;
  pack.dlc = 8u;
  for (auto& byte : pack.data) {
    byte = 0xFFu;
  }
  pack.data[7] = static_cast<std::uint8_t>(command);
  (void)can_->AddMessage(pack);
}

inline void DMMotor::SendMITCommand() {
  const DMMotorMitLimit& limit = config_.mit_limit;
  const float directed_position = ApplyDirection(command_.position);
  const float directed_velocity = ApplyDirection(command_.velocity);
  const float directed_torque = ApplyDirection(command_.torque);

  const std::uint16_t position_raw =
      FloatToUInt(directed_position, limit.angle_min, limit.angle_max, 16u);
  const std::uint16_t velocity_raw =
      FloatToUInt(directed_velocity, limit.velocity_min, limit.velocity_max, 12u);
  const std::uint16_t kp_raw =
      FloatToUInt(command_.kp, limit.kp_min, limit.kp_max, 12u);
  const std::uint16_t kd_raw =
      FloatToUInt(command_.kd, limit.kd_min, limit.kd_max, 12u);
  const std::uint16_t torque_raw =
      FloatToUInt(directed_torque, limit.torque_min, limit.torque_max, 12u);

  LibXR::CAN::ClassicPack pack{};
  pack.id = config_.tx_id;
  pack.type = LibXR::CAN::Type::STANDARD;
  pack.dlc = 8u;
  pack.data[0] = static_cast<std::uint8_t>((position_raw >> 8) & 0xFFu);
  pack.data[1] = static_cast<std::uint8_t>(position_raw & 0xFFu);
  pack.data[2] = static_cast<std::uint8_t>((velocity_raw >> 4) & 0xFFu);
  pack.data[3] =
      static_cast<std::uint8_t>(((velocity_raw & 0x0Fu) << 4) |
                                ((kp_raw >> 8) & 0x0Fu));
  pack.data[4] = static_cast<std::uint8_t>(kp_raw & 0xFFu);
  pack.data[5] = static_cast<std::uint8_t>((kd_raw >> 4) & 0xFFu);
  pack.data[6] = static_cast<std::uint8_t>(((kd_raw & 0x0Fu) << 4) |
                                           ((torque_raw >> 8) & 0x0Fu));
  pack.data[7] = static_cast<std::uint8_t>(torque_raw & 0xFFu);
  (void)can_->AddMessage(pack);
}

inline void DMMotor::UpdateFeedback(const LibXR::CAN::ClassicPack& pack) {
  if (pack.dlc < 8u) {
    XR_LOG_WARN("DMMotor feedback dlc invalid: %u", pack.dlc);
    return;
  }

  const DMMotorMitLimit& limit = config_.mit_limit;
  const std::uint8_t packed_state = pack.data[0];
  const std::uint16_t angle_raw =
      static_cast<std::uint16_t>((pack.data[1] << 8) | pack.data[2]);
  const std::uint16_t velocity_raw =
      static_cast<std::uint16_t>((pack.data[3] << 4) | (pack.data[4] >> 4));
  const std::uint16_t torque_raw = static_cast<std::uint16_t>(
      ((pack.data[4] & 0x0Fu) << 8) | pack.data[5]);

  float angle = UIntToFloat(angle_raw, limit.angle_min, limit.angle_max, 16u);
  float velocity =
      UIntToFloat(velocity_raw, limit.velocity_min, limit.velocity_max, 12u);
  float torque = UIntToFloat(torque_raw, limit.torque_min, limit.torque_max, 12u);

  angle = ApplyDirection(angle);
  velocity = ApplyDirection(velocity);
  torque = ApplyDirection(torque);

  const float angle_span = limit.angle_max - limit.angle_min;
  const float half_span = angle_span * 0.5f;
  if (!feedback_.initialized) {
    feedback_.initialized = true;
    feedback_.total_round = 0;
    feedback_.total_angle = angle;
  } else {
    float delta = angle - last_angle_;
    if (delta > half_span) {
      delta -= angle_span;
      feedback_.total_round--;
    } else if (delta < -half_span) {
      delta += angle_span;
      feedback_.total_round++;
    }
    feedback_.total_angle += delta;
  }

  last_angle_ = angle;
  feedback_.online = true;
  feedback_.motor_id = static_cast<std::uint8_t>(packed_state & 0x0Fu);
  feedback_.motor_state = static_cast<std::uint8_t>((packed_state >> 4) & 0x0Fu);
  feedback_.angle = angle;
  feedback_.velocity = velocity;
  feedback_.torque = torque;
  feedback_.mos_temp = static_cast<float>(pack.data[6]);
  feedback_.rotor_temp = static_cast<float>(pack.data[7]);
  feedback_.last_feedback_time_ms = LibXR::Thread::GetTime();
}

inline void DMMotor::CheckAndNormalizeLimits() {
  auto normalize_range = [](float& min_value, float& max_value, float default_min,
                            float default_max) {
    if (!std::isfinite(min_value) || !std::isfinite(max_value) ||
        (max_value - min_value) <= kEpsilon) {
      min_value = default_min;
      max_value = default_max;
    }
  };

  DMMotorMitLimit& limit = config_.mit_limit;
  normalize_range(limit.angle_min, limit.angle_max, -12.5f, 12.5f);
  normalize_range(limit.velocity_min, limit.velocity_max, -45.0f, 45.0f);
  normalize_range(limit.torque_min, limit.torque_max, -18.0f, 18.0f);
  normalize_range(limit.kp_min, limit.kp_max, 0.0f, 500.0f);
  normalize_range(limit.kd_min, limit.kd_max, 0.0f, 5.0f);
}
