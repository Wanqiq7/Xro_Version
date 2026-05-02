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

#include <array>
#include <cstdint>
#include <cstring>

#include "../../App/Config/BridgeConfig.hpp"
#include "app_framework.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "message.hpp"
#include "mutex.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "utils/crc.hpp"
#include "uart.hpp"

using LibXR::ErrorCode;

enum class MasterMachineFireMode : std::uint8_t {
  kNoFire = 0,
  kAutoFire = 1,
  kAutoAim = 2,
};

enum class MasterMachineTargetState : std::uint8_t {
  kNoTarget = 0,
  kTargetConverging = 1,
  kReadyToFire = 2,
};

enum class MasterMachineTargetType : std::uint8_t {
  kNoTarget = 0,
  kHero1 = 1,
  kEngineer2 = 2,
  kInfantry3 = 3,
  kInfantry4 = 4,
  kInfantry5 = 5,
  kOutpost = 6,
  kSentry = 7,
  kBase = 8,
};

/**
 * @brief master_machine 私有状态
 *
 * donor 字段来自 basic_framework 的 Vision_Recv_s 语义；bit[8]/bit[9] 等本地扩展
 * 只用于当前工程的桥接安全策略，不等同于 donor 已冻结事实，也不进入 App 公共
 * Topic 契约。
 */
struct MasterMachineState {
  // 本地扩展：通信在线与桥接安全开关。
  bool online = false;
  bool request_manual_override = false;
  bool request_fire_enable = false;

  // donor typed 语义：flags 的 bit layout 在 donor 中仍是 TODO，这里只承载解析结果。
  MasterMachineFireMode fire_mode = MasterMachineFireMode::kNoFire;
  MasterMachineTargetState target_state = MasterMachineTargetState::kNoTarget;
  MasterMachineTargetType target_type = MasterMachineTargetType::kNoTarget;
  float vision_pitch_deg = 0.0f;
  float vision_yaw_deg = 0.0f;

  // 本地扩展：兼容现有 App 输入桥字段，白名单仍只决定是否放行，不定义 wire format。
  float target_vx_mps = 0.0f;
  float target_vy_mps = 0.0f;
  float target_yaw_deg = 0.0f;
  float target_pitch_deg = 0.0f;
  bool vision_target_valid = false;
  std::uint8_t vision_target_type = 0;
  float vision_target_yaw_deg = 0.0f;
  float vision_target_pitch_deg = 0.0f;
};

class MasterMachine : public LibXR::Application {
 public:
  enum class EnemyColor : std::uint8_t {
    kNone = 0,
    kBlue = 1,
    kRed = 2,
  };

  enum class WorkMode : std::uint8_t {
    kAim = 0,
    kSmallBuff = 1,
    kBigBuff = 2,
  };

  enum class BulletSpeed : std::uint8_t {
    kNone = 0,
    kBigAmmo10 = 10,
    kSmallAmmo15 = 15,
    kBigAmmo16 = 16,
    kSmallAmmo18 = 18,
    kSmallAmmo30 = 30,
  };

  /**
   * @brief donor 风格视觉发送 typed 数据
   *
   * 当前工程把 donor TX flags 冻结为：
   * bit[1:0]=enemy_color, bit[3:2]=work_mode, bit[15:8]=bullet_speed。
   * work_mode 的 small_buff / big_buff 仅作为 wire enum 发送；App 暂无业务入口时由
   * MasterMachineBridgeController 保守选择 kAim。
   */
  struct VisionSendData {
    EnemyColor enemy_color = EnemyColor::kNone;
    WorkMode work_mode = WorkMode::kAim;
    BulletSpeed bullet_speed = BulletSpeed::kNone;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
  };

  /**
   * @brief App 桥接层使用的 donor TX 状态，保持简单标量字段便于跨层填充。
   */
  struct DonorTxStatus {
    std::uint8_t enemy_color = 0;
    std::uint8_t work_mode = 0;
    std::uint8_t bullet_speed = 0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
  };

  using TxStatus = DonorTxStatus;

