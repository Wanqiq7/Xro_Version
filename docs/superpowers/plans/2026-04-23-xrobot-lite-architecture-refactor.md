# XRobot-Lite Architecture Refactor Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不破坏当前唯一运行链与现有行为闭环的前提下，把 `App/` 收口为 XRobot-Lite 形态，并为后续 Controller 合并与 Topic 瘦身建立稳定边界。

**Architecture:** 先做“结构拆义，不改行为”的阶段一，把当前 `Integration/` 与 `Runtime/` 的混合职责拆开，固定 `Assembly + Robot + Subsystems + Runtime + Topics + Config + Dev` 的长期骨架。之后再做 `Role -> Controller` 收口和 `8 -> 5` Topic 白名单瘦身，但这些语义变化不与目录重构混在同一批次里。

**Tech Stack:** STM32CubeMX, FreeRTOS, LibXR, XRobot, CMake, C++

---

## Phase Overview

### Phase 1: 结构拆义，不改行为

**Boundary**
- 保持唯一运行链不变：`StartDefaultTask -> app_main -> XRobotMain -> RunXRobotAssembly -> MonitorAll`
- 保持 6 Role 不变
- 保持 8 Topic 不变
- 保持所有现有 Topic 名称、payload、SharedTopicClient 桥接列表不变
- 允许文件迁移、目录重命名、include 路径调整、CMake 收口

**Deliverables**
- 新建并接入以下目录：
  - `App/Assembly/`
  - `App/Robot/`
  - `App/Subsystems/`
  - `App/Runtime/`
  - `App/Topics/`
  - `App/Config/`
  - `App/Dev/CompileTests/`
- 将现有文件按下列规则迁移：
  - `App/Integration/XRobotAssembly.hpp` -> `App/Assembly/XRobotAssembly.hpp`
  - `App/Integration/AppRuntimeAssemblyHelper.hpp` -> `App/Assembly/AppRuntimeAssemblyHelper.hpp`
  - `App/Runtime/AppRuntime.*` -> `App/Robot/AppRuntime.*`
  - `App/Runtime/RuntimeContext.hpp` -> `App/Robot/RuntimeContext.hpp`
  - `App/Integration/*AxisBridge*` 与 `*MotorBridge*` -> `App/Subsystems/`
  - `App/Runtime/CANBridgeHealthView.hpp` -> `App/Subsystems/CANBridgeHealthView.hpp`
  - `App/Runtime/MasterMachineCoordinationView.hpp` -> `App/Subsystems/MasterMachineCoordinationView.hpp`
  - `App/Runtime/RefereeConstraintView.hpp` -> `App/Subsystems/RefereeConstraintView.hpp`
  - `App/Runtime/OperatorInputSnapshot.hpp`、`InputMappingHelpers.hpp`、`DegradedPolicy.hpp`、`HealthAggregator.hpp`、`ChassisStateEstimator.hpp`、`ChassisControlHelpers.hpp`、`ShootControlHelpers.hpp` 保留在 `App/Runtime/`
  - 所有 `App/Runtime/*_compile_test.cpp` -> `App/Dev/CompileTests/`
- 更新 `App/CMakeLists.txt` 与所有 include 引用
- 把 `Core/Src/freertos.c` 中 `app_main()` 之后不可达的 `for(;;)` 收口为“不可达处理”表达，避免第二主循环歧义

**Verification**
- `cmake --preset Debug`
- `cmake --build --preset Debug`
- 验证 `RunXRobotAssembly()` 仍是唯一 `MonitorAll()` 主循环
- 确认 `SharedTopicClient` 桥接列表仍为 `robot_mode/chassis_state/gimbal_state/shoot_state/system_health`

### Phase 2: Role 收口为 Controller

**Boundary**
- 不改变执行闭环的实际算法
- 不改变模块实例化顺序
- 允许重命名类、移动文件、调整装配关系

