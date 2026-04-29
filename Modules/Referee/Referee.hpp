#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: Referee 是一个基于 UART 接入的裁判系统最小 fan-in 骨架模块，负责发布私有状态并在离线超时时回落到保守默认值 / Referee is a UART-based minimal referee fan-in skeleton that publishes private state and falls back to conservative defaults on timeout
constructor_args:
  - config: "App::Config::kRefereeConfig"
template_args: []
required_hardware: config.uart_name
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../App/Config/RefereeConfig.hpp"
#include "app_framework.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "message.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "utils/crc.hpp"
#include "uart.hpp"

/**
 * @brief referee 模块私有状态
 *
 * 该结构仅作为模块私有 fan-in 输出，不直接进入 App/Topics 公共契约。
 * 在真实协议解析未接入完成前，字段由模块内部维持保守默认值。
 */
struct RefereeState {
  bool online = false;
  bool match_started = false;
  bool fire_allowed = false;
  float chassis_power_limit_w = 0.0f;
  float chassis_power_w = 0.0f;
  float shooter_speed_mps = 0.0f;
  float position_x_m = 0.0f;
  float position_y_m = 0.0f;
  float position_z_m = 0.0f;
  float position_yaw_deg = 0.0f;
  std::uint16_t shooter_heat_limit = 0;
  std::uint16_t cooling_rate = 0;
  std::uint16_t shooter_cooling_value = 0;
  std::uint16_t shooter_heat = 0;
  std::uint16_t remaining_heat = 0;
  std::uint16_t robot_hp = 0;
  std::uint16_t robot_max_hp = 0;
  std::uint16_t buffer_energy = 0;
  std::uint16_t chassis_voltage_mv = 0;
  std::uint16_t chassis_current_ma = 0;
  std::uint16_t stage_remain_time_s = 0;
  std::uint32_t event_flags = 0;
  std::uint8_t hurt_armor_id = 0;
  std::uint8_t hurt_type = 0;
  std::uint8_t game_type = 0;
  std::uint8_t game_progress = 0;
  std::uint8_t game_result = 0;
  std::uint8_t robot_id = 0;
  std::uint8_t robot_level = 0;
  std::uint8_t aerial_attack_time_s = 0;
  std::uint8_t remaining_energy_bits = 0;
};

class Referee : public LibXR::Application {
 public:
  struct RawStateCache {
    bool has_game_status = false;
    bool has_game_result = false;
    bool has_event_data = false;
    bool has_robot_status = false;
    bool has_robot_hp = false;
    bool has_power_heat = false;
    bool has_robot_position = false;
    bool has_aerial_energy = false;
    bool has_buff = false;
    bool has_hurt = false;
    bool has_shoot_data = false;
    bool match_started = false;
    bool chassis_output_enabled = false;
    bool shooter_output_enabled = false;
    float chassis_power_limit_w = 0.0f;
    float chassis_power_w = 0.0f;
    float shooter_speed_mps = 0.0f;
    float position_x_m = 0.0f;
    float position_y_m = 0.0f;
    float position_z_m = 0.0f;
    float position_yaw_deg = 0.0f;
    std::uint16_t shooter_heat_limit = 0;
    std::uint16_t cooling_rate = 0;
    std::uint16_t shooter_cooling_value = 0;
    std::uint16_t shooter_heat = 0;
    std::uint16_t robot_hp = 0;
    std::uint16_t robot_max_hp = 0;
    std::uint16_t buffer_energy = 0;
    std::uint16_t chassis_voltage_mv = 0;
    std::uint16_t chassis_current_ma = 0;
    std::uint16_t stage_remain_time_s = 0;
    std::uint16_t cooling_buff = 0;
    std::uint16_t robot_hp_slots[16]{};
    std::uint32_t event_flags = 0;
    std::uint8_t game_type = 0;
    std::uint8_t game_progress = 0;
    std::uint8_t game_result = 0;
    std::uint8_t hurt_armor_id = 0;
    std::uint8_t hurt_type = 0;
    std::uint8_t robot_id = 0;
    std::uint8_t robot_level = 0;
    std::uint8_t aerial_attack_time_s = 0;
    std::uint8_t remaining_energy_bits = 0;
  };

  static constexpr const char* kStateTopicName = "referee_state";
  static constexpr std::size_t kRxChunkSize = 32;
  static constexpr std::size_t kRxWindowSize = 256;