  /**
   * @brief 兼容当前 App 侧旧调用名，实际发送已改为 donor 22-byte TX 帧。
   */
  struct VisionStatusSummary {
    std::uint8_t enemy_color = 0;
    std::uint8_t work_mode = 0;
    std::uint8_t target_type = 0;
    bool target_locked = false;
    float bullet_speed_mps = 0.0f;
    float robot_yaw_deg = 0.0f;
    float robot_pitch_deg = 0.0f;
    float robot_roll_deg = 0.0f;
  };

  static constexpr const char* kTopicName = "master_machine_state";
  static constexpr std::size_t kRxChunkSize = 36;
  static constexpr std::size_t kRxWindowSize = 96;

  explicit MasterMachine(
      LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
      App::Config::MasterMachineConfig config = App::Config::kMasterMachineConfig)
      : config_(config),
        state_topic_(LibXR::Topic::CreateTopic<MasterMachineState>(
            kTopicName, nullptr, false, true, true)) {
    BindLinks(hw);
    PublishSafeState();
    app.Register(*this);
  }

  void OnMonitor() override {
    if (!App::Config::BridgeDirectionAllowsRx(config_.direction)) {
      PublishSafeState();
      return;
    }

    const std::uint32_t now_ms = LibXR::Thread::GetTime();
    bool timed_out = false;
    {
      LibXR::Mutex::LockGuard lock_guard(state_mutex_);
      if (!state_.online) {
        return;
      }
      timed_out = (now_ms - last_rx_time_ms_) > config_.offline_timeout_ms;
    }

    if (timed_out) {
      PublishSafeState();
    }
  }

  /**
   * @brief 供后续真实协议解析接入时更新桥接状态
   *
   * RX 线程解析出合法帧后从这里写入模块私有状态。多链路同时启用时采用
   * last-writer-wins：任一链路收到合法帧即刷新全局 online 状态。
   */
  void UpdateRxState(const MasterMachineState& next_state) {
    if (!App::Config::BridgeDirectionAllowsRx(config_.direction)) {
      PublishSafeState();
      return;
    }

    MasterMachineState published_state{};
    {
      LibXR::Mutex::LockGuard lock_guard(state_mutex_);
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
      if (!App::Config::MasterMachineFieldEnabled(
              config_, App::Config::MasterMachineInputField::kVisionTarget)) {
        state_.vision_target_valid = false;
        state_.vision_target_type = 0U;
        state_.vision_target_yaw_deg = 0.0f;
        state_.vision_target_pitch_deg = 0.0f;
      }
      // 收到合法帧即视为在线，零值目标同样是合法的“保持静止”输入。
      state_.online = true;
      last_rx_time_ms_ = LibXR::Thread::GetTime();
      published_state = state_;
    }
    state_topic_.Publish(published_state);
  }

  LibXR::Topic& StateTopic() { return state_topic_; }

  const LibXR::Topic& StateTopic() const { return state_topic_; }

  const App::Config::MasterMachineConfig& Config() const { return config_; }

  const MasterMachineState& State() const { return state_; }