**Deliverables**
- 新建：
  - `App/Robot/ControllerBase.hpp/.cpp`
  - `App/Robot/RobotController.hpp/.cpp`
  - `App/Subsystems/ChassisController.hpp/.cpp`
  - `App/Subsystems/GimbalController.hpp/.cpp`
  - `App/Subsystems/ShootController.hpp/.cpp`
- 收口映射：
  - `InputRole + DecisionRole + TelemetryRole` -> `RobotController`
  - `ChassisRole` -> `ChassisController`
  - `GimbalRole` -> `GimbalController`
  - `ShootRole` -> `ShootController`
- `RoleBase` 改名为 `ControllerBase`，或在本阶段先保留类型名、仅更换文件语义
- `Yaw/Pitch bridge` 继续保留为 `GimbalController` 的私有依赖，不并回控制器实现

**Verification**
- `cmake --build --preset Debug`
- 验证输入链仍能产生命令
- 验证三个执行端仍能发布状态
- 验证 `system_health` 仍由总控侧聚合

### Phase 3: Topic 白名单从 8 缩到 5

**Boundary**
- 对外桥接契约不变
- `robot_mode/chassis_state/gimbal_state/shoot_state/system_health` 必须保留
- `motion_command/aim_command/fire_command` 从公共 Topic 收回到 `RuntimeContext` 内部状态

**Deliverables**
- 删除 `RuntimeContext` 中 3 个命令 Topic 的创建与访问器
- 在 `RuntimeContext` 中新增内部命令状态槽：
  - `motion_command_`
  - `aim_command_`
  - `fire_command_`
- `RobotController` 直接写内部命令状态
- 三个 subsystem controller 直接从上下文读取内部命令状态

**Risks**
- `ApplicationManager` 遍历顺序会影响“同周期可见性”
- 需要明确 RobotController 必须在 subsystem controllers 之前执行，或接受一周期延迟并显式记录

**Verification**
- 命令链不丢失
- `SharedTopicClient` 桥接未受影响
- `system_health` 降级闭环仍可工作

### Phase 4: 约束流与健康流再收口

**Boundary**
- 不把业务控制塞进 Modules
- 不把约束判断散落回各 controller

**Deliverables**
- 统一 `HealthAggregator`、`RefereeConstraintView`、`MasterMachineCoordinationView`、`CANBridgeHealthView`
- 固化 RobotController 的三段式主循环：
  - `UpdateInput()`
  - `ComputeCommands()`
  - `ComputeAndPublishHealth()`
- 确保 `Subsystems` 只做子系统编排，不承担全局决策

**Verification**
- 断联/禁发/功率限制/主控接管路径仍可解释
- `system_health` 仍是唯一全局健康出口

### Phase 5: 清理迁移脚手架并完成最终收口

**Boundary**
- 不再保留仅服务于旧目录结构的兼容 include
- 删除过渡命名和空壳目录

**Deliverables**
- 删除空的 `App/Integration/`
- 删除空的旧 `App/Runtime/` 残留结构（若已拆空）
- 整理 `App/CMakeLists.txt`
- 更新必要文档说明（若本阶段需要）

**Verification**
- `cmake --preset Debug`
- `cmake --build --preset Debug`
- （建议）`cmake --preset Release`
- （建议）`cmake --build --preset Release`

---

## Task 1: 执行 Phase 1 的目录拆义