  Referee(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
          const App::Config::RefereeConfig& config = App::Config::kRefereeConfig)
      : uart_(config.enabled ? hw.template Find<LibXR::UART>(config.uart_name)
                             : nullptr),
        state_topic_(LibXR::Topic::CreateTopic<RefereeState>(
            kStateTopicName, nullptr, false, true, true)),
        config_(config),
        rx_chunk_(rx_buffer_, sizeof(rx_buffer_)) {
    if (config_.enabled) {
      ASSERT(uart_ != nullptr);
      uart_thread_.Create(this, UartThreadEntry, "Referee::Rx",
                          config_.task_stack_depth,
                          LibXR::Thread::Priority::REALTIME);
    }

    PublishState(BuildConservativeState(false));
    app.Register(*this);
  }

  void OnMonitor() override {
    if (!config_.enabled) {
      if (published_state_.online) {
        ResetRawStateCache();
        PublishState(BuildConservativeState(false));
      }
      return;
    }

    const std::uint32_t now_ms = LibXR::Thread::GetTime();
    if (!published_state_.online) {
      return;
    }

    if ((now_ms - last_rx_time_ms_) > config_.offline_timeout_ms) {
      ResetRawStateCache();
      PublishState(BuildConservativeState(false));
    }
  }

  /**
   * @brief 未来协议解析接入后的最小写入口
   *
   * 当前 bring-up 骨架允许上层或后续解析逻辑把“已确认有效”的裁判状态
   * 写回模块内部，再由模块统一做裁剪、发布时间戳更新与私有 Topic 发布。
   */
  void ApplyProtocolState(bool match_started, bool fire_allowed,
                          float chassis_power_limit_w,
                          std::uint16_t shooter_heat_limit,
                          std::uint16_t shooter_cooling_value,
                          std::uint16_t shooter_heat,
                          std::uint16_t remaining_heat,
                          std::uint16_t buffer_energy,
                          std::uint8_t remaining_energy_bits) {
    (void)fire_allowed;
    (void)remaining_heat;
    RefereeState next_state = BuildConservativeState(true);
    next_state.match_started = match_started;
    next_state.chassis_power_limit_w =
        ClampPowerLimit(config_, chassis_power_limit_w);
    next_state.shooter_heat_limit = ClampHeatLimit(config_, shooter_heat_limit);
    next_state.cooling_rate =
        ClampCoolingValue(config_, shooter_cooling_value);
    next_state.shooter_cooling_value =
        ClampCoolingValue(config_, shooter_cooling_value);
    next_state.shooter_heat =
        ClampShooterHeat(shooter_heat, next_state.shooter_heat_limit);
    next_state.remaining_heat =
        ResolveRemainingHeat(true, next_state.shooter_heat,
                             next_state.shooter_heat_limit, 0U);
    next_state.buffer_energy = ClampBufferEnergy(config_, buffer_energy);
    next_state.remaining_energy_bits = remaining_energy_bits & 0x7F;
    next_state.fire_allowed =
        next_state.match_started && next_state.remaining_heat > 0U;

    last_rx_time_ms_ = LibXR::Thread::GetTime();
    PublishState(next_state);
  }

  LibXR::Topic& StateTopic() { return state_topic_; }

  const LibXR::Topic& StateTopic() const { return state_topic_; }

  const RefereeState& PublishedState() const { return published_state_; }

  static constexpr RawStateCache BuildRawStateCache() { return RawStateCache{}; }

  static constexpr RawStateCache ApplyGameStatusFrame(RawStateCache cache,
                                                      std::uint8_t game_type,
                                                      std::uint8_t game_progress,
                                                      std::uint16_t stage_remain_time_s) {
    cache.has_game_status = true;
    cache.game_type = game_type;
    cache.game_progress = game_progress;
    cache.stage_remain_time_s = stage_remain_time_s;
    cache.match_started = game_progress >= 4U;
    return cache;
  }

  static constexpr RawStateCache ApplyGameResultFrame(RawStateCache cache,
                                                      std::uint8_t game_result) {
    cache.has_game_result = true;
    cache.game_result = game_result;
    return cache;
  }