  ErrorCode SendVisionData(const VisionSendData& data) {
    if (!App::Config::BridgeDirectionAllowsTx(config_.direction)) {
      return ErrorCode::FAILED;
    }

    std::array<std::uint8_t, kVisionTxFrameSize> frame{};
    frame[0] = kProtocolSof;
    frame[1] = static_cast<std::uint8_t>(kVisionTxDataLength & 0xFFu);
    frame[2] = static_cast<std::uint8_t>((kVisionTxDataLength >> 8) & 0xFFu);
    frame[3] = LibXR::CRC8::Calculate(frame.data(), 3);
    frame[kCommandIdOffset] = static_cast<std::uint8_t>(kVisionTxCommandId & 0xFFu);
    frame[kCommandIdOffset + 1] =
        static_cast<std::uint8_t>((kVisionTxCommandId >> 8) & 0xFFu);

    const std::uint16_t flags = EncodeTxFlags(data);
    frame[kPayloadOffset + 0] = static_cast<std::uint8_t>(flags & 0xFFu);
    frame[kPayloadOffset + 1] = static_cast<std::uint8_t>((flags >> 8) & 0xFFu);
    const float yaw_rad = DegreesToRadians(data.yaw);
    const float pitch_rad = DegreesToRadians(data.pitch);
    const float roll_rad = DegreesToRadians(data.roll);
    std::memcpy(frame.data() + kVisionTxYawOffset, &yaw_rad, sizeof(float));
    std::memcpy(frame.data() + kVisionTxPitchOffset, &pitch_rad, sizeof(float));
    std::memcpy(frame.data() + kVisionTxRollOffset, &roll_rad, sizeof(float));

    const std::uint16_t crc16 =
        LibXR::CRC16::Calculate(frame.data(), frame.size() - 2);
    frame[frame.size() - 2] = static_cast<std::uint8_t>(crc16 & 0xFFu);
    frame[frame.size() - 1] = static_cast<std::uint8_t>((crc16 >> 8) & 0xFFu);

    ErrorCode last_error = ErrorCode::FAILED;
    bool has_success = false;
    for (const auto& link : links_) {
      if (link.uart == nullptr || !link.enable_tx) {
        continue;
      }

      LibXR::WriteOperation op;
      const ErrorCode result =
          link.uart->Write(LibXR::ConstRawData(frame.data(), frame.size()), op);
      if (result == ErrorCode::OK) {
        has_success = true;
      } else {
        last_error = result;
      }
    }
    return has_success ? ErrorCode::OK : last_error;
  }

  ErrorCode SendDonorTxStatus(const DonorTxStatus& status) {
    VisionSendData data{};
    data.enemy_color = static_cast<EnemyColor>(status.enemy_color);
    data.work_mode = static_cast<WorkMode>(status.work_mode);
    data.bullet_speed = static_cast<BulletSpeed>(status.bullet_speed);
    data.yaw = status.yaw;
    data.pitch = status.pitch;
    data.roll = status.roll;
    return SendVisionData(data);
  }

  ErrorCode SendTxStatus(const TxStatus& status) { return SendDonorTxStatus(status); }

  ErrorCode SendVisionStatus(const VisionStatusSummary& status) {
    VisionSendData data{};
    data.enemy_color = static_cast<EnemyColor>(status.enemy_color);
    data.work_mode = static_cast<WorkMode>(status.work_mode);
    data.bullet_speed = BulletSpeed::kNone;
    data.yaw = status.robot_yaw_deg;
    data.pitch = status.robot_pitch_deg;
    data.roll = status.robot_roll_deg;
    return SendVisionData(data);
  }

 private:
  static constexpr std::uint8_t kProtocolSof = 0xA5;
  static constexpr float kDegToRad = static_cast<float>(LibXR::PI / 180.0);
  static constexpr float kRadToDeg = static_cast<float>(180.0 / LibXR::PI);
  static constexpr std::size_t kProtocolHeaderSize = 4;
  static constexpr std::size_t kProtocolOverheadSize = 8;
  static constexpr std::uint16_t kVisionRxCommandId = 0x0001;
  static constexpr std::uint16_t kVisionTxCommandId = 0x0002;
  static constexpr std::size_t kCommandIdOffset = kProtocolHeaderSize;
  static constexpr std::size_t kCommandIdSize = sizeof(std::uint16_t);
  static constexpr std::size_t kPayloadOffset =
      kProtocolHeaderSize + kCommandIdSize;

  // donor RX 兼容帧：flags(2) + pitch(float) + yaw(float)，总长 18 字节。
  static constexpr std::size_t kFlagsFieldSize = 2;
  static constexpr std::size_t kVisionRxFloatCount = 2;
  static constexpr std::size_t kVisionRxDataLength =
      kFlagsFieldSize + sizeof(float) * kVisionRxFloatCount;
  static constexpr std::size_t kVisionRxFrameSize =
      kProtocolOverheadSize + kVisionRxDataLength;
  static constexpr std::size_t kVisionRxPitchOffset =
      kPayloadOffset + kFlagsFieldSize;
  static constexpr std::size_t kVisionRxYawOffset =
      kVisionRxPitchOffset + sizeof(float);

