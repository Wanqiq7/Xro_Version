#pragma once

#include "../Chassis/ChassisController.hpp"
#include "../Cmd/DecisionController.hpp"
#include "../Cmd/InputController.hpp"
#include "../Cmd/MasterMachineBridgeController.hpp"
#include "../Gimbal/GimbalController.hpp"
#include "../Health/HealthController.hpp"
#include "../Shoot/ShootController.hpp"
#include "../../Modules/BMI088/BMI088.hpp"
#include "../../Modules/CANBridge/CANBridge.hpp"
#include "../../Modules/DMMotor/DMMotor.hpp"
#include "../../Modules/DJIMotor/DJIMotor.hpp"
#include "../../Modules/INS/INS.hpp"
#include "../../Modules/MasterMachine/MasterMachine.hpp"
#include "../../Modules/DT7/DT7.hpp"
#include "../../Modules/Referee/Referee.hpp"
#include "../../Modules/SharedTopicClient/SharedTopicClient.hpp"
#include "../../Modules/VT13/VT13.hpp"
#include "../../Modules/SuperCap/SuperCap.hpp"
#include "../Cmd/OperatorInputSnapshot.hpp"
#include "../Topics/AimCommand.hpp"
#include "../Topics/FireCommand.hpp"
#include "../Topics/InsState.hpp"
#include "../Topics/MotionCommand.hpp"
#include "../Topics/SystemHealth.hpp"

namespace App {

class AppRuntime {
 public:
  AppRuntime(LibXR::HardwareContainer &hardware,
             LibXR::ApplicationManager &application_manager);

 private:
  // --- Topic 基础设施 ---
  LibXR::Topic::Domain topic_domain_;
  LibXR::Topic robot_mode_topic_;
  LibXR::Topic chassis_state_topic_;
  LibXR::Topic gimbal_state_topic_;
  LibXR::Topic shoot_state_topic_;
  LibXR::Topic ins_state_topic_;
  LibXR::Topic system_health_topic_;

  // --- 共享命令状态（Controller 间同周期共享，非 Topic）---
  OperatorInputSnapshot operator_input_{};
  MotionCommand motion_command_{};
  AimCommand aim_command_{};
  FireCommand fire_command_{};

  // --- 传感器模块 ---
  BMI088 bmi088_;
  INS ins_;
  Referee referee_;
  MasterMachine master_machine_;

  // --- 输入设备 ---
  DT7 primary_remote_control_;
  VT13 vt13_;

  // --- 通信桥接 ---
  CANBridge can_bridge_;
  SuperCap super_cap_;
  SharedTopicClient state_topic_client_;

  // --- 云台电机 ---
  DJIMotorGroup gimbal_motor_group_;
  DJIMotor yaw_motor_;
  DMMotor pitch_motor_;

  // --- 底盘电机 ---
  DJIMotorGroup chassis_motor_group_;
  DJIMotor chassis_front_left_motor_;
  DJIMotor chassis_front_right_motor_;
  DJIMotor chassis_rear_left_motor_;
  DJIMotor chassis_rear_right_motor_;

  // --- 发射电机 ---
  DJIMotorGroup shoot_motor_group_;
  DJIMotor shoot_left_friction_motor_;
  DJIMotor shoot_right_friction_motor_;
  DJIMotor shoot_loader_motor_;

  // --- 控制器（LockFreeList 头插：后构造先执行）---
  MasterMachineBridgeController master_machine_bridge_controller_; // 执行顺序 7
  ShootController shoot_controller_;                                // 执行顺序 6
  ChassisController chassis_controller_;                            // 执行顺序 5
  GimbalController gimbal_controller_;                              // 执行顺序 4
  DecisionController decision_controller_;                          // 执行顺序 3
  HealthController health_controller_;                              // 执行顺序 2
  InputController input_controller_;                                // 执行顺序 1
};

// Ozone 直接调试入口：指向真实运行中的 AppRuntime，不复制任何状态。
extern AppRuntime* g_app_runtime;

}  // namespace App