  static constexpr RawStateCache ApplyEventDataFrame(RawStateCache cache,
                                                     std::uint32_t event_flags) {
    cache.has_event_data = true;
    cache.event_flags = event_flags;
    return cache;
  }

  static constexpr RawStateCache ApplyRobotStatusFrame(
      RawStateCache cache, std::uint8_t robot_id, std::uint8_t robot_level,
      std::uint16_t robot_hp, std::uint16_t robot_max_hp,
      bool chassis_output_enabled,
      bool shooter_output_enabled, float chassis_power_limit_w,
      std::uint16_t cooling_rate,
      std::uint16_t shooter_heat_limit,
      std::uint16_t shooter_cooling_value) {
    cache.has_robot_status = true;
    cache.robot_id = robot_id;
    cache.robot_level = robot_level;
    cache.robot_hp = robot_hp;
    cache.robot_max_hp = robot_max_hp;
    cache.chassis_output_enabled = chassis_output_enabled;
    cache.shooter_output_enabled = shooter_output_enabled;
    cache.chassis_power_limit_w = chassis_power_limit_w;
    cache.cooling_rate = cooling_rate;
    cache.shooter_heat_limit = shooter_heat_limit;
    cache.shooter_cooling_value = shooter_cooling_value;
    return cache;
  }

  static RawStateCache ApplyRobotHpFrame(RawStateCache cache,
                                         const std::uint8_t* payload,
                                         std::size_t payload_size) {
    cache.has_robot_hp = payload_size >= 32U;
    const std::size_t slot_count =
        LibXR::min<std::size_t>(payload_size / 2U, 16U);
    for (std::size_t i = 0; i < slot_count; ++i) {
      cache.robot_hp_slots[i] = ReadU16Le(payload, i * 2U);
    }
    return cache;
  }

  static constexpr RawStateCache ApplyPowerHeatFrame(
      RawStateCache cache, std::uint16_t chassis_voltage_mv,
      std::uint16_t chassis_current_ma, float chassis_power_w,
      std::uint16_t buffer_energy,
      std::uint16_t shooter_heat) {
    cache.has_power_heat = true;
    cache.chassis_voltage_mv = chassis_voltage_mv;
    cache.chassis_current_ma = chassis_current_ma;
    cache.chassis_power_w = chassis_power_w;
    cache.buffer_energy = buffer_energy;
    cache.shooter_heat = shooter_heat;
    return cache;
  }

  static constexpr RawStateCache ApplyRobotPositionFrame(
      RawStateCache cache, float position_x_m, float position_y_m,
      float position_z_m, float position_yaw_deg) {
    cache.has_robot_position = true;
    cache.position_x_m = position_x_m;
    cache.position_y_m = position_y_m;
    cache.position_z_m = position_z_m;
    cache.position_yaw_deg = position_yaw_deg;
    return cache;
  }

  static constexpr RawStateCache ApplyBuffFrame(RawStateCache cache,
                                                std::uint16_t cooling_buff,
                                                std::uint8_t remaining_energy_bits) {
    cache.has_buff = true;
    cache.cooling_buff = cooling_buff;
    cache.remaining_energy_bits = remaining_energy_bits;
    return cache;
  }

  static constexpr RawStateCache ApplyAerialEnergyFrame(
      RawStateCache cache, std::uint8_t aerial_attack_time_s) {
    cache.has_aerial_energy = true;
    cache.aerial_attack_time_s = aerial_attack_time_s;
    return cache;
  }

  static constexpr RawStateCache ApplyHurtFrame(RawStateCache cache,
                                                std::uint8_t hurt_armor_id,
                                                std::uint8_t hurt_type) {
    cache.has_hurt = true;
    cache.hurt_armor_id = hurt_armor_id;
    cache.hurt_type = hurt_type;
    return cache;
  }

  static constexpr RawStateCache ApplyShootDataFrame(
      RawStateCache cache, float shooter_speed_mps) {
    cache.has_shoot_data = true;
    cache.shooter_speed_mps = shooter_speed_mps;
    return cache;
  }