**Files:**
- Modify: `App/CMakeLists.txt`
- Modify: `Core/Src/freertos.c`
- Modify: `User/xrobot_main.hpp`
- Move: `App/Integration/XRobotAssembly.hpp`
- Move: `App/Integration/AppRuntimeAssemblyHelper.hpp`
- Move: `App/Runtime/AppRuntime.hpp`
- Move: `App/Runtime/AppRuntime.cpp`
- Move: `App/Runtime/RuntimeContext.hpp`
- Move: `App/Integration/AxisBridgeCommon.hpp`
- Move: `App/Integration/YawAxisBridge.hpp`
- Move: `App/Integration/PitchAxisBridge.hpp`
- Move: `App/Integration/YawDJIMotorBridge.hpp`
- Move: `App/Integration/YawDJIMotorBridge.cpp`
- Move: `App/Integration/PitchDMMotorBridge.hpp`
- Move: `App/Integration/PitchDMMotorBridge.cpp`
- Move: `App/Runtime/CANBridgeHealthView.hpp`
- Move: `App/Runtime/MasterMachineCoordinationView.hpp`
- Move: `App/Runtime/RefereeConstraintView.hpp`
- Move: `App/Runtime/*_compile_test.cpp`

- [ ] **Step 1: 创建目标目录骨架**
- [ ] **Step 2: 迁移 Assembly/Robot/Subsystems/Dev 文件**
- [ ] **Step 3: 更新 include 路径与 `App/CMakeLists.txt`**
- [ ] **Step 4: 收口 `freertos.c` 中不可达的第二主循环表达**
- [ ] **Step 5: 运行 `cmake --preset Debug`**
- [ ] **Step 6: 运行 `cmake --build --preset Debug`**

## Task 2: 执行 Phase 2 的 Controller 收口

**Files:**
- Create: `App/Robot/ControllerBase.hpp`
- Create: `App/Robot/ControllerBase.cpp`
- Create: `App/Robot/RobotController.hpp`
- Create: `App/Robot/RobotController.cpp`
- Create: `App/Subsystems/ChassisController.hpp`
- Create: `App/Subsystems/ChassisController.cpp`
- Create: `App/Subsystems/GimbalController.hpp`
- Create: `App/Subsystems/GimbalController.cpp`
- Create: `App/Subsystems/ShootController.hpp`
- Create: `App/Subsystems/ShootController.cpp`
- Modify: current Role implementations

- [ ] **Step 1: 建立 Controller 文件骨架**
- [ ] **Step 2: 先机械搬运 Input/Decision/Telemetry 到 RobotController**
- [ ] **Step 3: 先机械搬运三个执行 Role 到三个 subsystem controllers**
- [ ] **Step 4: 调整 AppRuntime 装配**
- [ ] **Step 5: 编译回归**

## Task 3: 执行 Phase 3 的 Topic 白名单瘦身

**Files:**
- Modify: `App/Robot/RuntimeContext.hpp`
- Modify: `App/Robot/RobotController.cpp`
- Modify: `App/Subsystems/ChassisController.cpp`
- Modify: `App/Subsystems/GimbalController.cpp`
- Modify: `App/Subsystems/ShootController.cpp`

- [ ] **Step 1: 将 3 个命令 Topic 收回内部状态**
- [ ] **Step 2: 改写发布/订阅为上下文读写**
- [ ] **Step 3: 保持 5 个公共 Topic 不变**
- [ ] **Step 4: 编译与行为回归**

## Task 4: 执行 Phase 4 的约束与健康流再收口

**Files:**
- Modify: `App/Robot/RobotController.cpp`
- Modify: `App/Runtime/HealthAggregator.hpp`
- Modify: `App/Subsystems/RefereeConstraintView.hpp`
- Modify: `App/Subsystems/MasterMachineCoordinationView.hpp`
- Modify: `App/Subsystems/CANBridgeHealthView.hpp`

- [ ] **Step 1: 固化 RobotController 三段式主循环**
- [ ] **Step 2: 收口健康与约束视图**
- [ ] **Step 3: 编译回归**

## Task 5: 执行 Phase 5 的清理与最终收口

**Files:**
- Modify: `App/CMakeLists.txt`
- Delete: obsolete compatibility headers/directories as applicable

- [ ] **Step 1: 删除空壳目录与过渡 include**
- [ ] **Step 2: 清理 CMake 路径**
- [ ] **Step 3: Debug/Release 双构建**

