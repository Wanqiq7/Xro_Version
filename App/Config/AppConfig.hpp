#pragma once

namespace App {

// 应用层 Topic 域与名称常量。
struct AppConfig {
  static constexpr const char* kTopicDomain = "app";

  static constexpr const char* kRobotModeTopicName = "robot_mode";
  static constexpr const char* kChassisStateTopicName = "chassis_state";
  static constexpr const char* kGimbalStateTopicName = "gimbal_state";
  static constexpr const char* kShootStateTopicName = "shoot_state";

  // 模块私有 Topic 名称集中在这里，避免 App 控制器散落硬编码契约。
  static constexpr const char* kBmi088GyroTopicName = "bmi088_gyro";
  static constexpr const char* kBmi088AcclTopicName = "bmi088_accl";
  static constexpr const char* kRefereeStateTopicName = "referee_state";
};

}  // namespace App
