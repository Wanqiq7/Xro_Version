# XRobot-Lite 架构说明

## 1. 目标

当前仓库已经从“`User/` 直接实例化若干模块”的模板状态，收口为 **XRobot-Lite** 形态。  
该形态的目标是：

- 保持外层结构为 `bsp -> module -> app`
- 保持唯一运行链
- 保持 `Modules/` 只承载通用能力，不承载整车业务决策
- 让 `App/` 内部结构更扁平、更接近 donor 的可读性，但不回退到 donor 那种“业务直接揉进 module / app 直碰总线”的模式

## 2. 唯一运行链

当前唯一运行链为：

`StartDefaultTask -> app_main() -> XRobotMain(peripherals) -> App::RunXRobotAssembly(hw) -> ApplicationManager::MonitorAll()`

对应位置：

- [freertos.c](/F:/Xrobot/Xro_template/Core/Src/freertos.c)
- [app_main.cpp](/F:/Xrobot/Xro_template/User/app_main.cpp)
- [xrobot_main.hpp](/F:/Xrobot/Xro_template/User/xrobot_main.hpp)
- [XRobotAssembly.hpp](/F:/Xrobot/Xro_template/App/Assembly/XRobotAssembly.hpp)

约束：

- 不允许第二个无限主循环
- 不允许第二套调度中心
- `User/` 只做板级封装与 `HardwareContainer` 注入

## 3. 目录结构

当前 `App/` 采用如下结构：

```text
App/
  Assembly/
  Config/
  Dev/
    CompileTests/
  Robot/
  Roles/
  Runtime/
  Subsystems/
  Topics/
```

### 3.1 `Assembly/`

职责：装配根（Composition Root）。

当前文件：

- [XRobotAssembly.hpp](/F:/Xrobot/Xro_template/App/Assembly/XRobotAssembly.hpp)
- [AppRuntimeAssemblyHelper.hpp](/F:/Xrobot/Xro_template/App/Assembly/AppRuntimeAssemblyHelper.hpp)

说明：

- `XRobotAssembly` 持有唯一 `ApplicationManager` 和唯一 `AppRuntime`
- `AppRuntimeAssemblyHelper` 负责装配时的配置工厂与可选模块装配细节

### 3.2 `Robot/`

职责：总控与运行时骨架。

当前文件：

- [AppRuntime.hpp](/F:/Xrobot/Xro_template/App/Robot/AppRuntime.hpp)
- [AppRuntime.cpp](/F:/Xrobot/Xro_template/App/Robot/AppRuntime.cpp)
- [RuntimeContext.hpp](/F:/Xrobot/Xro_template/App/Robot/RuntimeContext.hpp)
- [ControllerBase.hpp](/F:/Xrobot/Xro_template/App/Robot/ControllerBase.hpp)
- [ControllerBase.cpp](/F:/Xrobot/Xro_template/App/Robot/ControllerBase.cpp)
- [RobotController.hpp](/F:/Xrobot/Xro_template/App/Robot/RobotController.hpp)
- [RobotController.cpp](/F:/Xrobot/Xro_template/App/Robot/RobotController.cpp)

说明：

- `AppRuntime` 是运行时对象宿主，持有所有长期存活的模块、bridge、controller、包装层
- `RuntimeContext` 提供共享上下文、5 个公共 Topic、3 个内部命令状态以及 `OperatorInputSnapshot`
- `ControllerBase` 是所有 controller / 包装层的统一基类
- `RobotController` 是总控控制器，统一承载输入、决策、健康三段逻辑

### 3.3 `Subsystems/`

职责：执行子系统控制器与适配器。

当前文件包括两类：

1. 子系统控制器

- [ChassisController.hpp](/F:/Xrobot/Xro_template/App/Subsystems/ChassisController.hpp)
- [ChassisController.cpp](/F:/Xrobot/Xro_template/App/Subsystems/ChassisController.cpp)
- [GimbalController.hpp](/F:/Xrobot/Xro_template/App/Subsystems/GimbalController.hpp)
- [GimbalController.cpp](/F:/Xrobot/Xro_template/App/Subsystems/GimbalController.cpp)
- [ShootController.hpp](/F:/Xrobot/Xro_template/App/Subsystems/ShootController.hpp)
- [ShootController.cpp](/F:/Xrobot/Xro_template/App/Subsystems/ShootController.cpp)

2. 适配器 / 约束视图

