#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: CANBridge 是一个基于 CAN 的最小真实协议桥接模块，负责发送/接收 heartbeat 遥测帧、执行白名单校验并发布桥接链路状态 / CANBridge is a minimal real-protocol CAN bridge module that sends/receives heartbeat telemetry frames, enforces whitelist checks, and publishes link state
constructor_args: []
template_args: []
required_hardware: can
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstddef>
#include <cstdint>
#include <array>

#include "../../App/Config/BridgeConfig.hpp"
#include "app_framework.hpp"
#include "can.hpp"
#include "libxr_cb.hpp"
#include "message.hpp"
#include "thread.hpp"

/**
 * @brief can bridge 私有状态
 *
 * 当前只描述最小 bridge 协议闭环所需的链路状态、收发统计与最近活动信息。
 * 该状态只服务于链路观测，不承载任何业务命令旁路。
 */
struct CANBridgeState {
  bool online = false;
  bool tx_enabled = false;
  bool rx_enabled = false;
  std::uint32_t last_tx_time_ms = 0;
  std::uint32_t last_rx_time_ms = 0;
  std::uint32_t last_activity_time_ms = 0;
  std::uint32_t tx_frame_count = 0;
  std::uint32_t rx_frame_count = 0;
  std::uint32_t crc_error_count = 0;
  std::uint32_t payload_rx_count = 0;
  std::uint32_t payload_tx_count = 0;
  std::uint32_t payload_crc_error_count = 0;
  std::uint8_t last_tx_seq = 0;
  std::uint8_t last_rx_seq = 0;
  std::uint8_t last_payload_tx_seq = 0;
  std::uint8_t last_payload_rx_seq = 0;
  std::uint8_t last_payload_channel = 0;
  std::uint8_t last_payload_type = 0;
  std::uint8_t last_payload_len = 0;
  std::uint8_t last_payload_data0 = 0;
  std::uint8_t last_payload_data1 = 0;
};

struct CANBridgePayload {
  static constexpr std::uint8_t kMaxDataLength = 60u;

  std::uint8_t channel = 0;
  std::uint8_t type = 0;
  std::uint8_t seq = 0;
  std::uint8_t len = 0;
  std::uint8_t data[kMaxDataLength] = {};
};

