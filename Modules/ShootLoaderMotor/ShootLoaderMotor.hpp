#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: ShootLoaderMotor 是一个固定接入 can2 的单 M3508 拨盘执行器骨架模块 / ShootLoaderMotor is a fixed-can2 single M3508 loader actuator skeleton
constructor_args:
  - motor_id: 3
  - command_frame_id: 512
  - continuous_speed_rpm: 0.0
  - reverse_speed_rpm: 0.0
  - single_step_speed_rpm: 0.0
  - single_step_angle_deg: 0.0
  - monitor_timeout_ms: 100
template_args: []
required_hardware: can2
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstdint>

#include "app_framework.hpp"
#include "can.hpp"
#include "libxr_cb.hpp"

enum class ShootLoaderMode : uint8_t {
  kStop = 0,
  kSingle = 1,
  kContinuous = 2,
  kReverse = 3,
};

struct ShootLoaderMotorState {
  bool online = false;
  bool enabled = false;
  bool active = false;
  bool safe_stopped = true;
  ShootLoaderMode mode = ShootLoaderMode::kStop;
  float target_speed_rpm = 0.0f;
  float feedback_speed_rpm = 0.0f;
  float target_step_angle_deg = 0.0f;
  float feedback_angle_deg = 0.0f;
  std::uint16_t feedback_angle_raw = 0;
  std::uint8_t feedback_temperature = 0;
};

class ShootLoaderMotor : public LibXR::Application {
 public:
  static constexpr const char* kStateTopicName = "shoot_loader_motor_state";
  static constexpr const char* kDefaultCanName = "can2";

  ShootLoaderMotor(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
                   uint8_t motor_id, uint32_t command_frame_id,
                   float continuous_speed_rpm, float reverse_speed_rpm,
                   float single_step_speed_rpm, float single_step_angle_deg,
                   uint32_t monitor_timeout_ms)
      : can_(hw.template FindOrExit<LibXR::CAN>({kDefaultCanName})),
        state_topic_(LibXR::Topic::CreateTopic<ShootLoaderMotorState>(
            kStateTopicName, nullptr, false, true, true)),
        motor_id_(motor_id),
        command_frame_id_(command_frame_id),
        continuous_speed_rpm_(continuous_speed_rpm),
        reverse_speed_rpm_(reverse_speed_rpm),
        single_step_speed_rpm_(single_step_speed_rpm),
        single_step_angle_deg_(single_step_angle_deg),
        monitor_timeout_ms_(monitor_timeout_ms) {
    can_rx_callback_ = LibXR::CAN::Callback::Create(OnCanRxStatic, this);
    can_->Register(can_rx_callback_, LibXR::CAN::Type::STANDARD,
                   LibXR::CAN::FilterMode::ID_RANGE, motor_id_, motor_id_);
    state_cache_.online = true;
    app.Register(*this);
  }

  void OnMonitor() override {
    const uint32_t now_ms = LibXR::Thread::GetTime();
    if (enabled_ && monitor_timeout_ms_ > 0 &&
        (now_ms - last_command_time_ms_) > monitor_timeout_ms_) {
      SafeStop();
    }

    if (monitor_timeout_ms_ > 0 &&
        (now_ms - last_feedback_time_ms_) > monitor_timeout_ms_) {
      state_cache_.online = false;
    }

    UpdateCommandCache();
    PublishState();
    SendCurrentCommand(0);
  }

  void Enable() {
    enabled_ = true;
    state_cache_.safe_stopped = false;
    last_command_time_ms_ = LibXR::Thread::GetTime();
  }

  void Disable() { SafeStop(); }

  void SafeStop() {
    enabled_ = false;
    mode_ = ShootLoaderMode::kStop;
    state_cache_.safe_stopped = true;
    state_cache_.active = false;
    state_cache_.target_speed_rpm = 0.0f;
  }

  void SetMode(ShootLoaderMode mode) {
    mode_ = mode;
    last_command_time_ms_ = LibXR::Thread::GetTime();
  }

  void SetSpeedConfig(float continuous_speed_rpm, float reverse_speed_rpm,
                      float single_step_speed_rpm) {
    continuous_speed_rpm_ = continuous_speed_rpm;
    reverse_speed_rpm_ = reverse_speed_rpm;
    single_step_speed_rpm_ = single_step_speed_rpm;
  }

