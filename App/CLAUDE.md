[根目录](../CLAUDE.md) > **App**

# App -- 机器人应用层

## 变更记录 (Changelog)

| 日期 | 操作 | 说明 |
|------|------|------|
| 2026-04-26 | 修正 | 对齐当前真实目录、入口链路、Topic 与共享命令对象边界 |
| 2026-04-23 | 重构 | 精简架构：去除 Roles/Subsystems/Runtime 层，控制器直接注册 |
| 2026-04-23 | 新建 | 首次扫描，全量分析 |

---

## 模块职责

`App/` 是机器人应用编排层，负责把可复用 `Modules/` 能力组织成整车控制链路。当前采用 **控制器直接注册** 架构：`InputController`、`DecisionController`、`GimbalController`、`ChassisController`、`ShootController` 都继承 `ControllerBase`，构造时注册到 `LibXR::ApplicationManager`，由 `ApplicationManager::MonitorAll()` 周期调度。

边界约束：
- `Modules/` 提供传感器、执行器、通信和协议适配等通用能力。
- `App/` 负责输入汇聚、模式决策、控制命令生成、执行器编排和状态发布。
- `User/` 负责板级入口、硬件封装和向 `AppRuntime` 注入 `HardwareContainer`，不承载长期业务逻辑。

---

## 入口与启动

启动链路：

```text
FreeRTOS defaultTask -> app_main() -> XRobotMain(peripherals)
```

`User/xrobot_main.hpp` 是当前 App 入口。它创建静态 `LibXR::ApplicationManager` 和静态 `App::AppRuntime`：

```cpp
static LibXR::ApplicationManager application_manager;
static App::AppRuntime runtime(hw, application_manager);
```

随后进入唯一主循环：

```cpp
while (true) {
  application_manager.MonitorAll();
  LibXR::Thread::Sleep(1);
}
```

`App::AppRuntime` 在构造函数中完成 Topic、模块、电机和控制器装配，当前入口直接由 `User/xrobot_main.hpp` 承担。

---

## 数据流

当前数据流分为三类：模块私有 Topic、App 公共 Topic、AppRuntime 内共享命令对象。

```text
模块私有 Topic
  DT7State / VT13State / MasterMachineState
        |
        v
InputController
        |
        v
OperatorInputSnapshot（AppRuntime 内共享对象，非 Topic）
        |
        v
DecisionController
        |
        +--> 发布 RobotMode（App 公共 Topic）
        |
        +--> 写入 MotionCommand / AimCommand / FireCommand
             （AppRuntime 内共享命令对象，非 Topic）
                   |
                   +--> ChassisController 消费 MotionCommand
                   +--> GimbalController 消费 AimCommand
                   +--> ShootController 消费 FireCommand

执行控制器同时订阅状态 Topic，并发布各自状态：
  ChassisController: 订阅 RobotMode / GimbalState / RefereeState / BMI088，发布 ChassisState
  GimbalController : 订阅 RobotMode / BMI088，发布 GimbalState
  ShootController  : 订阅 RobotMode / RefereeState，发布 ShootState
```

`MotionCommand`、`AimCommand`、`FireCommand` 的类型定义位于 `App/Topics/`，但当前不创建对应 Topic；它们只是 `AppRuntime` 内部持有并在控制器间按引用传递的共享命令对象。

---

## 目录结构与子模块

当前 `App/` 下真实目录为：

```text
App/
  Chassis/
  Cmd/
  Config/
  Gimbal/
  Robot/
  Shoot/
  Topics/
```

### Robot/ -- 运行时核心

- `AppRuntime.hpp/.cpp`: 核心装配类，持有公共 Topic、共享命令对象、模块实例、电机实例和控制器实例。
- `ControllerBase.hpp/.cpp`: 控制器公共基类，继承 `LibXR::Application` 并在构造时注册到 `ApplicationManager`。

### Cmd/ -- 输入与决策

- `InputController.hpp/.cpp`: 订阅 DT7、VT13、MasterMachine 的模块私有状态 Topic，选择当前输入源并写入 `OperatorInputSnapshot`。
- `DecisionController.hpp/.cpp`: 消费 `OperatorInputSnapshot` 和裁判状态，写入共享 `MotionCommand`、`AimCommand`、`FireCommand`，并发布 `RobotMode`。
- `OperatorInputSnapshot.hpp`: 操作员输入快照，作为 AppRuntime 内共享对象使用。
- `RefereeConstraintView.hpp`: 裁判系统约束视图，用于把裁判状态折叠成发射控制约束。

### Chassis/ -- 底盘控制

- `ChassisController.hpp/.cpp`: 消费共享 `MotionCommand`，订阅 `RobotMode`、`GimbalState`、裁判状态和 BMI088 数据，驱动 4 个 DJI 底盘电机并发布 `ChassisState`。