  // donor TX 兼容帧：cmd_id=0x0002, flags(2) + yaw/pitch/roll，总长 22 字节。
  static constexpr std::size_t kVisionTxFloatCount = 3;
  static constexpr std::size_t kVisionTxDataLength =
      kFlagsFieldSize + sizeof(float) * kVisionTxFloatCount;
  static constexpr std::size_t kVisionTxFrameSize =
      kProtocolOverheadSize + kVisionTxDataLength;
  static constexpr std::size_t kVisionTxYawOffset =
      kPayloadOffset + kFlagsFieldSize;
  static constexpr std::size_t kVisionTxPitchOffset =
      kVisionTxYawOffset + sizeof(float);
  static constexpr std::size_t kVisionTxRollOffset =
      kVisionTxPitchOffset + sizeof(float);

  struct LinkContext {
    MasterMachine* owner = nullptr;
    LibXR::UART* uart = nullptr;
    const char* device_name = nullptr;
    bool enable_rx = true;
    bool enable_tx = true;
    LibXR::Thread rx_thread_;
    std::uint8_t rx_buffer_[kRxChunkSize]{};
    LibXR::RawData rx_chunk_{rx_buffer_, sizeof(rx_buffer_)};
    std::uint8_t rx_window_[kRxWindowSize]{};
    std::size_t rx_window_size_ = 0;
  };

  enum class FlagBit : std::uint8_t {
    // donor 未完成 flags 解码；以下高位仅是当前迁移约定，用于桥接安全字段。
    kManualOverride = 8,
    kFireEnable = 9,
  };

  struct RxFlagsLayout {
    static constexpr std::uint16_t kFireModeMask = 0x0003u;
    static constexpr std::uint8_t kFireModeShift = 0u;
    static constexpr std::uint16_t kTargetStateMask = 0x000Cu;
    static constexpr std::uint8_t kTargetStateShift = 2u;
    static constexpr std::uint16_t kTargetTypeMask = 0x00F0u;
    static constexpr std::uint8_t kTargetTypeShift = 4u;
  };

  struct TxFlagsLayout {
    static constexpr std::uint16_t kEnemyColorMask = 0x0003u;
    static constexpr std::uint8_t kEnemyColorShift = 0u;
    static constexpr std::uint16_t kWorkModeMask = 0x000Cu;
    static constexpr std::uint8_t kWorkModeShift = 2u;
    static constexpr std::uint16_t kBulletSpeedMask = 0xFF00u;
    static constexpr std::uint8_t kBulletSpeedShift = 8u;
  };

  static constexpr bool FlagIsSet(std::uint16_t flags, FlagBit bit) {
    return (flags & (1u << static_cast<std::uint8_t>(bit))) != 0u;
  }

  static constexpr float DegreesToRadians(float degrees) {
    return degrees * kDegToRad;
  }

  static constexpr float RadiansToDegrees(float radians) {
    return radians * kRadToDeg;
  }

  static constexpr MasterMachineFireMode DecodeFireMode(std::uint16_t flags) {
    const auto value = static_cast<std::uint8_t>(
        (flags & RxFlagsLayout::kFireModeMask) >>
        RxFlagsLayout::kFireModeShift);
    if (value > static_cast<std::uint8_t>(MasterMachineFireMode::kAutoAim)) {
      return MasterMachineFireMode::kNoFire;
    }
    return static_cast<MasterMachineFireMode>(value);
  }

  static constexpr MasterMachineTargetState DecodeTargetState(std::uint16_t flags) {
    const auto value = static_cast<std::uint8_t>(
        (flags & RxFlagsLayout::kTargetStateMask) >>
        RxFlagsLayout::kTargetStateShift);
    if (value >
        static_cast<std::uint8_t>(MasterMachineTargetState::kReadyToFire)) {
      return MasterMachineTargetState::kNoTarget;
    }
    return static_cast<MasterMachineTargetState>(value);
  }