  void SetSingleStepAngleDeg(float single_step_angle_deg) {
    single_step_angle_deg_ = single_step_angle_deg;
  }

  void UpdateFeedback(float speed_rpm, float angle_deg, bool online) {
    state_cache_.feedback_speed_rpm = speed_rpm;
    state_cache_.feedback_angle_deg = angle_deg;
    state_cache_.online = online;
  }

  LibXR::Topic& StateTopic() { return state_topic_; }

  const LibXR::Topic& StateTopic() const { return state_topic_; }

 private:
  static void OnCanRxStatic(bool, ShootLoaderMotor* self,
                            const LibXR::CAN::ClassicPack& pack) {
    self->OnCanFeedback(pack);
  }

  static int ResolveCurrentSlot(uint8_t motor_id) {
    if (motor_id >= 1 && motor_id <= 4) {
      return static_cast<int>(motor_id - 1);
    }
    if (motor_id >= 5 && motor_id <= 8) {
      return static_cast<int>(motor_id - 5);
    }
    return -1;
  }

  void OnCanFeedback(const LibXR::CAN::ClassicPack& pack) {
    if (pack.id != motor_id_ || pack.dlc < 7) {
      return;
    }

    state_cache_.feedback_angle_raw =
        static_cast<std::uint16_t>((pack.data[0] << 8) | pack.data[1]);
    state_cache_.feedback_speed_rpm = static_cast<float>(static_cast<std::int16_t>(
        (pack.data[2] << 8) | pack.data[3]));
    state_cache_.feedback_temperature = pack.data[6];
    state_cache_.feedback_angle_deg = static_cast<float>(state_cache_.feedback_angle_raw) *
                                      (360.0f / 8192.0f);
    state_cache_.online = true;
    last_feedback_time_ms_ = LibXR::Thread::GetTime();
  }

  void UpdateCommandCache() {
    state_cache_.enabled = enabled_;
    state_cache_.mode = enabled_ ? mode_ : ShootLoaderMode::kStop;
    state_cache_.target_step_angle_deg = single_step_angle_deg_;

    if (!enabled_) {
      state_cache_.active = false;
      state_cache_.target_speed_rpm = 0.0f;
      return;
    }

    switch (mode_) {
      case ShootLoaderMode::kSingle:
        state_cache_.active = true;
        state_cache_.target_speed_rpm = single_step_speed_rpm_;
        break;
      case ShootLoaderMode::kContinuous:
        state_cache_.active = true;
        state_cache_.target_speed_rpm = continuous_speed_rpm_;
        break;
      case ShootLoaderMode::kReverse:
        state_cache_.active = true;
        state_cache_.target_speed_rpm = reverse_speed_rpm_;
        break;
      case ShootLoaderMode::kStop:
      default:
        state_cache_.active = false;
        state_cache_.target_speed_rpm = 0.0f;
        break;
    }
  }

  void PublishState() { state_topic_.Publish(state_cache_); }

  void SendCurrentCommand(int16_t current) {
    LibXR::CAN::ClassicPack pack{};
    pack.id = command_frame_id_;
    pack.type = LibXR::CAN::Type::STANDARD;
    pack.dlc = 8;
    for (auto& byte : pack.data) {
      byte = 0;
    }

    const int slot = ResolveCurrentSlot(motor_id_);
    if (slot >= 0) {
      pack.data[slot * 2] = static_cast<uint8_t>((current >> 8) & 0xFF);
      pack.data[slot * 2 + 1] = static_cast<uint8_t>(current & 0xFF);
    }
    (void)can_->AddMessage(pack);
  }

  LibXR::CAN* can_ = nullptr;
  LibXR::Topic state_topic_;
  LibXR::CAN::Callback can_rx_callback_;

  uint8_t motor_id_ = 0;
  uint32_t command_frame_id_ = 0;
  float continuous_speed_rpm_ = 0.0f;
  float reverse_speed_rpm_ = 0.0f;
  float single_step_speed_rpm_ = 0.0f;
  float single_step_angle_deg_ = 0.0f;
  uint32_t monitor_timeout_ms_ = 0;
  uint32_t last_command_time_ms_ = 0;
  uint32_t last_feedback_time_ms_ = 0;
  bool enabled_ = false;
  ShootLoaderMode mode_ = ShootLoaderMode::kStop;

  ShootLoaderMotorState state_cache_{};
};