namespace CANBridgeProtocol {

static constexpr std::uint8_t kHeartbeatMagic = 0xA5u;
static constexpr std::uint8_t kPayloadMagic = 0x5Au;
static constexpr std::uint8_t kProtocolVersion = 1u;
static constexpr std::uint8_t kPayloadProtocolVersion = 2u;
static constexpr std::uint8_t kMaxPayloadBytes = 60u;
static constexpr std::uint8_t kPayloadHeaderSize = 7u;
static constexpr std::uint8_t kMaxPayloadRawBytes =
    static_cast<std::uint8_t>(kPayloadHeaderSize + kMaxPayloadBytes);
static constexpr std::uint8_t kFlagOnline = 1u << 0;
static constexpr std::uint8_t kFlagTxEnabled = 1u << 1;
static constexpr std::uint8_t kFlagRxEnabled = 1u << 2;
static constexpr std::uint8_t kCrc8Init = 0xFFu;
static constexpr std::uint8_t kCrc8Polynomial = 0x8Cu;

#pragma pack(push, 1)
struct HeartbeatFrame {
  std::uint8_t magic = kHeartbeatMagic;
  std::uint8_t version = kProtocolVersion;
  std::uint8_t seq = 0;
  std::uint8_t flags = 0;
  std::uint8_t rx_count = 0;
  std::uint8_t tx_count = 0;
  std::uint8_t peer_seq = 0;
  std::uint8_t checksum = 0;
};

struct PayloadFrame {
  std::uint8_t magic = kPayloadMagic;
  std::uint8_t version = kPayloadProtocolVersion;
  std::uint8_t seq = 0;
  std::uint8_t channel = 0;
  std::uint8_t type = 0;
  std::uint8_t payload_len = 0;
  std::uint8_t checksum = 0;
};
#pragma pack(pop)

inline constexpr std::uint8_t NarrowCounter(std::uint32_t counter) {
  return counter > 0xFFu ? 0xFFu : static_cast<std::uint8_t>(counter);
}

inline constexpr std::uint8_t EncodeFlags(const CANBridgeState& state) {
  std::uint8_t flags = 0;
  if (state.online) {
    flags |= kFlagOnline;
  }
  if (state.tx_enabled) {
    flags |= kFlagTxEnabled;
  }
  if (state.rx_enabled) {
    flags |= kFlagRxEnabled;
  }
  return flags;
}

inline constexpr std::uint8_t UpdateCrc8(std::uint8_t crc, std::uint8_t value) {
  crc ^= value;
  for (std::uint8_t bit = 0; bit < 8u; ++bit) {
    if ((crc & 0x01u) != 0u) {
      crc = static_cast<std::uint8_t>((crc >> 1u) ^ kCrc8Polynomial);
    } else {
      crc = static_cast<std::uint8_t>(crc >> 1u);
    }
  }
  return crc;
}

inline constexpr std::uint8_t ComputeChecksum(const HeartbeatFrame& frame) {
  std::uint8_t crc = kCrc8Init;
  crc = UpdateCrc8(crc, frame.magic);
  crc = UpdateCrc8(crc, frame.version);
  crc = UpdateCrc8(crc, frame.seq);
  crc = UpdateCrc8(crc, frame.flags);
  crc = UpdateCrc8(crc, frame.rx_count);
  crc = UpdateCrc8(crc, frame.tx_count);
  crc = UpdateCrc8(crc, frame.peer_seq);
  return crc;
}

inline constexpr std::uint8_t ComputeHeaderChecksum(const PayloadFrame& frame) {
  std::uint8_t crc = kCrc8Init;
  crc = UpdateCrc8(crc, frame.magic);
  crc = UpdateCrc8(crc, frame.version);
  crc = UpdateCrc8(crc, frame.seq);
  crc = UpdateCrc8(crc, frame.channel);
  crc = UpdateCrc8(crc, frame.type);
  crc = UpdateCrc8(crc, frame.payload_len);
  return crc;
}

inline std::uint8_t ComputePayloadChecksum(const PayloadFrame& frame,
                                           const std::uint8_t* data) {
  std::uint8_t crc = ComputeHeaderChecksum(frame);
  for (std::uint8_t i = 0; i < frame.payload_len; ++i) {
    crc = UpdateCrc8(crc, data[i]);
  }
  return crc;
}

inline constexpr HeartbeatFrame BuildHeartbeatFrame(
    const CANBridgeState& state) {
  HeartbeatFrame frame{};
  frame.seq = state.last_tx_seq;
  frame.flags = EncodeFlags(state);
  frame.rx_count = NarrowCounter(state.rx_frame_count);
  frame.tx_count = NarrowCounter(state.tx_frame_count);
  frame.peer_seq = state.last_rx_seq;
  frame.checksum = ComputeChecksum(frame);
  return frame;
}

inline constexpr bool IsValidHeartbeatFrame(const HeartbeatFrame& frame) {
  return frame.magic == kHeartbeatMagic &&
         frame.version == kProtocolVersion &&
         frame.checksum == ComputeChecksum(frame);
}

inline PayloadFrame BuildPayloadHeader(
    std::uint8_t seq, std::uint8_t channel, std::uint8_t type,
    const std::uint8_t* data, std::uint8_t len) {
  PayloadFrame frame{};
  frame.seq = seq;
  frame.channel = channel;
  frame.type = type;
  frame.payload_len = len;
  frame.checksum = ComputePayloadChecksum(frame, data);
  return frame;
}

inline bool IsValidPayloadFrame(const PayloadFrame& frame,
                                const std::uint8_t* data) {
  return frame.magic == kPayloadMagic &&
         frame.version == kPayloadProtocolVersion &&
         frame.payload_len <= kMaxPayloadBytes &&
         frame.checksum == ComputePayloadChecksum(frame, data);
}

}  // namespace CANBridgeProtocol

class CANBridge : public LibXR::Application {
 public:
  static constexpr const char* kTopicName = "can_bridge_state";
  static constexpr const char* kPayloadTopicName = "can_bridge_payload";

  explicit CANBridge(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
                     App::Config::CANBridgeConfig config =
                         App::Config::kCANBridgeConfig)
      : can_(hw.template FindOrExit<LibXR::CAN>({config.can_name})),
        config_(config),
        state_topic_(LibXR::Topic::CreateTopic<CANBridgeState>(
            kTopicName, nullptr, false, true, true)),
        payload_topic_(LibXR::Topic::CreateTopic<CANBridgePayload>(
            kPayloadTopicName, nullptr, false, true, true)) {
    rx_callback_ = LibXR::CAN::Callback::Create(OnCanRxStatic, this);
    can_->Register(rx_callback_, LibXR::CAN::Type::STANDARD);
    can_->Register(rx_callback_, LibXR::CAN::Type::EXTENDED);

    state_.tx_enabled = App::Config::BridgeDirectionAllowsTx(config_.direction);
    state_.rx_enabled = App::Config::BridgeDirectionAllowsRx(config_.direction);
    PublishState();
    app.Register(*this);
  }