### Gimbal/ -- 云台控制

- `GimbalController.hpp/.cpp`: 消费共享 `AimCommand`，订阅 `RobotMode` 和 BMI088 数据，驱动 Yaw DJI 电机与 Pitch DM 电机并发布 `GimbalState`。

### Shoot/ -- 发射控制

- `ShootController.hpp/.cpp`: 消费共享 `FireCommand`，订阅 `RobotMode` 和裁判状态，驱动摩擦轮/拨盘 DJI 电机并发布 `ShootState`。

### Topics/ -- App 公共契约与共享命令类型

这些结构体保持轻量、尺寸稳定、POD-like，便于 Topic 传输或控制器间共享。

| 类型 | 当前用途 |
|------|----------|
| `RobotMode` | App 公共 Topic，描述整车模式、控制来源、运动/瞄准/发射使能 |
| `ChassisState` | App 公共 Topic，描述底盘反馈状态 |
| `GimbalState` | App 公共 Topic，描述云台反馈状态 |
| `ShootState` | App 公共 Topic，描述发射反馈状态 |
| `MotionCommand` | AppRuntime 内共享命令对象，当前不是 Topic |
| `AimCommand` | AppRuntime 内共享命令对象，当前不是 Topic |
| `FireCommand` | AppRuntime 内共享命令对象，当前不是 Topic |

### Config/ -- 集中配置

- `AppConfig.hpp`: App Topic 域名、公共 Topic 名称和部分模块私有 Topic 名称常量。
- `BridgeConfig.hpp`: 上位机、CANBridge、SharedTopicClient 等通信桥配置。
- `ChassisConfig.hpp`: 底盘电机组、轮组和控制参数。
- `GimbalConfig.hpp`: 云台 Yaw DJI 电机、Pitch DM 电机和控制参数。
- `RefereeConfig.hpp`: 裁判系统配置与保守默认值。
- `ShootConfig.hpp`: 发射电机组、摩擦轮、拨盘和控制参数。

---

## 控制器执行顺序

`ApplicationManager` 底层使用 `LockFreeList` 头插注册，控制器 **后构造先执行**。`AppRuntime` 当前声明顺序刻意与期望执行顺序相反：

```cpp
ShootController shoot_controller_;       // 第 5 执行
ChassisController chassis_controller_;   // 第 4 执行
GimbalController gimbal_controller_;     // 第 3 执行
DecisionController decision_controller_; // 第 2 执行
InputController input_controller_;       // 第 1 执行
```

因此单周期内的核心链路是：

```text
InputController -> DecisionController -> GimbalController -> ChassisController -> ShootController
```

修改 `AppRuntime` 成员声明顺序时，必须重新确认控制器注册顺序和数据依赖。

---

## 对外接口

App 层当前对外通过 Topic 暴露状态：

- **发布 App 公共 Topic**: `robot_mode`、`chassis_state`、`gimbal_state`、`shoot_state`
- **订阅模块私有 Topic**: DT7、VT13、MasterMachine、Referee、BMI088 等模块状态
- **内部共享对象**: `OperatorInputSnapshot`、`MotionCommand`、`AimCommand`、`FireCommand`

注意：`MotionCommand`、`AimCommand`、`FireCommand` 虽然位于 `App/Topics/`，但当前只是类型定义和共享命令对象，不是已发布的 Topic。

---

## 关键依赖

- **LibXR**: `ApplicationManager`、`Application`、`Topic`、`HardwareContainer`、`Thread`、`Timebase`
- **Eigen**: 云台和底盘中使用的 3D 向量类型
- **Modules**: `BMI088`、`CANBridge`、`DMMotor`、`DJIMotor`、`DT7`、`VT13`、`MasterMachine`、`Referee`、`SharedTopicClient`、`SuperCap`

---

## 常见问题 (FAQ)

**Q: 为什么 AppRuntime 中控制器声明顺序是反的？**  
A: `ApplicationManager` 使用 `LockFreeList` 头插注册，后构造的控制器先执行。因此 `ShootController` 声明在最前面但最后执行，`InputController` 声明在最后面但最先执行。

**Q: 为什么 command 类型放在 Topics 目录，却不创建 Topic？**  
A: `MotionCommand`、`AimCommand`、`FireCommand` 是跨控制器稳定契约，所以类型定义放在 `App/Topics/`；但当前数据只需要在同一个 `AppRuntime` 周期内共享，按引用传递比创建 Topic 更直接，也避免把内部控制命令暴露成外部通信面。

**Q: 如何新增一个控制器？**  
A: 新控制器应继承 `ControllerBase`，由 `AppRuntime` 持有并注入依赖；若它发布跨角色长期稳定状态，再在 `App/Topics/` 增加公共状态类型并由 `AppRuntime` 创建 Topic。新增后必须检查 `LockFreeList` 头插带来的执行顺序。
