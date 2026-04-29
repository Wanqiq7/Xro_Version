#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: DT7 是一个基于 UART 解析 DJI DT7/DBUS 18 字节帧并发布最小输入状态的模块 / DT7 parses DJI DT7/DBUS frames from UART and publishes minimal input state
constructor_args:
  - uart_name: "usart3"
  - task_stack_depth: 2048
template_args: []
required_hardware: uart_name
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstdint>
#include <cstring>

#include "app_framework.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "message.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "uart.hpp"

enum class RemoteSwitchPosition : uint8_t {
  kUnknown = 0,
  kUp = 1,
  kDown = 2,
  kMiddle = 3,
};

/**
 * @brief DT7 遥控器最小状态
 *
 * 仅保留当前 App 输入链路所需的最小强类型输入信息。
 * 该结构由 DT7 模块私有发布，不进入 App/Topics 公共契约。
 */
struct DT7State {
  bool online = false;

  int16_t right_x = 0;
  int16_t right_y = 0;
  int16_t left_x = 0;
  int16_t left_y = 0;
  int16_t dial = 0;

  RemoteSwitchPosition right_switch = RemoteSwitchPosition::kUnknown;
  RemoteSwitchPosition left_switch = RemoteSwitchPosition::kUnknown;

  int16_t mouse_x = 0;
  int16_t mouse_y = 0;
  int16_t mouse_z = 0;
  bool mouse_left_pressed = false;
  bool mouse_right_pressed = false;
  uint16_t keyboard_key_code = 0;
};

class DT7 : public LibXR::Application {
 public:
  static constexpr const char* kTopicName = "remote_control_state";
  static constexpr const char* kDefaultUartName = "usart3";
  static constexpr size_t kDefaultTaskStackDepth = 2048;
  static constexpr const char* kDefaultThreadName = "DT7::Rx";

  struct Config {
    const char* uart_name = kDefaultUartName;
    size_t task_stack_depth = kDefaultTaskStackDepth;
    const char* state_topic_name = kTopicName;
    const char* thread_name = kDefaultThreadName;
  };

  DT7(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
      const char* uart_name, size_t task_stack_depth)
      : DT7(hw, app,
            Config{uart_name, task_stack_depth, kTopicName,
                   kDefaultThreadName}) {}

  DT7(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
      const Config& config)
      : uart_(hw.template Find<LibXR::UART>(config.uart_name)),
        state_topic_(LibXR::Topic::CreateTopic<DT7State>(
            config.state_topic_name, nullptr, false, true, true)) {
    ASSERT(uart_ != nullptr);

    uart_thread_.Create(this, UartThreadEntry, config.thread_name,
                        config.task_stack_depth,
                        LibXR::Thread::Priority::REALTIME);

    app.Register(*this);
  }

  void OnMonitor() override {
    if (!published_state_.online) {
      return;
    }

    const uint32_t now_ms = LibXR::Thread::GetTime();
    if ((now_ms - last_frame_time_ms_) > kOfflineTimeoutMs) {
      PublishOfflineState();
    }
  }

  LibXR::Topic& StateTopic() { return state_topic_; }

  const LibXR::Topic& StateTopic() const { return state_topic_; }

 private:
  static constexpr size_t kFrameLength = 18;
  static constexpr size_t kRxChunkSize = 36;
  static constexpr size_t kRxWindowSize = 72;
  static constexpr uint32_t kOfflineTimeoutMs = 100;
  static constexpr int16_t kChannelCenter = 1024;
  static constexpr int16_t kChannelBound = 900;

  static void UartThreadEntry(DT7* self) {
    LibXR::Semaphore semaphore;
    LibXR::ReadOperation op(semaphore);

    while (true) {
      self->uart_->Read({nullptr, 0}, op);

      const size_t readable_size =
          LibXR::min(self->uart_->read_port_->Size(), self->rx_chunk_.size_);
      if (readable_size == 0) {
        continue;
      }

      auto ans = self->uart_->Read(
          LibXR::RawData{self->rx_chunk_.addr_, readable_size}, op);
      if (ans != ErrorCode::OK) {
        continue;
      }

      self->PushBytes(static_cast<const uint8_t*>(self->rx_chunk_.addr_),
                      readable_size);
      self->TryParseFrames();
    }
  }

  void PushBytes(const uint8_t* data, size_t size) {
    if (size >= kRxWindowSize) {
      const uint8_t* tail = data + size - kRxWindowSize;
      std::memcpy(rx_window_, tail, kRxWindowSize);
      rx_window_size_ = kRxWindowSize;
      return;
    }

    if ((rx_window_size_ + size) > kRxWindowSize) {
      const size_t overflow = rx_window_size_ + size - kRxWindowSize;
      std::memmove(rx_window_, rx_window_ + overflow, rx_window_size_ - overflow);
      rx_window_size_ -= overflow;
    }

    std::memcpy(rx_window_ + rx_window_size_, data, size);
    rx_window_size_ += size;
  }

