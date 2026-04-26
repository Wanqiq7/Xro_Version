#pragma once

#include <cstddef>
#include <cstdint>

namespace App::Config {

/**
 * @brief referee 模块第一批冻结配置
 *
 * 当前只定义 bring-up 与 fan-in 骨架所需的最小配置，
 * 不引入协议细节，也不绑定上层公共 Topic。
 */
struct RefereeConfig {
  const char* uart_name = "uart_referee";
  std::size_t task_stack_depth = 2048;
  std::uint32_t offline_timeout_ms = 100;
  bool enabled = false;

  // 当前未完成真实协议解析前，统一使用保守默认值：
  // 不允许发射、比赛未开始、热量剩余为 0、功率预算为 0。
  float default_chassis_power_limit_w = 0.0f;
  std::uint16_t default_shooter_heat_limit = 0;
  std::uint16_t default_shooter_cooling_value = 0;
  std::uint16_t default_shooter_heat = 0;
  std::uint16_t default_remaining_heat = 0;
  std::uint16_t default_buffer_energy = 0;
  std::uint8_t default_remaining_energy_bits = 0;

  // 预留第一批基础上限，便于后续解析值进入模块时做最小裁剪。
  float max_chassis_power_limit_w = 200.0f;
  std::uint16_t max_shooter_heat_limit = 1024;
  std::uint16_t max_shooter_cooling_value = 1024;
  std::uint16_t max_buffer_energy = 1024;
};

inline constexpr RefereeConfig kRefereeConfig{};

}  // namespace App::Config