  void OnMonitor() override {
    const std::uint32_t now_ms = LibXR::Thread::GetTime();
    bool state_changed = false;

    if (ShouldSendHeartbeat(now_ms)) {
      state_changed |= SendHeartbeat(now_ms);
    }

    const bool online_now = EvaluateOnline(now_ms);
    if (state_.online != online_now) {
      state_.online = online_now;
      state_changed = true;
    }

    if (state_changed) {
      PublishState();
    }
  }

  /**
   * @brief 记录一次接收活动
   *
   * 当前仅接受最小 heartbeat/telemetry 协议帧，命中白名单后只更新 bridge
   * 自身观测状态，不向业务 Topic 写入任何命令。
   */
  void NotifyRxActivity(const CANBridgeProtocol::HeartbeatFrame& frame,
                        std::uint32_t now_ms) {
    if (!App::Config::BridgeDirectionAllowsRx(config_.direction)) {
      return;
    }

    state_.rx_enabled = true;
    state_.online = true;
    state_.last_rx_time_ms = now_ms;
    state_.last_activity_time_ms = now_ms;
    state_.last_rx_seq = frame.seq;
    state_.rx_frame_count += 1u;
    PublishState();
  }

  /**
   * @brief 记录一次发送活动
   *
   * 当前只发送 bridge 自己的最小 heartbeat/telemetry 帧，不承担业务命令转发。
   */
  void NotifyTxActivity(std::uint32_t now_ms) {
    if (!App::Config::BridgeDirectionAllowsTx(config_.direction)) {
      return;
    }

    state_.tx_enabled = true;
    state_.last_tx_time_ms = now_ms;
    state_.last_activity_time_ms = now_ms;
    state_.tx_frame_count += 1u;
    state_.last_tx_seq = static_cast<std::uint8_t>(state_.last_tx_seq + 1u);
  }

  /**
   * @brief 发送带 CRC 的 typed payload 单帧
   *
   * 该接口只提供 CAN bridge 的通用 payload 传输能力。payload 仍需命中
   * channel/type 白名单，模块只负责分包、CRC 与重组，不解释业务含义。
   */
  bool SendPayload(std::uint8_t channel, std::uint8_t type,
                   const std::uint8_t* data, std::size_t len) {
    if (!App::Config::BridgeDirectionAllowsTx(config_.direction)) {
      return false;
    }
    if (!IsTxFrameIdWhitelisted(config_.tx_frame_id)) {
      return false;
    }
    if (!App::Config::CANBridgePayloadAllowed(config_, channel, type)) {
      return false;
    }
    if (len > CANBridgeProtocol::kMaxPayloadBytes) {
      return false;
    }
    if (len > 0u && data == nullptr) {
      return false;
    }

    CANBridgeState next_state = state_;
    next_state.tx_enabled = true;
    next_state.last_payload_tx_seq =
        static_cast<std::uint8_t>(next_state.last_payload_tx_seq + 1u);

    const auto frame = CANBridgeProtocol::BuildPayloadHeader(
        next_state.last_payload_tx_seq, channel, type, data,
        static_cast<std::uint8_t>(len));
    std::array<std::uint8_t, CANBridgeProtocol::kMaxPayloadRawBytes> raw{};
    for (std::size_t i = 0; i < sizeof(frame); ++i) {
      raw[i] = reinterpret_cast<const std::uint8_t*>(&frame)[i];
    }
    for (std::size_t i = 0; i < len; ++i) {
      raw[CANBridgeProtocol::kPayloadHeaderSize + i] = data[i];
    }

    const std::size_t raw_len = CANBridgeProtocol::kPayloadHeaderSize + len;
    for (std::size_t offset = 0; offset < raw_len; offset += 8u) {
      LibXR::CAN::ClassicPack pack{};
      pack.id = config_.tx_frame_id;
      pack.type = LibXR::CAN::Type::STANDARD;
      const std::size_t remain = raw_len - offset;
      pack.dlc = remain >= 8u ? 8u : static_cast<std::uint8_t>(remain);
      for (std::size_t i = 0; i < pack.dlc; ++i) {
        pack.data[i] = raw[offset + i];
      }
      if (can_->AddMessage(pack) != LibXR::ErrorCode::OK) {
        return false;
      }
    }

    const std::uint32_t now_ms = LibXR::Thread::GetTime();
    next_state.last_tx_time_ms = now_ms;
    next_state.last_activity_time_ms = now_ms;
    next_state.tx_frame_count += 1u;
    next_state.payload_tx_count += 1u;
    next_state.last_payload_channel = channel;
    next_state.last_payload_type = type;
    next_state.last_payload_len = static_cast<std::uint8_t>(len);
    next_state.last_payload_data0 = len > 0u ? data[0] : 0u;
    next_state.last_payload_data1 = len > 1u ? data[1] : 0u;
    state_ = next_state;
    PublishState();
    return true;
  }