- [AxisBridgeCommon.hpp](/F:/Xrobot/Xro_template/App/Subsystems/AxisBridgeCommon.hpp)
- [YawAxisBridge.hpp](/F:/Xrobot/Xro_template/App/Subsystems/YawAxisBridge.hpp)
- [PitchAxisBridge.hpp](/F:/Xrobot/Xro_template/App/Subsystems/PitchAxisBridge.hpp)
- [YawDJIMotorBridge.hpp](/F:/Xrobot/Xro_template/App/Subsystems/YawDJIMotorBridge.hpp)
- [YawDJIMotorBridge.cpp](/F:/Xrobot/Xro_template/App/Subsystems/YawDJIMotorBridge.cpp)
- [PitchDMMotorBridge.hpp](/F:/Xrobot/Xro_template/App/Subsystems/PitchDMMotorBridge.hpp)
- [PitchDMMotorBridge.cpp](/F:/Xrobot/Xro_template/App/Subsystems/PitchDMMotorBridge.cpp)
- [CANBridgeHealthView.hpp](/F:/Xrobot/Xro_template/App/Subsystems/CANBridgeHealthView.hpp)
- [MasterMachineCoordinationView.hpp](/F:/Xrobot/Xro_template/App/Subsystems/MasterMachineCoordinationView.hpp)
- [RefereeConstraintView.hpp](/F:/Xrobot/Xro_template/App/Subsystems/RefereeConstraintView.hpp)

说明：

- `Chassis/Gimbal/ShootController` 已经分别接管旧 `Role` 的真实控制逻辑
- `Yaw/Pitch bridge` 继续作为 Gimbal 子系统的私有依赖存在
- 各种 `*View` 用于把模块状态折叠为 App 层稳定语义

### 3.4 `Roles/`

职责：**仅保留调度入口包装层**。

当前文件：

- [InputRole.hpp](/F:/Xrobot/Xro_template/App/Roles/InputRole.hpp)
- [InputRole.cpp](/F:/Xrobot/Xro_template/App/Roles/InputRole.cpp)
- [DecisionRole.hpp](/F:/Xrobot/Xro_template/App/Roles/DecisionRole.hpp)
- [DecisionRole.cpp](/F:/Xrobot/Xro_template/App/Roles/DecisionRole.cpp)
- [ChassisRole.hpp](/F:/Xrobot/Xro_template/App/Roles/ChassisRole.hpp)
- [ChassisRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ChassisRole.cpp)
- [GimbalRole.hpp](/F:/Xrobot/Xro_template/App/Roles/GimbalRole.hpp)
- [GimbalRole.cpp](/F:/Xrobot/Xro_template/App/Roles/GimbalRole.cpp)
- [ShootRole.hpp](/F:/Xrobot/Xro_template/App/Roles/ShootRole.hpp)
- [ShootRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ShootRole.cpp)
- [TelemetryRole.hpp](/F:/Xrobot/Xro_template/App/Roles/TelemetryRole.hpp)
- [TelemetryRole.cpp](/F:/Xrobot/Xro_template/App/Roles/TelemetryRole.cpp)

说明：

- 这 6 个类已不承载真实业务逻辑
- 它们现在只负责注册进 `ApplicationManager`
- 每个包装层只把 `OnMonitor()` 转发给对应 controller

包装映射：

- `InputRole` -> `RobotController::UpdateInput()`
- `DecisionRole` -> `RobotController::ComputeCommands()`
- `TelemetryRole` -> `RobotController::ComputeAndPublishHealth()`
- `ChassisRole` -> `ChassisController::OnMonitor()`
- `GimbalRole` -> `GimbalController::OnMonitor()`
- `ShootRole` -> `ShootController::OnMonitor()`

### 3.5 `Runtime/`

职责：helper / policy / estimator / aggregator。

当前文件：

- [OperatorInputSnapshot.hpp](/F:/Xrobot/Xro_template/App/Runtime/OperatorInputSnapshot.hpp)
- [InputMappingHelpers.hpp](/F:/Xrobot/Xro_template/App/Runtime/InputMappingHelpers.hpp)
- [DegradedPolicy.hpp](/F:/Xrobot/Xro_template/App/Runtime/DegradedPolicy.hpp)
- [HealthAggregator.hpp](/F:/Xrobot/Xro_template/App/Runtime/HealthAggregator.hpp)
- [ChassisStateEstimator.hpp](/F:/Xrobot/Xro_template/App/Runtime/ChassisStateEstimator.hpp)
- [ChassisControlHelpers.hpp](/F:/Xrobot/Xro_template/App/Runtime/ChassisControlHelpers.hpp)
- [ShootControlHelpers.hpp](/F:/Xrobot/Xro_template/App/Runtime/ShootControlHelpers.hpp)

说明：

- `Runtime/` 只承载纯 helper 和小型策略对象
- 控制器可以调用这些 helper，但不应把这些 helper 反向注册成应用

### 3.6 `Topics/`

职责：公共契约。

当前保留的公共 Topic 为 5 个：

