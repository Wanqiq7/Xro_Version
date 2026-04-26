#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: ShootFrictionMotorGroup 是一个固定接入 can2 的双 M3508 摩擦轮执行器骨架模块 / ShootFrictionMotorGroup is a fixed-can2 dual M3508 friction wheel actuator skeleton
constructor_args:
  - left_motor_id: 1
  - right_motor_id: 2
  - command_frame_id: 512
  - bullet_speed_to_wheel_rpm_scale: 1.0
  - monitor_timeout_ms: 100
template_args: []
required_hardware: can2
depends: []
=== END MANIFEST === */
// clang-format on

#include <array>
#include <cstdint>

#include "app_framework.hpp"
#include "can.hpp"
#include "libxr_cb.hpp"

struct ShootFrictionMotorGroupState {
  bool online = false;
  bool enabled = false;
  bool safe_stopped = true;
  float target_bullet_speed_mps = 0.0f;
  float target_left_speed_rpm = 0.0f;
  float target_right_speed_rpm = 0.0f;
  float feedback_left_speed_rpm = 0.0f;
  float feedback_right_speed_rpm = 0.0f;
  std::uint16_t feedback_left_angle_raw = 0;
  std::uint16_t feedback_right_angle_raw = 0;
  std::uint8_t feedback_left_temperature = 0;
  std::uint8_t feedback_right_temperature = 0;
};

class ShootFrictionMotorGroup : public LibXR::Application {
 public:
  static constexpr const char* kStateTopicName = "shoot_friction_motor_group_state";
  static constexpr const char* kDefaultCanName = "can2";

  ShootFrictionMotorGroup(LibXR::HardwareContainer& hw,
                          LibXR::ApplicationManager& app,
                          uint8_t left_motor_id,
                          uint8_t right_motor_id,
                          uint32_t command_frame_id,
                          float bullet_speed_to_wheel_rpm_scale,
                          uint32_t monitor_timeout_ms)
      : can_(hw.template FindOrExit<LibXR::CAN>({kDefaultCanName})),
        state_topic_(LibXR::Topic::CreateTopic<ShootFrictionMotorGroupState>(
            kStateTopicName, nullptr, false, true, true)),
        motor_ids_{left_motor_id, right_motor_id},
        command_frame_id_(command_frame_id),
        bullet_speed_to_wheel_rpm_scale_(bullet_speed_to_wheel_rpm_scale),
        monitor_timeout_ms_(monitor_timeout_ms) {
    can_rx_callback_ = LibXR::CAN::Callback::Create(OnCanRxStatic, this);
    can_->Register(can_rx_callback_, LibXR::CAN::Type::STANDARD,
                   LibXR::CAN::FilterMode::ID_RANGE, motor_ids_[0], motor_ids_[0]);
    can_->Register(can_rx_callback_, LibXR::CAN::Type::STANDARD,
                   LibXR::CAN::FilterMode::ID_RANGE, motor_ids_[1], motor_ids_[1]);
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

    PublishState();
    SendCurrentCommand(0, 0);
  }

  void Enable() {
    enabled_ = true;
    state_cache_.safe_stopped = false;
    last_command_time_ms_ = LibXR::Thread::GetTime();
  }

  void Disable() { SafeStop(); }

  void SafeStop() {
    enabled_ = false;
    state_cache_.safe_stopped = true;
    state_cache_.target_bullet_speed_mps = 0.0f;
    state_cache_.target_left_speed_rpm = 0.0f;
    state_cache_.target_right_speed_rpm = 0.0f;
  }

  void SetTargetBulletSpeedMps(float target_bullet_speed_mps) {
    state_cache_.target_bullet_speed_mps = target_bullet_speed_mps;
    SetTargetWheelSpeedRpm(target_bullet_speed_mps * bullet_speed_to_wheel_rpm_scale_,
                           -target_bullet_speed_mps * bullet_speed_to_wheel_rpm_scale_);
  }

  void SetTargetWheelSpeedRpm(float left_speed_rpm, float right_speed_rpm) {
    state_cache_.target_left_speed_rpm = left_speed_rpm;
    state_cache_.target_right_speed_rpm = right_speed_rpm;
    last_command_time_ms_ = LibXR::Thread::GetTime();
  }

  void UpdateFeedback(float left_speed_rpm, float right_speed_rpm, bool online) {
    state_cache_.feedback_left_speed_rpm = left_speed_rpm;
    state_cache_.feedback_right_speed_rpm = right_speed_rpm;
    state_cache_.online = online;
  }

  LibXR::Topic& StateTopic() { return state_topic_; }

  const LibXR::Topic& StateTopic() const { return state_topic_; }

 private:
  static void OnCanRxStatic(bool, ShootFrictionMotorGroup* self,
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
    if (pack.dlc < 7) {
      return;
    }

    const std::uint16_t angle_raw =
        static_cast<std::uint16_t>((pack.data[0] << 8) | pack.data[1]);
    const std::int16_t speed_raw =
        static_cast<std::int16_t>((pack.data[2] << 8) | pack.data[3]);
    const std::uint8_t temperature = pack.data[6];

    if (pack.id == motor_ids_[0]) {
      state_cache_.feedback_left_angle_raw = angle_raw;
      state_cache_.feedback_left_speed_rpm = static_cast<float>(speed_raw);
      state_cache_.feedback_left_temperature = temperature;
    } else if (pack.id == motor_ids_[1]) {
      state_cache_.feedback_right_angle_raw = angle_raw;
      state_cache_.feedback_right_speed_rpm = static_cast<float>(speed_raw);
      state_cache_.feedback_right_temperature = temperature;
    } else {
      return;
    }

    last_feedback_time_ms_ = LibXR::Thread::GetTime();
    state_cache_.online = true;
  }

  void PublishState() {
    state_cache_.enabled = enabled_;
    if (!enabled_) {
      state_cache_.safe_stopped = true;
    }
    state_topic_.Publish(state_cache_);
  }

  void SendCurrentCommand(int16_t left_current, int16_t right_current) {
    LibXR::CAN::ClassicPack pack{};
    pack.id = command_frame_id_;
    pack.type = LibXR::CAN::Type::STANDARD;
    pack.dlc = 8;
    for (auto& byte : pack.data) {
      byte = 0;
    }

    const int left_slot = ResolveCurrentSlot(motor_ids_[0]);
    const int right_slot = ResolveCurrentSlot(motor_ids_[1]);
    if (left_slot >= 0) {
      pack.data[left_slot * 2] = static_cast<uint8_t>((left_current >> 8) & 0xFF);
      pack.data[left_slot * 2 + 1] = static_cast<uint8_t>(left_current & 0xFF);
    }
    if (right_slot >= 0) {
      pack.data[right_slot * 2] = static_cast<uint8_t>((right_current >> 8) & 0xFF);
      pack.data[right_slot * 2 + 1] = static_cast<uint8_t>(right_current & 0xFF);
    }
    (void)can_->AddMessage(pack);
  }

  LibXR::CAN* can_ = nullptr;
  LibXR::Topic state_topic_;
  LibXR::CAN::Callback can_rx_callback_;

  std::array<uint8_t, 2> motor_ids_{};
  uint32_t command_frame_id_ = 0;
  float bullet_speed_to_wheel_rpm_scale_ = 0.0f;
  uint32_t monitor_timeout_ms_ = 0;
  uint32_t last_command_time_ms_ = 0;
  uint32_t last_feedback_time_ms_ = 0;
  bool enabled_ = false;

  ShootFrictionMotorGroupState state_cache_{};
};
