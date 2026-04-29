#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace App::Config {

inline constexpr std::size_t kMasterMachineMaxLinks = 2;

enum class BridgeDirection : std::uint8_t {
  kDisabled = 0,
  kRxOnly = 1,
  kTxOnly = 2,
  kBidirectional = 3,
};

enum class MasterMachineInputField : std::uint8_t {
  kManualOverride = 0,
  kFireEnable = 1,
  kTargetVx = 2,
  kTargetVy = 3,
  kTargetYaw = 4,
  kTargetPitch = 5,
  kVisionTarget = 6,
};

enum class MasterMachineIngressMode : std::uint8_t {
  kDisabled = 0,
  kManualOverrideOnly = 1,
};

struct InputWhitelistEntry {
  MasterMachineInputField field = MasterMachineInputField::kManualOverride;
  bool enabled = false;
};

struct MasterMachineLinkConfig {
  const char* device_name = nullptr;
  bool enable_rx = true;
  bool enable_tx = true;
};

struct MasterMachineConfig {
  std::uint32_t offline_timeout_ms = 100;
  std::size_t task_stack_depth = 2048;
  BridgeDirection direction = BridgeDirection::kBidirectional;
  MasterMachineIngressMode ingress_mode =
      MasterMachineIngressMode::kManualOverrideOnly;
  std::array<MasterMachineLinkConfig, kMasterMachineMaxLinks> links{{
      {"uart_master_machine", true, true},
      {"vcp_master_machine", true, true},
  }};
  std::array<InputWhitelistEntry, 7> inbound_whitelist{{
      {MasterMachineInputField::kManualOverride, true},
      {MasterMachineInputField::kFireEnable, true},
      {MasterMachineInputField::kTargetVx, true},
      {MasterMachineInputField::kTargetVy, true},
      {MasterMachineInputField::kTargetYaw, true},
      {MasterMachineInputField::kTargetPitch, true},
      {MasterMachineInputField::kVisionTarget, true},
  }};
};

struct SharedTopicClientConfig {
  const char* uart_name = "uart_master_machine";
  std::uint32_t task_stack_depth = 2048;
  std::uint32_t buffer_size = 512;
};

struct CANBridgePayloadWhitelistEntry {
  std::uint8_t channel = 0;
  std::uint8_t type = 0;
  bool enabled = false;
};

struct CANBridgeConfig {
  const char* can_name = "can_bridge";
  std::uint32_t offline_timeout_ms = 100;
  std::uint32_t tx_period_ms = 20;
  std::size_t task_stack_depth = 2048;
  BridgeDirection direction = BridgeDirection::kBidirectional;
  std::uint32_t tx_frame_id = 0x250;
  std::uint32_t rx_frame_id = 0x251;
  std::array<CANBridgePayloadWhitelistEntry, 4> payload_whitelist{{
      {0, 0, true},
      {0, 0, false},
      {0, 0, false},
      {0, 0, false},
  }};
};

inline constexpr bool BridgeDirectionAllowsRx(BridgeDirection direction) {
  return direction == BridgeDirection::kRxOnly ||
         direction == BridgeDirection::kBidirectional;
}

inline constexpr bool BridgeDirectionAllowsTx(BridgeDirection direction) {
  return direction == BridgeDirection::kTxOnly ||
         direction == BridgeDirection::kBidirectional;
}

inline constexpr bool MasterMachineFieldEnabled(
    const MasterMachineConfig& config, MasterMachineInputField field) {
  for (const auto& entry : config.inbound_whitelist) {
    if (entry.field == field) {
      return entry.enabled;
    }
  }
  return false;
}

inline constexpr bool CANBridgePayloadAllowed(const CANBridgeConfig& config,
                                              std::uint8_t channel,
                                              std::uint8_t type) {
  for (const auto& entry : config.payload_whitelist) {
    if (entry.enabled && entry.channel == channel && entry.type == type) {
      return true;
    }
  }
  return false;
}

inline constexpr MasterMachineConfig kMasterMachineConfig{};
inline constexpr CANBridgeConfig kCANBridgeConfig{};
inline constexpr SharedTopicClientConfig kSharedTopicClientConfig{};

}  // namespace App::Config