  bool IsRxFrameIdWhitelisted(std::uint32_t frame_id) const {
    return frame_id == config_.rx_frame_id;
  }

  bool IsTxFrameIdWhitelisted(std::uint32_t frame_id) const {
    return frame_id == config_.tx_frame_id;
  }

  LibXR::Topic& StateTopic() { return state_topic_; }

  const LibXR::Topic& StateTopic() const { return state_topic_; }

  LibXR::Topic& PayloadTopic() { return payload_topic_; }

  const LibXR::Topic& PayloadTopic() const { return payload_topic_; }

  const CANBridgeState& State() const { return state_; }

 private:
  struct PayloadTransferState {
    bool active = false;
    std::uint8_t raw_len = 0;
    std::uint8_t expected_raw_len = 0;
    std::array<std::uint8_t, CANBridgeProtocol::kMaxPayloadRawBytes> raw{};
  };

  static void OnCanRxStatic(bool, CANBridge* self,
                            const LibXR::CAN::ClassicPack& pack) {
    if (!self->IsRxFrameIdWhitelisted(pack.id) || pack.dlc == 0u ||
        pack.dlc > 8u) {
      return;
    }

    if (self->payload_rx_.active ||
        pack.data[0] == CANBridgeProtocol::kPayloadMagic) {
      self->HandlePayloadRx(pack, LibXR::Thread::GetTime());
      return;
    }

    if (pack.dlc != 8u) {
      return;
    }

    CANBridgeProtocol::HeartbeatFrame frame{};
    for (std::size_t i = 0; i < pack.dlc; ++i) {
      reinterpret_cast<std::uint8_t*>(&frame)[i] = pack.data[i];
    }

    if (!CANBridgeProtocol::IsValidHeartbeatFrame(frame)) {
      self->state_.crc_error_count += 1u;
      self->PublishState();
      return;
    }

    self->NotifyRxActivity(frame, LibXR::Thread::GetTime());
  }

  void HandlePayloadRx(const LibXR::CAN::ClassicPack& pack,
                       std::uint32_t now_ms) {
    if (!App::Config::BridgeDirectionAllowsRx(config_.direction)) {
      return;
    }

    if (!AppendPayloadRxFrame(pack)) {
      state_.payload_crc_error_count += 1u;
      state_.crc_error_count += 1u;
      PublishState();
      return;
    }

    if (!payload_rx_.active) {
      return;
    }

    if (payload_rx_.raw_len < payload_rx_.expected_raw_len) {
      return;
    }

    CANBridgeProtocol::PayloadFrame frame{};
    for (std::size_t i = 0; i < sizeof(frame); ++i) {
      reinterpret_cast<std::uint8_t*>(&frame)[i] = payload_rx_.raw[i];
    }
    const std::uint8_t* payload_data =
        payload_rx_.raw.data() + CANBridgeProtocol::kPayloadHeaderSize;

    if (!CANBridgeProtocol::IsValidPayloadFrame(frame, payload_data)) {
      ResetPayloadRx();
      state_.payload_crc_error_count += 1u;
      state_.crc_error_count += 1u;
      PublishState();
      return;
    }

    if (!App::Config::CANBridgePayloadAllowed(config_, frame.channel,
                                             frame.type)) {
      ResetPayloadRx();
      return;
    }

    const std::uint8_t len = frame.payload_len;
    CANBridgePayload payload{};
    payload.channel = frame.channel;
    payload.type = frame.type;
    payload.seq = frame.seq;
    payload.len = len;
    for (std::uint8_t i = 0; i < len; ++i) {
      payload.data[i] = payload_data[i];
    }

    state_.rx_enabled = true;
    state_.online = true;
    state_.last_rx_time_ms = now_ms;
    state_.last_activity_time_ms = now_ms;
    state_.last_payload_rx_seq = frame.seq;
    state_.rx_frame_count += 1u;
    state_.payload_rx_count += 1u;
    state_.last_payload_channel = frame.channel;
    state_.last_payload_type = frame.type;
    state_.last_payload_len = len;
    state_.last_payload_data0 = len > 0u ? payload.data[0] : 0u;
    state_.last_payload_data1 = len > 1u ? payload.data[1] : 0u;
    PublishPayload(payload);
    ResetPayloadRx();
    PublishState();
  }

