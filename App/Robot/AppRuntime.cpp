#include "AppRuntime.hpp"

#include "../Config/ChassisConfig.hpp"
#include "../Config/GimbalConfig.hpp"
#include "../Config/ShootConfig.hpp"
#include "../Config/AppConfig.hpp"
#include "../Config/BridgeConfig.hpp"
#include "../Config/RefereeConfig.hpp"

namespace {

DT7::Config MakePrimaryRemoteConfig() {
  DT7::Config config;
  config.uart_name = DT7::kDefaultUartName;
  config.task_stack_depth = DT7::kDefaultTaskStackDepth;
  config.state_topic_name = DT7::kTopicName;
  config.thread_name = DT7::kDefaultThreadName;
  return config;
}

SuperCap::Config MakeSuperCapConfig() {
  SuperCap::Config config;
  config.can_name = "can1";
  return config;
}

// HeroCode 的温控 PID 输出范围是 0..2000，再映射成 0.0..1.0 占空比。
// LibXR PWM 接口直接使用占空比，因此这里保留同等控制强度。
// 目标温度对齐 ins_task.c 中的 RefTemp = 45。
constexpr float kBmi088HeatP = 1000.0f / 2000.0f;
constexpr float kBmi088HeatI = 20.0f / 2000.0f;
constexpr float kBmi088HeatOutputLimit = 1.0f;
constexpr float kBmi088TargetTemperatureC = 45.0f;

// IWDG 看门狗参数
// timeout_ms = 12000: 12s 超时，足够覆盖启动期对象构造 + 首周期 MonitorAll
//                     同时保证系统挂死后在可接受时间内复位
constexpr std::uint32_t kIWDGTimeoutMs = 12000;
constexpr std::uint32_t kHealthHeartbeatTimeoutMs = 100;

struct IwdgPrescalerOption {
  std::uint32_t reg_value;
  std::uint32_t divider;
};

bool ConfigureIwdg(IWDG_HandleTypeDef& handle, std::uint32_t timeout_ms) {
  static constexpr IwdgPrescalerOption kOptions[] = {
      {IWDG_PRESCALER_4, 4},     {IWDG_PRESCALER_8, 8},
      {IWDG_PRESCALER_16, 16},   {IWDG_PRESCALER_32, 32},
      {IWDG_PRESCALER_64, 64},   {IWDG_PRESCALER_128, 128},
      {IWDG_PRESCALER_256, 256},
  };

  if (timeout_ms == 0u) {
    return false;
  }

  for (const auto& option : kOptions) {
    std::uint32_t reload =
        (timeout_ms * LSI_VALUE) / (1000u * option.divider);
    if (reload == 0u) {
      reload = 1u;
    }
    if (reload > 1u) {
      --reload;
    }
    if (reload <= 0x0FFFu) {
      handle.Instance = IWDG;
      handle.Init.Prescaler = option.reg_value;
      handle.Init.Reload = reload;
      return HAL_IWDG_Init(&handle) == HAL_OK;
    }
  }

  return false;
}

}  // namespace

