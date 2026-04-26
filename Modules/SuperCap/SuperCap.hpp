#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: SuperCap 是一个基于 CAN 的超级电容最小通信模块，负责收发 donor super_cap 协议并发布模块私有状态 / SuperCap is a minimal CAN-based super capacitor communication module that exchanges donor super_cap protocol frames and publishes private state
constructor_args: []
template_args: []
required_hardware: can
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "app_framework.hpp"
#include "can.hpp"
#include "libxr_cb.hpp"
#include "message.hpp"
#include "thread.hpp"

/**
 * @brief 超级电容模块私有状态
 *
 * 当前阶段只承载 donor super_cap 协议反馈的最小可观测状态，
 * 不承担功率控制链路编排，也不进入 App/Topics 公共契约。
 */
struct SuperCapState {
  bool online = false;
  std::uint8_t error_code = 0;
  float chassis_power_w = 0.0f;
  std::uint16_t chassis_power_limit_w = 0;
  float cap_energy_percent = 0.0f;
  std::uint32_t last_rx_time_ms = 0;
  std::uint32_t rx_frame_count = 0;
};

namespace SuperCapProtocol {

static constexpr std::uint32_t kFeedbackFrameId = 0x051u;
static constexpr std::uint32_t kCommandFrameId = 0x061u;
static constexpr std::uint8_t kEnableDcdcFlag = 0x01u;
static constexpr std::uint8_t kSystemRestartFlag = 0x02u;
static constexpr std::uint8_t kOutputDisabledMask = 0x80u;

#pragma pack(push, 1)
struct FeedbackFrame {
  std::uint8_t error_code = 0;
  float chassis_power_w = 0.0f;
  std::uint16_t chassis_power_limit_w = 0;
  std::uint8_t cap_energy_raw = 0;
};

struct CommandFrame {
  std::uint8_t control_flags = 0;
  std::uint16_t referee_power_limit_w = 0;
  std::uint16_t referee_energy_buffer_j = 0;
  std::uint8_t reserved1[3] = {0, 0, 0};
};
#pragma pack(pop)

inline constexpr float ToEnergyPercent(std::uint8_t cap_energy_raw) {
  return static_cast<float>(cap_energy_raw) * 100.0f / 255.0f;
}

inline constexpr bool IsOutputDisabled(std::uint8_t error_code) {
  return (error_code & kOutputDisabledMask) != 0u;
}

inline constexpr CommandFrame BuildCommandFrame(bool enable_dcdc,
                                                bool system_restart,
                                                std::uint16_t power_limit_w,
                                                std::uint16_t energy_buffer_j) {
  CommandFrame frame{};
  if (enable_dcdc) {
    frame.control_flags |= kEnableDcdcFlag;
  }
  if (system_restart) {
    frame.control_flags |= kSystemRestartFlag;
  }
  frame.referee_power_limit_w = power_limit_w;
  frame.referee_energy_buffer_j = energy_buffer_j;
  return frame;
}

inline constexpr SuperCapState BuildStateView(std::uint8_t error_code,
                                              float chassis_power_w,
                                              std::uint16_t power_limit_w,
                                              std::uint8_t cap_energy_raw) {
  return SuperCapState{
      .online = true,
      .error_code = error_code,
      .chassis_power_w = chassis_power_w,
      .chassis_power_limit_w = power_limit_w,
      .cap_energy_percent = ToEnergyPercent(cap_energy_raw),
      .last_rx_time_ms = 0,
      .rx_frame_count = 0,
  };
}

inline SuperCapState DecodeFeedbackFrame(const FeedbackFrame& frame) {
  return BuildStateView(frame.error_code, frame.chassis_power_w,
                        frame.chassis_power_limit_w, frame.cap_energy_raw);
}

inline FeedbackFrame LoadFeedbackFrame(const std::uint8_t* data, std::size_t size) {
  FeedbackFrame frame{};
  if (data == nullptr || size < sizeof(FeedbackFrame)) {
    return frame;
  }
  std::memcpy(&frame, data, sizeof(FeedbackFrame));
  return frame;
}

}  // namespace SuperCapProtocol

class SuperCap : public LibXR::Application {
 public:
  static constexpr const char* kTopicName = "super_cap_state";
  static constexpr std::uint32_t kDefaultOfflineTimeoutMs = 100u;

  struct Config {
    const char* can_name = "can1";
    std::uint32_t offline_timeout_ms = kDefaultOfflineTimeoutMs;
    const char* state_topic_name = kTopicName;
  };

  explicit SuperCap(LibXR::HardwareContainer& hw,
                    LibXR::ApplicationManager& app)
      : SuperCap(hw, app, Config{}) {}

  SuperCap(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
           const Config& config)
      : can_(hw.template FindOrExit<LibXR::CAN>({config.can_name})),
        config_(config),
        state_topic_(LibXR::Topic::CreateTopic<SuperCapState>(
            config.state_topic_name, nullptr, false, true, true)) {
    can_rx_callback_ = LibXR::CAN::Callback::Create(OnCanRxStatic, this);
    can_->Register(can_rx_callback_, LibXR::CAN::Type::STANDARD,
                   LibXR::CAN::FilterMode::ID_RANGE,
                   SuperCapProtocol::kFeedbackFrameId,
                   SuperCapProtocol::kFeedbackFrameId);
    PublishState();
    app.Register(*this);
  }

  void OnMonitor() override {
    if (!state_.online) {
      return;
    }

    const std::uint32_t now_ms = LibXR::Thread::GetTime();
    if ((now_ms - state_.last_rx_time_ms) > config_.offline_timeout_ms) {
      state_.online = false;
      PublishState();
    }
  }

  bool SendCommand(bool enable_dcdc, std::uint16_t power_limit_w,
                   std::uint16_t energy_buffer_j,
                   bool system_restart = false) {
    const auto frame = SuperCapProtocol::BuildCommandFrame(
        enable_dcdc, system_restart, power_limit_w, energy_buffer_j);

    LibXR::CAN::ClassicPack pack{};
    pack.id = SuperCapProtocol::kCommandFrameId;
    pack.type = LibXR::CAN::Type::STANDARD;
    pack.dlc = sizeof(frame);
    std::memcpy(pack.data, &frame, sizeof(frame));
    return can_->AddMessage(pack) == LibXR::ErrorCode::OK;
  }

  LibXR::Topic& StateTopic() { return state_topic_; }

  const LibXR::Topic& StateTopic() const { return state_topic_; }

  const SuperCapState& GetState() const { return state_; }

 private:
  static void OnCanRxStatic(bool, SuperCap* self,
                            const LibXR::CAN::ClassicPack& pack) {
    if (pack.id != SuperCapProtocol::kFeedbackFrameId ||
        pack.dlc < sizeof(SuperCapProtocol::FeedbackFrame)) {
      return;
    }

    const auto frame =
        SuperCapProtocol::LoadFeedbackFrame(pack.data, pack.dlc);
    auto next_state = SuperCapProtocol::DecodeFeedbackFrame(frame);
    next_state.last_rx_time_ms = LibXR::Thread::GetTime();
    next_state.rx_frame_count = self->state_.rx_frame_count + 1u;
    self->state_ = next_state;
    self->PublishState();
  }

  void PublishState() { state_topic_.Publish(state_); }

  LibXR::CAN* can_ = nullptr;
  Config config_{};
  LibXR::Topic state_topic_;
  LibXR::CAN::Callback can_rx_callback_;
  SuperCapState state_{};
};