  static constexpr RefereeState DeriveStableState(
      const RawStateCache& cache, const App::Config::RefereeConfig& config,
      bool online) {
    if (!online) {
      return BuildConservativeState(config, false);
    }

    RefereeState state = BuildConservativeState(config, true);
    state.match_started = cache.has_game_status ? cache.match_started : false;
    state.game_type = cache.has_game_status ? cache.game_type : 0U;
    state.game_progress = cache.has_game_status ? cache.game_progress : 0U;
    state.stage_remain_time_s =
        cache.has_game_status ? cache.stage_remain_time_s : 0U;
    state.game_result = cache.has_game_result ? cache.game_result : 0U;
    state.event_flags = cache.has_event_data ? cache.event_flags : 0U;
    state.robot_id = cache.has_robot_status ? cache.robot_id : 0U;
    state.robot_level =
        cache.has_robot_status ? ClampRobotLevel(cache.robot_level) : 0U;

    const std::uint16_t shooter_heat_limit =
        cache.has_robot_status
            ? ClampHeatLimit(config, cache.shooter_heat_limit)
            : ClampHeatLimit(config, config.default_shooter_heat_limit);
    state.shooter_heat_limit = shooter_heat_limit;

    const std::uint16_t base_cooling_value =
        cache.has_robot_status
            ? ClampCoolingValue(
                  config,
                  cache.cooling_rate > 0U ? cache.cooling_rate
                                          : cache.shooter_cooling_value)
            : ClampCoolingValue(config, config.default_shooter_cooling_value);
    const std::uint16_t cooling_buff =
        cache.has_buff ? ClampCoolingValue(config, cache.cooling_buff) : 0U;
    state.cooling_rate = base_cooling_value;
    state.shooter_cooling_value =
        ClampCoolingSum(config, base_cooling_value, cooling_buff);

    state.chassis_power_limit_w =
        (cache.has_robot_status && cache.chassis_output_enabled)
            ? ClampPowerLimit(config, cache.chassis_power_limit_w)
            : 0.0f;
    state.chassis_voltage_mv =
        cache.has_power_heat ? cache.chassis_voltage_mv : 0U;
    state.chassis_current_ma =
        cache.has_power_heat ? cache.chassis_current_ma : 0U;
    state.chassis_power_w =
        cache.has_power_heat ? ClampChassisPowerW(cache.chassis_power_w) : 0.0f;

    state.buffer_energy =
        cache.has_power_heat
            ? ClampBufferEnergy(config, cache.buffer_energy)
            : ClampBufferEnergy(config, config.default_buffer_energy);

    state.remaining_energy_bits =
        static_cast<std::uint8_t>((cache.has_buff ? cache.remaining_energy_bits
                                                  : config.default_remaining_energy_bits) &
                                  0x7FU);

    state.shooter_heat =
        (cache.has_robot_status && cache.has_power_heat)
            ? ClampShooterHeat(cache.shooter_heat, shooter_heat_limit)
            : ClampShooterHeat(config.default_shooter_heat, shooter_heat_limit);
    state.remaining_heat = ResolveRemainingHeat(
        cache.has_robot_status && cache.has_power_heat, state.shooter_heat,
        shooter_heat_limit, config.default_remaining_heat);
    state.robot_hp = ResolveRobotHp(cache, state.robot_id);
    state.robot_max_hp = cache.has_robot_status ? cache.robot_max_hp : 0U;
    state.hurt_armor_id = cache.has_hurt ? (cache.hurt_armor_id & 0x0FU) : 0U;
    state.hurt_type = cache.has_hurt ? (cache.hurt_type & 0x0FU) : 0U;
    state.shooter_speed_mps =
        cache.has_shoot_data ? ClampShooterSpeedMps(cache.shooter_speed_mps)
                             : 0.0f;
    state.position_x_m =
        cache.has_robot_position ? cache.position_x_m : 0.0f;
    state.position_y_m =
        cache.has_robot_position ? cache.position_y_m : 0.0f;
    state.position_z_m =
        cache.has_robot_position ? cache.position_z_m : 0.0f;
    state.position_yaw_deg =
        cache.has_robot_position ? cache.position_yaw_deg : 0.0f;
    state.aerial_attack_time_s =
        cache.has_aerial_energy ? cache.aerial_attack_time_s : 0U;

    state.fire_allowed =
        state.match_started && cache.has_robot_status && cache.has_power_heat &&
        cache.shooter_output_enabled && state.remaining_heat > 0U;
    return state;
  }

