#pragma once

#include <cstdint>

namespace App {

// 系统健康等级，用于表达整车当前可运行状态。
enum class HealthLevel : std::uint8_t {
  kOk = 0,
  kWarning = 1,
  kFault = 2,
  kEmergencyStop = 3,
};

// 安全动作建议，由健康汇总逻辑给上层控制器消费。
enum class SafeAction : std::uint8_t {
  kNone = 0,
  kLimitOutput = 1,
  kDisableShoot = 2,
  kStopMotion = 3,
  kEmergencyStop = 4,
};

// 系统故障位。故障通常需要触发降级、停机或人工介入。
enum class SystemHealthFault : std::uint32_t {
  kInputLost = 1u << 0,
  kInsLost = 1u << 1,
  kChassisFault = 1u << 2,
  kGimbalFault = 1u << 3,
  kShootFault = 1u << 4,
  kTopicStale = 1u << 5,
};

// 系统告警位。告警表示能力缺失或数据不可用，但未必要求立即停机。
enum class SystemHealthWarning : std::uint32_t {
  kRefereeLost = 1u << 0,
  kSuperCapLost = 1u << 1,
  kCanBridgeLost = 1u << 2,
};

// SystemHealth Topic：汇总输入、姿态、执行器和外部链路的健康状态。
struct SystemHealth {
  bool online = false;
  bool emergency_stop_requested = false;
  bool buzzer_alarm_requested = false;

  HealthLevel level = HealthLevel::kOk;
  SafeAction safe_action = SafeAction::kNone;
  std::uint32_t fault_flags = 0;
  std::uint32_t warning_flags = 0;
  std::uint32_t last_update_ms = 0;

  bool input_online = false;
  bool ins_online = false;
  bool chassis_ready = false;
  bool gimbal_ready = false;
  bool shoot_ready = false;
  bool referee_online = false;
  bool super_cap_online = false;
  bool can_bridge_online = false;
};

constexpr bool HasSystemHealthFault(std::uint32_t flags,
                                    SystemHealthFault fault) {
  return (flags & static_cast<std::uint32_t>(fault)) != 0u;
}

constexpr bool HasSystemHealthWarning(std::uint32_t flags,
                                      SystemHealthWarning warning) {
  return (flags & static_cast<std::uint32_t>(warning)) != 0u;
}

}  // namespace App
