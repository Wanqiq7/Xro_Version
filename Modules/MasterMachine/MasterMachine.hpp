#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: MasterMachine 是一个基于 UART 的上位机桥接最小骨架模块，负责发布强类型私有状态并提供超时安全回退 / MasterMachine is a minimal UART bridge skeleton that publishes a typed private state topic with timeout-safe fallback
constructor_args: []
template_args: []
required_hardware: uart
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstdint>
#include <cstring>

#include "../../App/Config/BridgeConfig.hpp"
#include "app_framework.hpp"
#include "libxr_rw.hpp"
#include "message.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "utils/crc.hpp"
#include "uart.hpp"

/**
 * @brief master_machine 私有状态
 *
 * 该状态只表达上位机桥当前批次需要输出给 App 的最小意图，
 * 不等同于真实串口协议帧，也不进入 App/Topics 公共契约。
 */
struct MasterMachineState {
  bool online = false;
  bool request_manual_override = false;
  bool request_fire_enable = false;
  float target_vx_mps = 0.0f;
  float target_vy_mps = 0.0f;
  float target_yaw_deg = 0.0f;
  float target_pitch_deg = 0.0f;
};

class MasterMachine : public LibXR::Application {
 public:
  static constexpr const char* kTopicName = "master_machine_state";
  static constexpr std::size_t kRxChunkSize = 36;
  static constexpr std::size_t kRxWindowSize = 96;

  explicit MasterMachine(
      LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
      App::Config::MasterMachineConfig config = App::Config::kMasterMachineConfig)
      : uart_(hw.template FindOrExit<LibXR::UART>({config.uart_name})),
        config_(config),
        state_topic_(LibXR::Topic::CreateTopic<MasterMachineState>(
            kTopicName, nullptr, false, true, true)) {
    uart_thread_.Create(this, UartThreadEntry, "MasterMachine::Rx",
                        config_.task_stack_depth,
                        LibXR::Thread::Priority::REALTIME);
    PublishSafeState();
    app.Register(*this);
  }

  void OnMonitor() override {
    if (!state_.online) {
      return;
    }

    if (!App::Config::BridgeDirectionAllowsRx(config_.direction)) {
      PublishSafeState();
      return;
    }

    const std::uint32_t now_ms = LibXR::Thread::GetTime();
    if ((now_ms - last_rx_time_ms_) > config_.offline_timeout_ms) {
      PublishSafeState();
    }
  }

  /**
   * @brief 供后续真实协议解析接入时更新桥接状态
   *
   * 当前 Phase 3 只冻结最小骨架，因此这里不做串口协议解析，
   * 而是保留一个显式注入入口，便于后续把解析逻辑补到模块内部线程或驱动回调中。
   */
  void UpdateRxState(const MasterMachineState& next_state) {
    if (!App::Config::BridgeDirectionAllowsRx(config_.direction)) {
      PublishSafeState();
      return;
    }

    state_ = next_state;
    state_.request_manual_override =
        App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kManualOverride) &&
        state_.request_manual_override;
    state_.request_fire_enable =
        App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kFireEnable) &&
        state_.request_fire_enable;
    if (!App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetVx)) {
      state_.target_vx_mps = 0.0f;
    }
    if (!App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetVy)) {
      state_.target_vy_mps = 0.0f;
    }
    if (!App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetYaw)) {
      state_.target_yaw_deg = 0.0f;
    }
    if (!App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetPitch)) {
      state_.target_pitch_deg = 0.0f;
    }
    // 收到合法帧即视为在线，零值目标同样是合法的“保持静止”输入。
    state_.online = true;
    last_rx_time_ms_ = LibXR::Thread::GetTime();
    state_topic_.Publish(state_);
  }

  LibXR::Topic& StateTopic() { return state_topic_; }

  const LibXR::Topic& StateTopic() const { return state_topic_; }

  const App::Config::MasterMachineConfig& Config() const { return config_; }

 private:
  static constexpr std::uint8_t kProtocolSof = 0xA5;
  static constexpr std::size_t kProtocolHeaderSize = 4;
  static constexpr std::size_t kProtocolOverheadSize = 8;

  enum class FlagBit : std::uint8_t {
    kManualOverride = 0,
    kFireEnable = 1,
  };

  static void UartThreadEntry(MasterMachine* self) {
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
    while (rx_window_size_ >= kProtocolOverheadSize) {
      MasterMachineState next_state{};
      std::size_t frame_size = 0;
      if (TryDecodeFrame(rx_window_, rx_window_size_, next_state, frame_size)) {
        UpdateRxState(next_state);
        ConsumeBytes(frame_size);
        continue;
      }
      ConsumeBytes(1);
    }
  }

  bool TryDecodeFrame(const std::uint8_t* frame, std::size_t size,
                      MasterMachineState& out_state,
                      std::size_t& frame_size) const {
    if (size < kProtocolOverheadSize || frame[0] != kProtocolSof) {
      return false;
    }

    if (LibXR::CRC8::Calculate(frame, 3) != frame[3]) {
      return false;
    }

    const std::uint16_t data_length =
        static_cast<std::uint16_t>(frame[1] | (frame[2] << 8));
    frame_size = data_length + kProtocolOverheadSize;
    if (data_length < 2 || frame_size > size) {
      return false;
    }

    const std::uint16_t expected_crc16 =
        LibXR::CRC16::Calculate(frame, frame_size - 2);
    const std::uint16_t received_crc16 = static_cast<std::uint16_t>(
        frame[frame_size - 2] | (frame[frame_size - 1] << 8));
    if (expected_crc16 != received_crc16) {
      return false;
    }

    const std::uint16_t flags_register =
        static_cast<std::uint16_t>(frame[6] | (frame[7] << 8));
    const std::size_t float_payload_size = data_length - 2;
    if ((float_payload_size % sizeof(float)) != 0) {
      return false;
    }

    float parsed_values[4]{0.0f, 0.0f, 0.0f, 0.0f};
    const std::size_t float_count =
        std::min<std::size_t>(float_payload_size / sizeof(float), 4);
    std::memcpy(parsed_values, frame + 8, float_count * sizeof(float));

    out_state.online = true;
    // 只保留 BridgeConfig 白名单允许进入 App 的稳定输入语义。
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kManualOverride)) {
      out_state.request_manual_override =
          (flags_register & (1u << static_cast<std::uint8_t>(
                                  FlagBit::kManualOverride))) != 0u;
    }
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kFireEnable)) {
      out_state.request_fire_enable =
          (flags_register &
           (1u << static_cast<std::uint8_t>(FlagBit::kFireEnable))) != 0u;
    }
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetPitch)) {
      out_state.target_pitch_deg = parsed_values[0];
    }
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetYaw)) {
      out_state.target_yaw_deg = parsed_values[1];
    }
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetVx)) {
      out_state.target_vx_mps = parsed_values[2];
    }
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetVy)) {
      out_state.target_vy_mps = parsed_values[3];
    }
    return true;
  }

  void PublishSafeState() {
    state_ = {};
    last_rx_time_ms_ = 0;
    state_topic_.Publish(state_);
  }

  LibXR::UART* uart_ = nullptr;
  App::Config::MasterMachineConfig config_{};
  LibXR::Topic state_topic_;
  LibXR::Thread uart_thread_;
  std::uint8_t rx_buffer_[kRxChunkSize]{};
  LibXR::RawData rx_chunk_{rx_buffer_, sizeof(rx_buffer_)};
  std::uint8_t rx_window_[kRxWindowSize]{};
  std::size_t rx_window_size_ = 0;
  MasterMachineState state_{};
  std::uint32_t last_rx_time_ms_ = 0;
};