  static constexpr MasterMachineTargetType DecodeTargetType(std::uint16_t flags) {
    const auto value = static_cast<std::uint8_t>(
        (flags & RxFlagsLayout::kTargetTypeMask) >>
        RxFlagsLayout::kTargetTypeShift);
    if (value > static_cast<std::uint8_t>(MasterMachineTargetType::kBase)) {
      return MasterMachineTargetType::kNoTarget;
    }
    return static_cast<MasterMachineTargetType>(value);
  }

  static constexpr std::uint16_t EncodeTxFlags(const VisionSendData& data) {
    return static_cast<std::uint16_t>(
        ((static_cast<std::uint16_t>(data.bullet_speed)
          << TxFlagsLayout::kBulletSpeedShift) &
         TxFlagsLayout::kBulletSpeedMask) |
        ((static_cast<std::uint16_t>(data.work_mode)
          << TxFlagsLayout::kWorkModeShift) &
         TxFlagsLayout::kWorkModeMask) |
        ((static_cast<std::uint16_t>(data.enemy_color)
          << TxFlagsLayout::kEnemyColorShift) &
         TxFlagsLayout::kEnemyColorMask));
  }

  void BindLinks(LibXR::HardwareContainer& hw) {
    bool has_any_link = false;
    std::size_t link_index = 0;

    for (const auto& link_config : config_.links) {
      if (link_index >= links_.size() || link_config.device_name == nullptr ||
          link_config.device_name[0] == '\0') {
        ++link_index;
        continue;
      }

      auto& link = links_[link_index++];
      link.owner = this;
      link.device_name = link_config.device_name;
      link.enable_rx = link_config.enable_rx;
      link.enable_tx = link_config.enable_tx;
      link.uart = hw.template Find<LibXR::UART>(link_config.device_name);
      if (link.uart == nullptr) {
        continue;
      }

      has_any_link = true;
      if (App::Config::BridgeDirectionAllowsRx(config_.direction) &&
          link.enable_rx) {
        link.rx_thread_.Create(&link, UartThreadEntry, "MasterMachine::Rx",
                               config_.task_stack_depth,
                               LibXR::Thread::Priority::REALTIME);
      }
    }

    ASSERT(has_any_link);
  }

  static void UartThreadEntry(LinkContext* link) {
    LibXR::Semaphore semaphore;
    LibXR::ReadOperation read_operation(semaphore);

    while (true) {
      if (link->uart == nullptr) {
        return;
      }

      link->uart->Read({nullptr, 0}, read_operation);

      const std::size_t readable_size =
          LibXR::min(link->uart->read_port_->Size(), link->rx_chunk_.size_);
      if (readable_size == 0) {
        continue;
      }

      const auto result = link->uart->Read(
          LibXR::RawData{link->rx_chunk_.addr_, readable_size}, read_operation);
      if (result != ErrorCode::OK) {
        continue;
      }

      link->owner->PushBytes(*link,
                             static_cast<const std::uint8_t*>(link->rx_chunk_.addr_),
                             readable_size);
      link->owner->TryParseFrames(*link);
    }
  }

  void PushBytes(LinkContext& link, const std::uint8_t* data, std::size_t size) {
    if (size >= kRxWindowSize) {
      const auto* tail = data + size - kRxWindowSize;
      std::memcpy(link.rx_window_, tail, kRxWindowSize);
      link.rx_window_size_ = kRxWindowSize;
      return;
    }

    if ((link.rx_window_size_ + size) > kRxWindowSize) {
      const std::size_t overflow = link.rx_window_size_ + size - kRxWindowSize;
      std::memmove(link.rx_window_, link.rx_window_ + overflow,
                   link.rx_window_size_ - overflow);
      link.rx_window_size_ -= overflow;
    }

    std::memcpy(link.rx_window_ + link.rx_window_size_, data, size);
    link.rx_window_size_ += size;
  }

  void ConsumeBytes(LinkContext& link, std::size_t size) {
    if (size >= link.rx_window_size_) {
      link.rx_window_size_ = 0;
      return;
    }

    std::memmove(link.rx_window_, link.rx_window_ + size,
                 link.rx_window_size_ - size);
    link.rx_window_size_ -= size;
  }