- [RobotMode.hpp](/F:/Xrobot/Xro_template/App/Topics/RobotMode.hpp)
- [ChassisState.hpp](/F:/Xrobot/Xro_template/App/Topics/ChassisState.hpp)
- [GimbalState.hpp](/F:/Xrobot/Xro_template/App/Topics/GimbalState.hpp)
- [ShootState.hpp](/F:/Xrobot/Xro_template/App/Topics/ShootState.hpp)
- [SystemHealth.hpp](/F:/Xrobot/Xro_template/App/Topics/SystemHealth.hpp)

不再作为公共 Topic 的 3 个命令对象：

- [MotionCommand.hpp](/F:/Xrobot/Xro_template/App/Topics/MotionCommand.hpp)
- [AimCommand.hpp](/F:/Xrobot/Xro_template/App/Topics/AimCommand.hpp)
- [FireCommand.hpp](/F:/Xrobot/Xro_template/App/Topics/FireCommand.hpp)

说明：

- 这 3 个命令类型仍然存在，但只作为 `RuntimeContext` 内部状态使用
- 不再通过公共 Topic 广播

### 3.7 `Config/`

职责：配置与固定参数。

当前文件：

- [ActuatorConfig.hpp](/F:/Xrobot/Xro_template/App/Config/ActuatorConfig.hpp)
- [AppConfig.hpp](/F:/Xrobot/Xro_template/App/Config/AppConfig.hpp)
- [BridgeConfig.hpp](/F:/Xrobot/Xro_template/App/Config/BridgeConfig.hpp)
- [DegradedModeConfig.hpp](/F:/Xrobot/Xro_template/App/Config/DegradedModeConfig.hpp)
- [HealthPolicyConfig.hpp](/F:/Xrobot/Xro_template/App/Config/HealthPolicyConfig.hpp)
- [RefereeConfig.hpp](/F:/Xrobot/Xro_template/App/Config/RefereeConfig.hpp)

### 3.8 `Dev/CompileTests/`

职责：编译期契约冻结。

说明：

- 当前所有迁移期和结构期的 compile-only 测试都集中在这里
- 它们不承载运行时行为，只负责锁定签名、依赖形状和阶段边界

## 4. 控制权流向

当前运行时的控制权流向是：

1. `InputRole` 调用 `RobotController::UpdateInput()`
2. `DecisionRole` 调用 `RobotController::ComputeCommands()`
3. `GimbalRole` 调用 `GimbalController::OnMonitor()`
4. `ChassisRole` 调用 `ChassisController::OnMonitor()`
5. `ShootRole` 调用 `ShootController::OnMonitor()`
6. `TelemetryRole` 调用 `RobotController::ComputeAndPublishHealth()`

## 5. 调度顺序

当前调度顺序不是按成员声明顺序，而是由 `ApplicationManager` 的注册顺序决定。  
`ApplicationManager` 通过 `LockFreeList` 头插注册，因此 **后注册的应用先执行**。

为保证当前的目标执行顺序：

`Input -> Decision -> Gimbal -> Chassis -> Shoot -> Telemetry`

`AppRuntime` 中包装层成员按逆序声明：

- `TelemetryRole`
- `ShootRole`
- `ChassisRole`
- `GimbalRole`
- `DecisionRole`
- `InputRole`

这样在构造时后声明者后构造，最终先执行。

## 6. 健康流与约束流

当前健康流和约束流统一收口在 `RobotController`：

- `ComputeCommands()`
  - 使用 `CloseDecisionStage()`
  - 收口 `DegradedPolicyResult`
  - 收口 `RefereeConstraintView`

- `ComputeAndPublishHealth()`
  - 使用 `CloseHealthStage()`
  - 收口 `MasterMachineCoordinationView`
  - 收口 `CANBridgeHealthView`
  - 收口 `RefereeConstraintView`
  - 收口 `HealthAggregator::HealthFlowContext`

这意味着：

- 约束流不再在多个 controller 里零散拼接
- 健康流不再在 `RobotController` 里手工拼装字段

## 7. 当前仍保留的历史兼容物

当前还保留但已“降格”的内容：

- `Roles/` 目录
  - 仍然存在
  - 但已不再承载真实业务逻辑
- `MotionCommand / AimCommand / FireCommand` 类型
  - 仍然存在
  - 但不再作为公共 Topic

## 8. 当前未解决的既有问题

- [BMI088.hpp](/F:/Xrobot/Xro_template/Modules/BMI088/BMI088.hpp)
  - `GetGyroLSB()`
  - `GetAcclLSB()`
  - 仍存在“control reaches end of non-void function” warning

## 9. 架构边界总结

一句话总结当前架构：

**`User/` 只注入，`Assembly/` 只装配，`Robot/` 只总控，`Subsystems/` 只编排执行，`Runtime/` 只放 helper，`Topics/` 只放公共契约，`Roles/` 只保留调度包装。**