namespace App {

AppRuntime* g_app_runtime = nullptr;

AppRuntime::AppRuntime(LibXR::HardwareContainer &hardware,
                       LibXR::ApplicationManager &application_manager)
    : // --- Topic 基础设施 ---
      topic_domain_(AppConfig::kTopicDomain),
      robot_mode_topic_(LibXR::Topic::CreateTopic<RobotMode>(
          AppConfig::kRobotModeTopicName, &topic_domain_, false, true, true)),
      chassis_state_topic_(LibXR::Topic::CreateTopic<ChassisState>(
          AppConfig::kChassisStateTopicName, &topic_domain_, false, true,
          true)),
      gimbal_state_topic_(LibXR::Topic::CreateTopic<GimbalState>(
          AppConfig::kGimbalStateTopicName, &topic_domain_, false, true,
          true)),
      shoot_state_topic_(LibXR::Topic::CreateTopic<ShootState>(
          AppConfig::kShootStateTopicName, &topic_domain_, false, true, true)),
      ins_state_topic_(LibXR::Topic::CreateTopic<InsState>(
          AppConfig::kInsStateTopicName, &topic_domain_, false, true, true)),
      system_health_topic_(LibXR::Topic::CreateTopic<SystemHealth>(
          AppConfig::kSystemHealthTopicName, &topic_domain_, false, true,
          true)),
      // --- 传感器模块 ---
      // BMI088 采样率和量程与 HeroCode 的 BMI088driver.c 保持一致，
      // 保证陀螺仪零偏校准和 IMU 原始输入特性对齐。
      bmi088_(hardware, application_manager,
              BMI088::GyroFreq::GYRO_2000HZ_BW230HZ,
              BMI088::AcclFreq::ACCL_800HZ,
              BMI088::GyroRange::DEG_2000DPS,
              BMI088::AcclRange::ACCL_6G,
              LibXR::Quaternion<float>(1.0f, 0.0f, 0.0f, 0.0f),
              LibXR::PID<float>::Param{
                  .k = 1.0f,
                  .p = kBmi088HeatP,
                  .i = kBmi088HeatI,
                  .d = 0.0f,
                  .i_limit = 300.0f,
                  .out_limit = kBmi088HeatOutputLimit,
                  .cycle = false,
              },
              AppConfig::kBmi088GyroTopicName,
              AppConfig::kBmi088AcclTopicName, kBmi088TargetTemperatureC, 2048),
      ins_(application_manager, AppConfig::kBmi088GyroTopicName,
           AppConfig::kBmi088AcclTopicName, AppConfig::kInsStateTopicName,
           &topic_domain_, 2048),
      referee_(hardware, application_manager, Config::kRefereeConfig),
      master_machine_(hardware, application_manager,
                      Config::kMasterMachineConfig),
      // --- 输入设备 ---
      primary_remote_control_(
          hardware, application_manager, MakePrimaryRemoteConfig()),
      vt13_(hardware, application_manager, VT13::Config{}),
      // --- 通信桥接 ---
      can_bridge_(hardware, application_manager, Config::kCANBridgeConfig),
      super_cap_(hardware, application_manager,
                 MakeSuperCapConfig()),
      state_topic_client_(hardware, application_manager,
                          Config::kSharedTopicClientConfig.uart_name,
                           Config::kSharedTopicClientConfig.task_stack_depth,
                           Config::kSharedTopicClientConfig.buffer_size,
                           {
                               SharedTopicClient::TopicConfig{
                                   AppConfig::kRobotModeTopicName,
                                   AppConfig::kTopicDomain},
                               SharedTopicClient::TopicConfig{
                                   AppConfig::kChassisStateTopicName,
                                   AppConfig::kTopicDomain},
                               SharedTopicClient::TopicConfig{
                                   AppConfig::kGimbalStateTopicName,
                                   AppConfig::kTopicDomain},
                               SharedTopicClient::TopicConfig{
                                   AppConfig::kShootStateTopicName,
                                   AppConfig::kTopicDomain},
                               SharedTopicClient::TopicConfig{
                                   AppConfig::kInsStateTopicName,
                                   AppConfig::kTopicDomain},
                               SharedTopicClient::TopicConfig{
                                   AppConfig::kSystemHealthTopicName,
                                   AppConfig::kTopicDomain},
                           }),
      // --- 云台电机 ---
      gimbal_motor_group_(hardware, application_manager,
                          Config::kGimbalMotorGroupConfig),
      yaw_motor_(gimbal_motor_group_, Config::kYawMotorConfig),
      pitch_motor_(hardware, application_manager, Config::kPitchDMMotorConfig),
      // --- 底盘电机 ---
      chassis_motor_group_(hardware, application_manager,
                           Config::kChassisMotorGroupConfig),
      chassis_front_left_motor_(chassis_motor_group_,
                                Config::kChassisWheelMotorConfigs[0]),
      chassis_front_right_motor_(chassis_motor_group_,
                                 Config::kChassisWheelMotorConfigs[1]),
      chassis_rear_left_motor_(chassis_motor_group_,
                               Config::kChassisWheelMotorConfigs[2]),
      chassis_rear_right_motor_(chassis_motor_group_,
                                Config::kChassisWheelMotorConfigs[3]),
      // --- 发射电机 ---
      shoot_motor_group_(hardware, application_manager,
                         Config::kShootMotorGroupConfig),
      shoot_left_friction_motor_(shoot_motor_group_,
                                 Config::kShootLeftFrictionMotorConfig),
      shoot_right_friction_motor_(shoot_motor_group_,
                                  Config::kShootRightFrictionMotorConfig),
      shoot_loader_motor_(shoot_motor_group_, Config::kShootLoaderMotorConfig),
      // --- 控制器（后构造先执行）---
      master_machine_bridge_controller_(
          application_manager, master_machine_, referee_, robot_mode_topic_,
          gimbal_state_topic_, shoot_state_topic_, topic_domain_),
      shoot_controller_(application_manager, fire_command_,
                        robot_mode_topic_, shoot_state_topic_,
                        shoot_left_friction_motor_,
                        shoot_right_friction_motor_,
                        shoot_loader_motor_),
      chassis_controller_(application_manager, motion_command_,
                          robot_mode_topic_, gimbal_state_topic_,
                          chassis_state_topic_, topic_domain_,
                          chassis_front_left_motor_, chassis_front_right_motor_,
                          chassis_rear_left_motor_, chassis_rear_right_motor_,
                          super_cap_),
      gimbal_controller_(application_manager, aim_command_,
                         robot_mode_topic_, gimbal_state_topic_,
                         topic_domain_, yaw_motor_, pitch_motor_),
      decision_controller_(application_manager, operator_input_,
                           motion_command_, aim_command_, fire_command_,
                           robot_mode_topic_, system_health_topic_, referee_),
      health_controller_(application_manager, system_health_topic_,
                         chassis_state_topic_, gimbal_state_topic_,
                         shoot_state_topic_, ins_state_topic_,
                         robot_mode_topic_,
                         primary_remote_control_, vt13_, master_machine_,
                         referee_, super_cap_, can_bridge_),
      input_controller_(application_manager, operator_input_,
                        primary_remote_control_, vt13_,
                        master_machine_),
      iwdg_handle_{} {
  // IWDG 只检测主循环健康心跳，不承载业务急停策略。
  watchdog_ready_ = ConfigureIwdg(iwdg_handle_, kIWDGTimeoutMs);
  if (!watchdog_ready_) {
    XR_LOG_ERROR("IWDG init failed");
  }
}

bool AppRuntime::ShouldFeedWatchdog() const {
  SystemHealth health = health_controller_.LatestHealth();
  const std::uint32_t last_update_ms = health.last_update_ms;
  if (last_update_ms == 0u) {
    return false;
  }

  const std::uint32_t now_ms =
      static_cast<std::uint32_t>(LibXR::Timebase::GetMilliseconds());
  return (now_ms - last_update_ms) <= kHealthHeartbeatTimeoutMs;
}

void AppRuntime::FeedWatchdog() {
  if (!watchdog_ready_ || !ShouldFeedWatchdog()) {
    return;
  }

  const HAL_StatusTypeDef result = HAL_IWDG_Refresh(&iwdg_handle_);
  if (result != HAL_OK && !watchdog_feed_failure_reported_) {
    watchdog_feed_failure_reported_ = true;
    XR_LOG_ERROR("IWDG feed failed: %d", static_cast<int>(result));
  }
}

}  // namespace App