 private:
  static void UartThreadEntry(Referee* self) {
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

      self->OnRawBytesReceived(readable_size);
    }
  }

  void OnRawBytesReceived(std::size_t readable_size) {
    if (readable_size == 0) {
      return;
    }

    PushBytes(static_cast<const std::uint8_t*>(rx_chunk_.addr_), readable_size);
    TryParseFrames();
  }

  static constexpr RefereeState BuildConservativeState(
      const App::Config::RefereeConfig& config, bool online) {
    RefereeState state{};
    state.online = online;
    state.match_started = false;
    state.fire_allowed = false;
    state.chassis_power_limit_w =
        ClampPowerLimit(config, config.default_chassis_power_limit_w);
    state.chassis_power_w = 0.0f;
    state.shooter_speed_mps = 0.0f;
    state.position_x_m = 0.0f;
    state.position_y_m = 0.0f;
    state.position_z_m = 0.0f;
    state.position_yaw_deg = 0.0f;
    state.shooter_heat_limit =
        ClampHeatLimit(config, config.default_shooter_heat_limit);
    state.cooling_rate =
        ClampCoolingValue(config, config.default_shooter_cooling_value);
    state.shooter_cooling_value =
        ClampCoolingValue(config, config.default_shooter_cooling_value);
    state.shooter_heat =
        ClampShooterHeat(config.default_shooter_heat, state.shooter_heat_limit);
    state.remaining_heat = ClampRemainingHeat(config.default_remaining_heat,
                                              state.shooter_heat_limit);
    state.robot_hp = 0U;
    state.robot_max_hp = 0U;
    state.buffer_energy =
        ClampBufferEnergy(config, config.default_buffer_energy);
    state.chassis_voltage_mv = 0U;
    state.chassis_current_ma = 0U;
    state.stage_remain_time_s = 0U;
    state.event_flags = 0U;
    state.hurt_armor_id = 0U;
    state.hurt_type = 0U;
    state.game_type = 0U;
    state.game_progress = 0U;
    state.game_result = 0U;
    state.robot_id = 0U;
    state.robot_level = 0U;
    state.aerial_attack_time_s = 0U;
    state.remaining_energy_bits = config.default_remaining_energy_bits & 0x7F;
    return state;
  }

  RefereeState BuildConservativeState(bool online) const {
    return BuildConservativeState(config_, online);
  }

  static constexpr float ClampPowerLimit(
      const App::Config::RefereeConfig& config, float value) {
    if (value < 0.0f) {
      return 0.0f;
    }
    if (value > config.max_chassis_power_limit_w) {
      return config.max_chassis_power_limit_w;
    }
    return value;
  }

  static constexpr std::uint16_t ClampHeatLimit(
      const App::Config::RefereeConfig& config, std::uint16_t value) {
    if (value > config.max_shooter_heat_limit) {
      return config.max_shooter_heat_limit;
    }
    return value;
  }

  static constexpr std::uint16_t ClampCoolingValue(
      const App::Config::RefereeConfig& config, std::uint16_t value) {
    if (value > config.max_shooter_cooling_value) {
      return config.max_shooter_cooling_value;
    }
    return value;
  }

  static constexpr std::uint16_t ClampCoolingSum(
      const App::Config::RefereeConfig& config, std::uint16_t base_value,
      std::uint16_t buff_value) {
    const std::uint32_t sum = static_cast<std::uint32_t>(base_value) +
                              static_cast<std::uint32_t>(buff_value);
    if (sum > static_cast<std::uint32_t>(config.max_shooter_cooling_value)) {
      return config.max_shooter_cooling_value;
    }
    return static_cast<std::uint16_t>(sum);
  }

  static constexpr std::uint16_t ClampShooterHeat(
      std::uint16_t shooter_heat, std::uint16_t shooter_heat_limit) {
    if (shooter_heat > shooter_heat_limit) {
      return shooter_heat_limit;
    }
    return shooter_heat;
  }

  static constexpr std::uint16_t ClampBufferEnergy(
      const App::Config::RefereeConfig& config, std::uint16_t value) {
    if (value > config.max_buffer_energy) {
      return config.max_buffer_energy;
    }
    return value;
  }

  static constexpr std::uint8_t ClampRobotLevel(std::uint8_t value) {
    return value > 10U ? 10U : value;
  }

  static constexpr float ClampShooterSpeedMps(float value) {
    if (value < 0.0f) {
      return 0.0f;
    }
    if (value > 60.0f) {
      return 60.0f;
    }
    return value;
  }

  static constexpr float ClampChassisPowerW(float value) {
    if (value < 0.0f) {
      return 0.0f;
    }
    if (value > 500.0f) {
      return 500.0f;
    }
    return value;
  }

  static constexpr std::uint16_t ClampRemainingHeat(
      std::uint16_t remaining_heat, std::uint16_t shooter_heat_limit) {
    if (remaining_heat > shooter_heat_limit) {
      return shooter_heat_limit;
    }
    return remaining_heat;
  }

  static constexpr std::uint16_t ResolveRemainingHeat(
      bool has_complete_heat_window, std::uint16_t shooter_heat,
      std::uint16_t shooter_heat_limit, std::uint16_t conservative_default) {
    if (!has_complete_heat_window) {
      return ClampRemainingHeat(conservative_default, shooter_heat_limit);
    }
    if (shooter_heat >= shooter_heat_limit) {
      return 0U;
    }
    return static_cast<std::uint16_t>(shooter_heat_limit - shooter_heat);
  }

  static constexpr std::uint16_t ReadU16Le(const std::uint8_t* data,
                                           std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
  }

  static float ReadF32Le(const std::uint8_t* data, std::size_t offset) {
    const std::uint32_t value =
        static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    float output = 0.0f;
    std::memcpy(&output, &value, sizeof(output));
    return output;
  }

  static constexpr std::uint32_t ReadU32Le(const std::uint8_t* data,
                                           std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
  }

  static constexpr int ResolveRobotHpSlotIndex(std::uint8_t robot_id) {
    switch (robot_id) {
      case 1:
        return 0;
      case 2:
        return 1;
      case 3:
        return 2;
      case 4:
        return 3;
      case 5:
        return 4;
      case 7:
        return 5;
      case 11:
        return 6;
      case 12:
        return 7;
      case 101:
        return 8;
      case 102:
        return 9;
      case 103:
        return 10;
      case 104:
        return 11;
      case 105:
        return 12;
      case 107:
        return 13;
      case 111:
        return 14;
      case 112:
        return 15;
      default:
        return -1;
    }
  }

  static constexpr std::uint16_t ResolveRobotHp(const RawStateCache& cache,
                                                std::uint8_t robot_id) {
    if (!cache.has_robot_status) {
      return 0U;
    }

    const int slot_index = ResolveRobotHpSlotIndex(robot_id);
    if (cache.has_robot_hp && slot_index >= 0) {
      return cache.robot_hp_slots[slot_index];
    }
    return cache.robot_hp;
  }

  void PublishState(const RefereeState& state) {
    published_state_ = state;
    state_topic_.Publish(published_state_);
  }

  void PublishDerivedState(bool online) {
    PublishState(DeriveStableState(raw_state_cache_, config_, online));
  }

  void ResetRawStateCache() { raw_state_cache_ = BuildRawStateCache(); }

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
    while (rx_window_size_ >= 9) {
      std::size_t frame_size = 0;
      if (!TryParseSingleFrame(rx_window_, rx_window_size_, frame_size)) {
        if (frame_size > rx_window_size_) {
          break;
        }
        ConsumeBytes(1);
        continue;
      }
      ConsumeBytes(frame_size);
    }
  }

  bool TryParseSingleFrame(const std::uint8_t* frame, std::size_t size,
                           std::size_t& frame_size) {
    if (size < 9 || frame[0] != 0xA5) {
      return false;
    }
    if (LibXR::CRC8::Calculate(frame, 4) != frame[4]) {
      return false;
    }

    const std::uint16_t data_length =
        static_cast<std::uint16_t>(frame[1] | (frame[2] << 8));
    frame_size = static_cast<std::size_t>(data_length) + 9;
    if (frame_size > size || data_length == 0) {
      return false;
    }

    const std::uint16_t expected_crc16 =
        LibXR::CRC16::Calculate(frame, frame_size - 2);
    const std::uint16_t received_crc16 = static_cast<std::uint16_t>(
        frame[frame_size - 2] | (frame[frame_size - 1] << 8));
    if (expected_crc16 != received_crc16) {
      return false;
    }

    last_rx_time_ms_ = LibXR::Thread::GetTime();
    const std::uint16_t cmd_id =
        static_cast<std::uint16_t>(frame[5] | (frame[6] << 8));
    const std::uint8_t* payload = frame + 7;
    switch (cmd_id) {
      case 0x0001:
        if (data_length >= 3) {
          const std::uint8_t game_type =
              static_cast<std::uint8_t>(payload[0] & 0x0F);
          const std::uint8_t game_progress = static_cast<std::uint8_t>(
              (payload[0] >> 4) & 0x0F);
          raw_state_cache_ =
              ApplyGameStatusFrame(raw_state_cache_, game_type, game_progress,
                                   ReadU16Le(payload, 1));
        } else if (data_length >= 1) {
          const std::uint8_t game_type =
              static_cast<std::uint8_t>(payload[0] & 0x0F);
          const std::uint8_t game_progress = static_cast<std::uint8_t>(
              (payload[0] >> 4) & 0x0F);
          raw_state_cache_ = ApplyGameStatusFrame(raw_state_cache_, game_type,
                                                  game_progress, 0U);
        }
        break;
      case 0x0002:
        if (data_length >= 1) {
          raw_state_cache_ =
              ApplyGameResultFrame(raw_state_cache_, payload[0]);
        }
        break;
      case 0x0003:
        if (data_length >= 32) {
          raw_state_cache_ =
              ApplyRobotHpFrame(raw_state_cache_, payload, data_length);
        }
        break;
      case 0x0101:
        if (data_length >= 4) {
          raw_state_cache_ =
              ApplyEventDataFrame(raw_state_cache_, ReadU32Le(payload, 0));
        }
        break;
      case 0x0201:
        if (data_length >= 13) {
          const std::uint8_t output_flags = payload[12];
          raw_state_cache_ = ApplyRobotStatusFrame(
              raw_state_cache_, payload[0], payload[1], ReadU16Le(payload, 2),
              ReadU16Le(payload, 4),
              (output_flags & (1U << 1U)) != 0U,
              (output_flags & (1U << 2U)) != 0U,
              static_cast<float>(ReadU16Le(payload, 10)), ReadU16Le(payload, 6),
              ReadU16Le(payload, 8), ReadU16Le(payload, 6));
        }
        break;
      case 0x0202:
        if (data_length >= 12) {
          raw_state_cache_ = ApplyPowerHeatFrame(
              raw_state_cache_, ReadU16Le(payload, 0), ReadU16Le(payload, 2),
              ReadF32Le(payload, 4), ReadU16Le(payload, 8),
              ReadU16Le(payload, 10));
        }
        break;
      case 0x0203:
        if (data_length >= 16) {
          raw_state_cache_ = ApplyRobotPositionFrame(
              raw_state_cache_, ReadF32Le(payload, 0), ReadF32Le(payload, 4),
              ReadF32Le(payload, 8), ReadF32Le(payload, 12));
        }
        break;
      case 0x0204:
        if (data_length >= 8) {
          raw_state_cache_ = ApplyBuffFrame(
              raw_state_cache_, ReadU16Le(payload, 1),
              static_cast<std::uint8_t>(payload[7] & 0x7F));
        }
        break;
      case 0x0205:
        if (data_length >= 1) {
          raw_state_cache_ =
              ApplyAerialEnergyFrame(raw_state_cache_, payload[0]);
        }
        break;
      case 0x0206:
        if (data_length >= 1) {
          raw_state_cache_ = ApplyHurtFrame(raw_state_cache_, payload[0] & 0x0F,
                                            (payload[0] >> 4) & 0x0F);
        }
        break;
      case 0x0207:
        if (data_length >= 7) {
          raw_state_cache_ = ApplyShootDataFrame(raw_state_cache_,
                                                 ReadF32Le(payload, 3));
        }
        break;
      default:
        break;
    }

    PublishDerivedState(true);
    return true;
  }

  LibXR::UART* uart_ = nullptr;
  LibXR::Topic state_topic_;
  const App::Config::RefereeConfig config_;
  LibXR::Thread uart_thread_;

  std::uint8_t rx_buffer_[kRxChunkSize]{};
  LibXR::RawData rx_chunk_;
  std::uint8_t rx_window_[kRxWindowSize]{};
  std::size_t rx_window_size_ = 0;

  RefereeState published_state_{};
  RawStateCache raw_state_cache_{};
  std::uint32_t last_rx_time_ms_ = 0;
};