  void TryParseFrames() {
    while (rx_window_size_ >= kFrameLength) {
      DT7State parsed_state{};
      if (TryDecodeFrame(rx_window_, parsed_state)) {
        PublishOnlineState(parsed_state);
        ConsumeBytes(kFrameLength);
        continue;
      }

      ConsumeBytes(1);
    }
  }

  void ConsumeBytes(size_t size) {
    if (size >= rx_window_size_) {
      rx_window_size_ = 0;
      return;
    }

    std::memmove(rx_window_, rx_window_ + size, rx_window_size_ - size);
    rx_window_size_ -= size;
  }

  bool TryDecodeFrame(const uint8_t* frame, DT7State& out_state) const {
    const int16_t ch0 =
        static_cast<int16_t>(((frame[0] | (frame[1] << 8)) & 0x07FF)) -
        kChannelCenter;
    const int16_t ch1 =
        static_cast<int16_t>((((frame[1] >> 3) | (frame[2] << 5)) & 0x07FF)) -
        kChannelCenter;
    const int16_t ch2 = static_cast<int16_t>(
                            (((frame[2] >> 6) | (frame[3] << 2) |
                              (frame[4] << 10)) &
                             0x07FF)) -
                        kChannelCenter;
    const int16_t ch3 =
        static_cast<int16_t>((((frame[4] >> 1) | (frame[5] << 7)) & 0x07FF)) -
        kChannelCenter;
    const int16_t dial =
        static_cast<int16_t>(((frame[16] | (frame[17] << 8)) & 0x07FF)) -
        kChannelCenter;

    const RemoteSwitchPosition right_switch =
        DecodeSwitch(static_cast<uint8_t>((frame[5] >> 4) & 0x03));
    const RemoteSwitchPosition left_switch =
        DecodeSwitch(static_cast<uint8_t>((frame[5] >> 6) & 0x03));

    if (!IsValidChannel(ch0) || !IsValidChannel(ch1) || !IsValidChannel(ch2) ||
        !IsValidChannel(ch3) || !IsValidChannel(dial) ||
        !IsValidSwitch(right_switch) || !IsValidSwitch(left_switch)) {
      return false;
    }

    out_state.online = true;
    out_state.right_x = ch0;
    out_state.right_y = ch1;
    out_state.left_x = ch2;
    out_state.left_y = ch3;
    out_state.dial = dial;
    out_state.right_switch = right_switch;
    out_state.left_switch = left_switch;
    out_state.mouse_x =
        static_cast<int16_t>(frame[6] | static_cast<uint16_t>(frame[7]) << 8);
    out_state.mouse_y =
        static_cast<int16_t>(frame[8] | static_cast<uint16_t>(frame[9]) << 8);
    out_state.mouse_z =
        static_cast<int16_t>(frame[10] | static_cast<uint16_t>(frame[11]) << 8);
    out_state.mouse_left_pressed = frame[12] != 0;
    out_state.mouse_right_pressed = frame[13] != 0;
    out_state.keyboard_key_code =
        static_cast<uint16_t>(frame[14] | static_cast<uint16_t>(frame[15]) << 8);
    return true;
  }

  static RemoteSwitchPosition DecodeSwitch(uint8_t raw_value) {
    switch (raw_value) {
      case 1:
        return RemoteSwitchPosition::kUp;
      case 2:
        return RemoteSwitchPosition::kDown;
      case 3:
        return RemoteSwitchPosition::kMiddle;
      default:
        return RemoteSwitchPosition::kUnknown;
    }
  }

  static bool IsValidSwitch(RemoteSwitchPosition position) {
    return position == RemoteSwitchPosition::kUp ||
           position == RemoteSwitchPosition::kDown ||
           position == RemoteSwitchPosition::kMiddle;
  }

  static bool IsValidChannel(int16_t value) {
    return value >= -kChannelBound && value <= kChannelBound;
  }

  void PublishOnlineState(const DT7State& state) {
    last_frame_time_ms_ = LibXR::Thread::GetTime();
    published_state_ = state;
    state_topic_.Publish(published_state_);
  }

  void PublishOfflineState() {
    published_state_ = {};
    state_topic_.Publish(published_state_);
  }

  LibXR::UART* uart_ = nullptr;
  LibXR::Topic state_topic_;
  LibXR::Thread uart_thread_;

  LibXR::RawData rx_chunk_{new uint8_t[kRxChunkSize], kRxChunkSize};
  uint8_t rx_window_[kRxWindowSize]{};
  size_t rx_window_size_ = 0;

  DT7State published_state_{};
  uint32_t last_frame_time_ms_ = 0;
};