  void TryParseFrames(LinkContext& link) {
    while (link.rx_window_size_ >= kProtocolOverheadSize) {
      MasterMachineState next_state{};
      std::size_t frame_size = 0;
      if (TryDecodeFrame(link.rx_window_, link.rx_window_size_, next_state,
                         frame_size)) {
        UpdateRxState(next_state);
        ConsumeBytes(link, frame_size);
        continue;
      }
      if (frame_size > link.rx_window_size_) {
        break;
      }
      ConsumeBytes(link, 1);
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
    if (data_length != kVisionRxDataLength || frame_size != kVisionRxFrameSize ||
        frame_size > size) {
      return false;
    }

    const std::uint16_t command_id = static_cast<std::uint16_t>(
        frame[kCommandIdOffset] | (frame[kCommandIdOffset + 1] << 8));
    if (command_id != kVisionRxCommandId) {
      return false;
    }

    const std::uint16_t expected_crc16 =
        LibXR::CRC16::Calculate(frame, frame_size - 2);
    const std::uint16_t received_crc16 = static_cast<std::uint16_t>(
        frame[frame_size - 2] | (frame[frame_size - 1] << 8));
    if (expected_crc16 != received_crc16) {
      return false;
    }

    const std::uint16_t flags_register = static_cast<std::uint16_t>(
        frame[kPayloadOffset + 0] | (frame[kPayloadOffset + 1] << 8));
    float vision_pitch = 0.0f;
    float vision_yaw = 0.0f;
    std::memcpy(&vision_pitch, frame + kVisionRxPitchOffset, sizeof(float));
    std::memcpy(&vision_yaw, frame + kVisionRxYawOffset, sizeof(float));

    out_state.online = true;
    // donor 的 flags bit layout 尚未完成；这里采用当前迁移约定填充 typed 字段。
    out_state.fire_mode = DecodeFireMode(flags_register);
    out_state.target_state = DecodeTargetState(flags_register);
    out_state.target_type = DecodeTargetType(flags_register);
    out_state.vision_pitch_deg = RadiansToDegrees(vision_pitch);
    out_state.vision_yaw_deg = RadiansToDegrees(vision_yaw);

    // 只保留 BridgeConfig 白名单允许进入 App 的本地扩展语义。
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kManualOverride)) {
      out_state.request_manual_override =
          FlagIsSet(flags_register, FlagBit::kManualOverride);
    }
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kFireEnable)) {
      out_state.request_fire_enable = FlagIsSet(flags_register,
                                                FlagBit::kFireEnable);
    }

    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetPitch)) {
      out_state.target_pitch_deg = out_state.vision_pitch_deg;
    }
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kTargetYaw)) {
      out_state.target_yaw_deg = out_state.vision_yaw_deg;
    }
    if (App::Config::MasterMachineFieldEnabled(
            config_, App::Config::MasterMachineInputField::kVisionTarget)) {
      out_state.vision_target_pitch_deg = out_state.vision_pitch_deg;
      out_state.vision_target_yaw_deg = out_state.vision_yaw_deg;
      out_state.vision_target_valid =
          out_state.target_state != MasterMachineTargetState::kNoTarget;
      out_state.vision_target_type =
          static_cast<std::uint8_t>(out_state.target_type);
    }
    return true;
  }

  void PublishSafeState() {
    MasterMachineState published_state{};
    {
      LibXR::Mutex::LockGuard lock_guard(state_mutex_);
      state_ = {};
      last_rx_time_ms_ = 0;
      published_state = state_;
    }
    state_topic_.Publish(published_state);
  }

  App::Config::MasterMachineConfig config_{};
  LibXR::Topic state_topic_;
  LibXR::Mutex state_mutex_;
  std::array<LinkContext, App::Config::kMasterMachineMaxLinks> links_{};
  MasterMachineState state_{};
  std::uint32_t last_rx_time_ms_ = 0;
};
