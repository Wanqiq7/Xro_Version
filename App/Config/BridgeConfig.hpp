#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace App::Config {

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
};

enum class MasterMachineIngressMode : std::uint8_t {
  kDisabled = 0,
  kManualOverrideOnly = 1,
};

struct InputWhitelistEntry {
  MasterMachineInputField field = MasterMachineInputField::kManualOverride;
  bool enabled = false;
};

struct MasterMachineConfig {
  const char* uart_name = "uart_master_machine";
  std::uint32_t offline_timeout_ms = 100;
  std::size_t task_stack_depth = 2048;
  BridgeDirection direction = BridgeDirection::kRxOnly;
  MasterMachineIngressMode ingress_mode =
      MasterMachineIngressMode::kManualOverrideOnly;
  std::array<InputWhitelistEntry, 6> inbound_whitelist{{
      {MasterMachineInputField::kManualOverride, true},
      {MasterMachineInputField::kFireEnable, true},
      {MasterMachineInputField::kTargetVx, true},
      {MasterMachineInputField::kTargetVy, true},
      {MasterMachineInputField::kTargetYaw, true},
      {MasterMachineInputField::kTargetPitch, true},
  }};
};

struct SharedTopicClientConfig {
  const char* uart_name = "uart_master_machine";
  std::uint32_t task_stack_depth = 2048;
  std::uint32_t buffer_size = 512;
};

struct CANBridgeConfig {
  const char* can_name = "can_bridge";
  std::uint32_t offline_timeout_ms = 100;
  std::uint32_t tx_period_ms = 20;
  std::size_t task_stack_depth = 2048;
  BridgeDirection direction = BridgeDirection::kBidirectional;
  std::uint32_t tx_frame_id = 0x250;
  std::uint32_t rx_frame_id = 0x251;
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

inline constexpr MasterMachineConfig kMasterMachineConfig{};
inline constexpr CANBridgeConfig kCANBridgeConfig{};
inline constexpr SharedTopicClientConfig kSharedTopicClientConfig{};

}  // namespace App::Config