  bool AppendPayloadRxFrame(const LibXR::CAN::ClassicPack& pack) {
    if (pack.data[0] == CANBridgeProtocol::kPayloadMagic) {
      ResetPayloadRx();
      if (pack.dlc < CANBridgeProtocol::kPayloadHeaderSize) {
        return false;
      }

      CANBridgeProtocol::PayloadFrame frame{};
      for (std::size_t i = 0; i < sizeof(frame); ++i) {
        reinterpret_cast<std::uint8_t*>(&frame)[i] = pack.data[i];
      }
      if (frame.version != CANBridgeProtocol::kPayloadProtocolVersion ||
          frame.payload_len > CANBridgeProtocol::kMaxPayloadBytes ||
          !App::Config::CANBridgePayloadAllowed(config_, frame.channel,
                                                frame.type)) {
        return false;
      }

      payload_rx_.active = true;
      payload_rx_.expected_raw_len = static_cast<std::uint8_t>(
          CANBridgeProtocol::kPayloadHeaderSize + frame.payload_len);
    } else if (!payload_rx_.active) {
      return false;
    }

    if (payload_rx_.raw_len + pack.dlc > payload_rx_.expected_raw_len ||
        payload_rx_.raw_len + pack.dlc >
            CANBridgeProtocol::kMaxPayloadRawBytes) {
      ResetPayloadRx();
      return false;
    }

    for (std::size_t i = 0; i < pack.dlc; ++i) {
      payload_rx_.raw[payload_rx_.raw_len + i] = pack.data[i];
    }
    payload_rx_.raw_len =
        static_cast<std::uint8_t>(payload_rx_.raw_len + pack.dlc);
    return true;
  }

  void ResetPayloadRx() { payload_rx_ = PayloadTransferState{}; }

  void PublishState() { state_topic_.Publish(state_); }

  void PublishPayload(const CANBridgePayload& payload) {
    CANBridgePayload payload_copy = payload;
    payload_topic_.Publish(payload_copy);
  }

  bool ShouldSendHeartbeat(std::uint32_t now_ms) const {
    if (!App::Config::BridgeDirectionAllowsTx(config_.direction)) {
      return false;
    }
    if (!IsTxFrameIdWhitelisted(config_.tx_frame_id)) {
      return false;
    }
    if (state_.last_tx_time_ms == 0u) {
      return true;
    }
    return (now_ms - state_.last_tx_time_ms) >= config_.tx_period_ms;
  }

  bool EvaluateOnline(std::uint32_t now_ms) const {
    if (App::Config::BridgeDirectionAllowsRx(config_.direction)) {
      return state_.last_rx_time_ms != 0u &&
             (now_ms - state_.last_rx_time_ms) <= config_.offline_timeout_ms;
    }
    if (App::Config::BridgeDirectionAllowsTx(config_.direction)) {
      return state_.last_tx_time_ms != 0u &&
             (now_ms - state_.last_tx_time_ms) <= config_.offline_timeout_ms;
    }
    return false;
  }

  bool SendHeartbeat(std::uint32_t now_ms) {
    CANBridgeState next_state = state_;
    next_state.tx_enabled = true;
    next_state.last_tx_time_ms = now_ms;
    next_state.last_activity_time_ms = now_ms;
    next_state.tx_frame_count += 1u;
    next_state.last_tx_seq =
        static_cast<std::uint8_t>(next_state.last_tx_seq + 1u);

    const auto frame = CANBridgeProtocol::BuildHeartbeatFrame(next_state);
    LibXR::CAN::ClassicPack pack{};
    pack.id = config_.tx_frame_id;
    pack.type = LibXR::CAN::Type::STANDARD;
    pack.dlc = sizeof(frame);
    for (std::size_t i = 0; i < sizeof(frame); ++i) {
      pack.data[i] = reinterpret_cast<const std::uint8_t*>(&frame)[i];
    }

    if (can_->AddMessage(pack) != LibXR::ErrorCode::OK) {
      return false;
    }

    state_ = next_state;
    return true;
  }

  LibXR::CAN* can_ = nullptr;
  App::Config::CANBridgeConfig config_{};
  LibXR::Topic state_topic_;
  LibXR::Topic payload_topic_;
  LibXR::CAN::Callback rx_callback_;
  PayloadTransferState payload_rx_{};
  CANBridgeState state_{};
};
