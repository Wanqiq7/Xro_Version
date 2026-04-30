#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: VT13 是一个基于 UART 解析 rmpp VT13 协议并发布设备级私有状态的模块 / VT13 parses the rmpp VT13 UART protocol and publishes device-level private state
constructor_args:
  - uart_name: "uart_vt13"
  - task_stack_depth: 2048
template_args: []
required_hardware: uart_name
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "app_framework.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "message.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "uart.hpp"
#include "utils/crc.hpp"

using LibXR::ErrorCode;

enum class VT13Mode : uint8_t {
  kC = 0,
  kN = 1,
  kS = 2,
  kErr = 3,
};

struct VT13MouseState {
  float yaw = 0.0f;
  float pitch = 0.0f;
  int16_t wheel = 0;
  bool left = false;
  bool right = false;
  bool middle = false;
};

struct VT13KeyState {
  bool w = false;
  bool s = false;
  bool a = false;
  bool d = false;
  bool shift = false;
  bool ctrl = false;
  bool q = false;
  bool e = false;
  bool r = false;
  bool f = false;
  bool g = false;
  bool z = false;
  bool x = false;
  bool c = false;
  bool v = false;
  bool b = false;
};

/**
 * @brief VT13 设备级状态
 *
 * 该结构保持 VT13 协议的原始语义边界，不与其他遥控器状态强行统一，
 * 仅作为模块私有强类型 Topic 输出。
 */
struct VT13State {
  bool online = false;

  float x = 0.0f;
  float y = 0.0f;
  float pitch = 0.0f;
  float yaw = 0.0f;
  float wheel = 0.0f;

  VT13Mode mode = VT13Mode::kErr;
  bool pause = false;
  bool trigger = false;
  bool fn = false;
  bool photo = false;

  VT13MouseState mouse{};
  VT13KeyState key{};
};

class VT13 : public LibXR::Application {
 public:
  static constexpr const char* kTopicName = "vt13_state";
  static constexpr const char* kDefaultUartName = "uart_vt13";
  static constexpr std::size_t kDefaultTaskStackDepth = 2048;
  static constexpr const char* kDefaultThreadName = "VT13::Rx";

  struct Config {
    const char* uart_name = kDefaultUartName;
    std::size_t task_stack_depth = kDefaultTaskStackDepth;
    const char* state_topic_name = kTopicName;
    const char* thread_name = kDefaultThreadName;
    std::uint32_t offline_timeout_ms = 500;
    float joystick_deadband = 0.05f;
  };

  VT13(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
       const char* uart_name, std::size_t task_stack_depth)
      : VT13(hw, app,
             Config{uart_name, task_stack_depth, kTopicName, kDefaultThreadName,
                    500, 0.05f}) {}

  VT13(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
       const Config& config)
      : uart_(hw.template FindOrExit<LibXR::UART>({config.uart_name})),
        config_(config),
        state_topic_(LibXR::Topic::CreateTopic<VT13State>(
            config.state_topic_name, nullptr, false, true, true)),
        rx_chunk_(rx_buffer_, sizeof(rx_buffer_)) {
    uart_thread_.Create(this, UartThreadEntry, config.thread_name,
                        config.task_stack_depth,
                        LibXR::Thread::Priority::REALTIME);

    PublishOfflineState();
    app.Register(*this);
  }

  void OnMonitor() override {
    if (!published_state_.online) {
      return;
    }

    const std::uint32_t now_ms = LibXR::Thread::GetTime();
    if ((now_ms - last_frame_time_ms_) > config_.offline_timeout_ms) {
      PublishOfflineState();
    }
  }

  LibXR::Topic& StateTopic() { return state_topic_; }

  const LibXR::Topic& StateTopic() const { return state_topic_; }

 private:
  static constexpr std::size_t kFrameLength = 21;
  static constexpr std::size_t kRxChunkSize = 42;
  static constexpr std::size_t kRxWindowSize = 84;
  static constexpr std::uint8_t kFrameSof1 = 0xA9;
  static constexpr std::uint8_t kFrameSof2 = 0x53;
  static constexpr std::int16_t kJoystickCenter = 1024;
  static constexpr float kJoystickScale = 660.0f;
  static constexpr std::int16_t kMouseMax = 500;

