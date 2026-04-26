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
  std::uint16_t shooter_heat_limit = 0;
  std::uint16_t shooter_cooling_value = 0;
  std::uint16_t shooter_heat = 0;
  std::uint16_t remaining_heat = 0;
  std::uint16_t buffer_energy = 0;
  std::uint8_t remaining_energy_bits = 0;
};

class Referee : public LibXR::Application {
 public:
  struct RawStateCache {
    bool has_game_status = false;
    bool has_robot_status = false;
    bool has_power_heat = false;
    bool has_buff = false;
    bool match_started = false;
    bool chassis_output_enabled = false;
    bool shooter_output_enabled = false;
    float chassis_power_limit_w = 0.0f;
    std::uint16_t shooter_heat_limit = 0;
    std::uint16_t shooter_cooling_value = 0;
    std::uint16_t shooter_heat = 0;
    std::uint16_t buffer_energy = 0;
    std::uint16_t cooling_buff = 0;
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
                                                      std::uint8_t game_progress) {
    cache.has_game_status = true;
    cache.match_started = game_progress >= 4U;
    return cache;
  }

  static constexpr RawStateCache ApplyRobotStatusFrame(
      RawStateCache cache, bool chassis_output_enabled,
      bool shooter_output_enabled, float chassis_power_limit_w,
      std::uint16_t shooter_heat_limit,
      std::uint16_t shooter_cooling_value) {
    cache.has_robot_status = true;
    cache.chassis_output_enabled = chassis_output_enabled;
    cache.shooter_output_enabled = shooter_output_enabled;
    cache.chassis_power_limit_w = chassis_power_limit_w;
    cache.shooter_heat_limit = shooter_heat_limit;
    cache.shooter_cooling_value = shooter_cooling_value;
    return cache;
  }

  static constexpr RawStateCache ApplyPowerHeatFrame(RawStateCache cache,
                                                     std::uint16_t buffer_energy,
                                                     std::uint16_t shooter_heat) {
    cache.has_power_heat = true;
    cache.buffer_energy = buffer_energy;
    cache.shooter_heat = shooter_heat;
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

  static constexpr RefereeState DeriveStableState(
      const RawStateCache& cache, const App::Config::RefereeConfig& config,
      bool online) {
    if (!online) {
      return BuildConservativeState(config, false);
    }

    RefereeState state = BuildConservativeState(config, true);
    state.match_started = cache.has_game_status ? cache.match_started : false;

    const std::uint16_t shooter_heat_limit =
        cache.has_robot_status
            ? ClampHeatLimit(config, cache.shooter_heat_limit)
            : ClampHeatLimit(config, config.default_shooter_heat_limit);
    state.shooter_heat_limit = shooter_heat_limit;

    const std::uint16_t base_cooling_value =
        cache.has_robot_status
            ? ClampCoolingValue(config, cache.shooter_cooling_value)
            : ClampCoolingValue(config, config.default_shooter_cooling_value);
    const std::uint16_t cooling_buff =
        cache.has_buff ? ClampCoolingValue(config, cache.cooling_buff) : 0U;
    state.shooter_cooling_value =
        ClampCoolingSum(config, base_cooling_value, cooling_buff);

    state.chassis_power_limit_w =
        (cache.has_robot_status && cache.chassis_output_enabled)
            ? ClampPowerLimit(config, cache.chassis_power_limit_w)
            : 0.0f;

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
    state.shooter_heat_limit =
        ClampHeatLimit(config, config.default_shooter_heat_limit);
    state.shooter_cooling_value =
        ClampCoolingValue(config, config.default_shooter_cooling_value);
    state.shooter_heat =
        ClampShooterHeat(config.default_shooter_heat, state.shooter_heat_limit);
    state.remaining_heat = ClampRemainingHeat(config.default_remaining_heat,
                                              state.shooter_heat_limit);
    state.buffer_energy =
        ClampBufferEnergy(config, config.default_buffer_energy);
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
        if (data_length >= 1) {
          const std::uint8_t game_progress = static_cast<std::uint8_t>(
              (payload[0] >> 4) & 0x0F);
          raw_state_cache_ =
              ApplyGameStatusFrame(raw_state_cache_, game_progress);
        }
        break;
      case 0x0201:
        if (data_length >= 13) {
          raw_state_cache_ = ApplyRobotStatusFrame(
              raw_state_cache_, (payload[12] & (1U << 1U)) != 0U,
              (payload[12] & (1U << 2U)) != 0U,
              static_cast<float>(payload[10] | (payload[11] << 8)),
              static_cast<std::uint16_t>(payload[8] | (payload[9] << 8)),
              static_cast<std::uint16_t>(payload[6] | (payload[7] << 8)));
        }
        break;
      case 0x0202:
        if (data_length >= 14) {
          raw_state_cache_ = ApplyPowerHeatFrame(
              raw_state_cache_,
              static_cast<std::uint16_t>(payload[8] | (payload[9] << 8)),
              static_cast<std::uint16_t>(payload[10] | (payload[11] << 8)));
        }
        break;
      case 0x0204:
        if (data_length >= 8) {
          raw_state_cache_ = ApplyBuffFrame(
              raw_state_cache_,
              static_cast<std::uint16_t>(payload[1] | (payload[2] << 8)),
              static_cast<std::uint8_t>(payload[7] & 0x7F));
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