  struct __attribute__((packed)) RawFrame {
    std::uint8_t sof_1 : 8;
    std::uint8_t sof_2 : 8;
    std::uint16_t ch_0 : 11;
    std::uint16_t ch_1 : 11;
    std::uint16_t ch_2 : 11;
    std::uint16_t ch_3 : 11;
    std::uint8_t mode_sw : 2;
    std::uint8_t pause : 1;
    std::uint8_t fn_1 : 1;
    std::uint8_t fn_2 : 1;
    std::uint16_t wheel : 11;
    std::uint8_t trigger : 1;
    std::uint8_t reserved_3bit : 3;
    std::int16_t mouse_x : 16;
    std::int16_t mouse_y : 16;
    std::int16_t mouse_z : 16;
    std::uint8_t mouse_left : 2;
    std::uint8_t mouse_right : 2;
    std::uint8_t mouse_middle : 2;
    std::uint8_t reserved_2bit : 2;
    std::uint16_t key : 16;
    std::uint16_t crc16 : 16;
  };

  static_assert(sizeof(RawFrame) == kFrameLength, "VT13 frame size mismatch");

  static void UartThreadEntry(VT13* self) {
    LibXR::Semaphore semaphore;
    LibXR::ReadOperation read_operation(semaphore);

    while (true) {
      self->uart_->Read({nullptr, 0}, read_operation);

      const std::size_t readable_size =
          LibXR::min(self->uart_->read_port_->Size(), self->rx_chunk_.size_);
      if (readable_size == 0) {
        continue;
      }

      const auto result = self->uart_->Read(
          LibXR::RawData{self->rx_chunk_.addr_, readable_size}, read_operation);
      if (result != ErrorCode::OK) {
        continue;
      }

      self->PushBytes(static_cast<const std::uint8_t*>(self->rx_chunk_.addr_),
                      readable_size);
      self->TryParseFrames();
    }
  }

  void PushBytes(const std::uint8_t* data, std::size_t size) {
    if (size >= kRxWindowSize) {
      const auto* tail = data + size - kRxWindowSize;
      std::memcpy(rx_window_, tail, kRxWindowSize);
      rx_window_size_ = kRxWindowSize;
      return;
    }

    if ((rx_window_size_ + size) > kRxWindowSize) {
      const std::size_t overflow = rx_window_size_ + size - kRxWindowSize;
      std::memmove(rx_window_, rx_window_ + overflow, rx_window_size_ - overflow);
      rx_window_size_ -= overflow;
    }

    std::memcpy(rx_window_ + rx_window_size_, data, size);
    rx_window_size_ += size;
  }

  void ConsumeBytes(std::size_t size) {
    if (size >= rx_window_size_) {
      rx_window_size_ = 0;
      return;
    }

    std::memmove(rx_window_, rx_window_ + size, rx_window_size_ - size);
    rx_window_size_ -= size;
  }

  void TryParseFrames() {
    while (rx_window_size_ >= kFrameLength) {
      VT13State next_state{};
      if (TryDecodeFrame(rx_window_, next_state)) {
        PublishOnlineState(next_state);
        ConsumeBytes(kFrameLength);
        continue;
      }

      ConsumeBytes(1);
    }
  }

  bool TryDecodeFrame(const std::uint8_t* frame, VT13State& out_state) const {
    if (frame[0] != kFrameSof1 || frame[1] != kFrameSof2) {
      return false;
    }

    const std::uint16_t expected_crc16 =
        LibXR::CRC16::Calculate(frame, kFrameLength - 2);
    const std::uint16_t received_crc16 = static_cast<std::uint16_t>(
        frame[kFrameLength - 2] | (frame[kFrameLength - 1] << 8));
    if (expected_crc16 != received_crc16) {
      return false;
    }

    RawFrame raw{};
    std::memcpy(&raw, frame, sizeof(raw));

    out_state.online = true;
    out_state.x = NormalizeJoystick(raw.ch_1);
    out_state.y = -NormalizeJoystick(raw.ch_0);
    out_state.pitch = NormalizeJoystick(raw.ch_2);
    out_state.yaw = -NormalizeJoystick(raw.ch_3);
    out_state.wheel = -NormalizeJoystick(raw.wheel);
    out_state.mode = DecodeMode(raw.mode_sw);
    out_state.pause = raw.pause != 0;
    out_state.trigger = raw.trigger != 0;
    out_state.fn = raw.fn_1 != 0;
    out_state.photo = raw.fn_2 != 0;

    out_state.mouse.yaw =
        -ClampUnit(static_cast<float>(raw.mouse_x) / static_cast<float>(kMouseMax));
    out_state.mouse.pitch =
        ClampUnit(static_cast<float>(raw.mouse_y) / static_cast<float>(kMouseMax));
    out_state.mouse.wheel = raw.mouse_z;
    out_state.mouse.left = raw.mouse_left != 0;
    out_state.mouse.right = raw.mouse_right != 0;
    out_state.mouse.middle = raw.mouse_middle != 0;

    out_state.key.w = (raw.key & (1u << 0u)) != 0u;
    out_state.key.s = (raw.key & (1u << 1u)) != 0u;
    out_state.key.a = (raw.key & (1u << 2u)) != 0u;
    out_state.key.d = (raw.key & (1u << 3u)) != 0u;
    out_state.key.shift = (raw.key & (1u << 4u)) != 0u;
    out_state.key.ctrl = (raw.key & (1u << 5u)) != 0u;
    out_state.key.q = (raw.key & (1u << 6u)) != 0u;
    out_state.key.e = (raw.key & (1u << 7u)) != 0u;
    out_state.key.r = (raw.key & (1u << 8u)) != 0u;
    out_state.key.f = (raw.key & (1u << 9u)) != 0u;
    out_state.key.g = (raw.key & (1u << 10u)) != 0u;
    out_state.key.z = (raw.key & (1u << 11u)) != 0u;
    out_state.key.x = (raw.key & (1u << 12u)) != 0u;
    out_state.key.c = (raw.key & (1u << 13u)) != 0u;
    out_state.key.v = (raw.key & (1u << 14u)) != 0u;
    out_state.key.b = (raw.key & (1u << 15u)) != 0u;
    return true;
  }

  VT13Mode DecodeMode(std::uint8_t raw_mode) const {
    switch (raw_mode) {
      case 0:
        return VT13Mode::kC;
      case 1:
        return VT13Mode::kN;
      case 2:
        return VT13Mode::kS;
      default:
        return VT13Mode::kErr;
    }
  }

  float NormalizeJoystick(std::uint16_t value) const {
    const float normalized =
        (static_cast<float>(value) - static_cast<float>(kJoystickCenter)) /
        kJoystickScale;
    const float clamped = ClampUnit(normalized);
    return (Abs(clamped) < config_.joystick_deadband) ? 0.0f : clamped;
  }

  static float ClampUnit(float value) {
    if (value > 1.0f) {
      return 1.0f;
    }
    if (value < -1.0f) {
      return -1.0f;
    }
    return value;
  }

  static float Abs(float value) { return value >= 0.0f ? value : -value; }

  void PublishOnlineState(const VT13State& state) {
    last_frame_time_ms_ = LibXR::Thread::GetTime();
    published_state_ = state;
    state_topic_.Publish(published_state_);
  }

  void PublishOfflineState() {
    published_state_ = {};
    state_topic_.Publish(published_state_);
  }

  LibXR::UART* uart_ = nullptr;
  Config config_{};
  LibXR::Topic state_topic_;
  LibXR::Thread uart_thread_;
  std::uint8_t rx_buffer_[kRxChunkSize]{};
  LibXR::RawData rx_chunk_;
  std::uint8_t rx_window_[kRxWindowSize]{};
  std::size_t rx_window_size_ = 0;
  VT13State published_state_{};
  std::uint32_t last_frame_time_ms_ = 0;
};
