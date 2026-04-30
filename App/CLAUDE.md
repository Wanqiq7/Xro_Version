[根目录](../CLAUDE.md) > **App**

# App -- 机器人应用层

## 变更记录 (Changelog)

| 日期 | 操作 | 说明 |
|------|------|------|
| 2026-04-30 | 更新 | C++20 基线升级、RuntimeStringView 命名构造支持、模块兼容性验证 |
| 2026-04-26 | 修正 | 对齐当前真实目录、入口链路、Topic 与共享命令对象边界 |
| 2026-04-23 | 新建 | 首次扫描，全量分析 |

---

## 模块职责

`App/` 是机器人应用编排层，负责把可复用 `Modules/` 能力组织成整车控制链路。当前采用 **控制器直接注册** 架构：`InputController`、`DecisionController`、`GimbalController`、`ChassisController`、`ShootController`、`HealthController` 都继承 `ControllerBase`，构造时注册到 `LibXR::ApplicationManager`，由 `ApplicationManager::MonitorAll()` 周期调度。

边界约束：
- `Modules/` 提供传感器、执行器、通信和协议适配等通用能力。
- `App/` 负责输入汇聚、模式决策、控制命令生成、功率管理、健康监测、执行器编排和状态发布。
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

BMI088（gyro/accl）--> INS 模块 --> 发布 InsState（App 公共 Topic）
                                      |
                                      +--> ChassisController 消费
                                      +--> GimbalController 消费

HealthController 订阅所有状态 Topic，发布 SystemHealth

执行控制器同时订阅状态 Topic，并发布各自状态：
  ChassisController: 订阅 RobotMode / GimbalState / RefereeState / InsState / SuperCapState，发布 ChassisState
  GimbalController : 订阅 RobotMode / InsState，发布 GimbalState
  ShootController  : 订阅 RobotMode / RefereeState，发布 ShootState
  HealthController : 订阅 ChassisState / GimbalState / ShootState / InsState / RobotMode /
                     DT7State / VT13State / MasterMachineState / RefereeState /
                     SuperCapState / CANBridgeState，发布 SystemHealth
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
  Dev/           (编译时测试)
  Gimbal/
  Health/        (健康监测)
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
- `RefereeConstraintView.hpp`: 裁判系统约束视图，用于把裁判状态折叠成发射控制约束和功率管理输入。
- `RemoteInputMapper.hpp`: 遥控器映射辅助函数，将 DBUS 数据映射为 `OperatorInputSnapshot`。
- `MasterMachineBridgeController.hpp/.cpp`: 上位机桥接控制器，解析上位机命令帧。
- `InputController.hpp/.cpp`: 统一输入控制器，聚合所有输入源。

### Chassis/ -- 底盘控制（力控重构）

底盘控制链路在 2026-04-30 附近从速控（四路轮速 PID）重构为力控链路：

```text
MotionCommand (vx, vy, wz)
  -> ChassisController: 运动学分解 -> 目标力/力矩
  -> 底盘级力控 PID (Fx, Fy, Tz) -> 四轮力矩分配
  -> ChassisPowerLimiter: 功率限制 -> 钳位 wheel_tau_ref_nm
  -> DJIMotor (kCurrent 外环, 电流直驱)
```

组件清单：

- `ChassisController.hpp/.cpp`: 消费共享 `MotionCommand`，订阅 `RobotMode`、`GimbalState`、`InsState`、`RefereeState`、`SuperCapState`，执行 Mecanum 运动学解算 + 力控 PID + 摩擦补偿 + 功率限制，驱动 4 个 DJI 底盘电机并发布 `ChassisState`。
- `ChassisPowerLimiter.hpp`: 功率限制器薄适配层，将裁判约束和超级电容状态适配为 `ChassisPowerController` 的输入格式，调用其 Update() 后输出钳位后的力矩。
- `ChassisPowerController.hpp`: 核心功率控制器（HKUST 双环架构）：
  - **能量环 PD 控制**：基于电容能量 / 裁判缓冲能量的 sqrt PD 控制器，决定 `base_max_power_` 和 `full_max_power_`
  - **RLS 在线辨识**：使用 `Module::RLS::RlsEstimator` 在线估计 k1（转速损耗系数）和 k2（力矩平方损耗系数）
  - **功率环分配**：预测各轮功率 -> 误差置信度权重分配 -> 二次方程求解力矩上限
  - **容错降级**：电机/裁判/电容断连检测，电容离线能量估算，双断功率衰减
- `ChassisMotorPowerModel.hpp`: HKUST 功率模型 `P_i = tau*omega + k1*|omega| + k2*tau^2 + k3/n`，含预测函数和二次方程力矩求解函数。
- `ChassisPowerLimiterConfig` 在 `ChassisConfig.hpp` 中定义，含旧版档位缩放参数（当前力控链路中 motion_scale 固定为 1.0）。

### Health/ -- 健康监测（新增）

- `HealthController.hpp/.cpp`: 系统健康监测控制器。订阅全部执行器和通信链路的 Topic，执行：
  - **故障检测（Fault）**：输入丢失、INS 丢失、底盘/云台/发射故障 -> 触发 `kEmergencyStop` 或 `kDisableShoot`
  - **告警检测（Warning）**：裁判离线、超级电容离线、CANBridge 离线 -> 触发 `kLimitOutput`
  - **降级状态机**：`HealthLevel::{kOk, kWarning, kFault, kEmergencyStop}`
  - **蜂鸣器报警**：Fault 或 EmergencyStop 级别触发蜂鸣器
- `SystemHealth` 在 `App/Topics/SystemHealth.hpp` 中定义，含故障位、告警位、安全动作建议和在线状态汇总。

### Gimbal/ -- 云台控制

- `GimbalController.hpp/.cpp`: 消费共享 `AimCommand`，订阅 `RobotMode` 和 `InsState` 数据，驱动 Yaw DJI 电机与 Pitch DM 电机并发布 `GimbalState`。
- `GimbalMath.hpp`: 云台数学辅助函数。

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
| `InsState` | App 公共 Topic，描述姿态估计状态（欧拉角/角速率/四元数/运动加速度） |
| `SystemHealth` | App 公共 Topic，描述系统健康状态（故障/告警/安全动作） |
| `ChassisBoardBridge` | 双板底盘桥命令/反馈结构体（含 `constexpr` 编码/解码函数） |
| `MotionCommand` | AppRuntime 内共享命令对象，当前不是 Topic |
| `AimCommand` | AppRuntime 内共享命令对象，当前不是 Topic |
| `FireCommand` | AppRuntime 内共享命令对象，当前不是 Topic |

### Config/ -- 集中配置

- `AppConfig.hpp`: App Topic 域名、公共 Topic 名称和部分模块私有 Topic 名称常量（含 `bmi088_gyro`/`bmi088_accl` 名称）。静态名称使用 `constexpr const char*`；运行时拼接/格式化命名使用 `LibXR::RuntimeStringView`（支持 `RuntimeStringView<>("base", "_suffix")` 拼接和 `RuntimeStringView<"fmt_{}", int>` 格式化，隐式转为 `const char*` 传给 Topic/HardwareContainer API）。
- `InputConfig.hpp`: 输入映射常量（摇杆最大幅值、Yaw/Pitch 最大增量、线/角速度上限、发射速率等）。
- `BridgeConfig.hpp`: 上位机、CANBridge、SharedTopicClient 等通信桥配置。
- `ChassisConfig.hpp`: 底盘配置（Mecanum 几何参数、力控 PID、摩擦补偿、功率限制器、功率控制器）。
- `GimbalConfig.hpp`: 云台 Yaw DJI 电机、Pitch DM 电机和控制参数。
- `RefereeConfig.hpp`: 裁判系统配置与保守默认值。
- `ShootConfig.hpp`: 发射电机组、摩擦轮、拨盘和控制参数。

### Dev/ -- 编译时测试（新增）

- `CompileTests/smc_controller_compile_test.cpp`: SMC 滑模控制器编译时测试，覆盖 7 个测试用例（默认构造、参数构造、SetParam、位置控制、速度控制、5 种模式全量、死区、Reset/ClearIntegral）。

---

## 控制器执行顺序

`ApplicationManager` 底层使用 `LockFreeList` 头插注册，控制器 **后构造先执行**。`AppRuntime` 当前声明顺序刻意与期望执行顺序相反（含新增 HealthController）：

```cpp
HealthController health_controller_;       // 第 6 执行（最后）
ShootController shoot_controller_;         // 第 5 执行
ChassisController chassis_controller_;     // 第 4 执行
GimbalController gimbal_controller_;       // 第 3 执行
DecisionController decision_controller_;   // 第 2 执行
InputController input_controller_;         // 第 1 执行（最先）
```

因此单周期内的核心链路是：

```text
InputController -> DecisionController -> GimbalController -> ChassisController -> ShootController -> HealthController
```

HealthController 最后执行，收集本周期所有控制器的状态快照后进行健康判定。修改 `AppRuntime` 成员声明顺序时，必须重新确认控制器注册顺序和数据依赖。

---

## 对外接口

App 层当前对外通过 Topic 暴露状态：

- **发布 App 公共 Topic**: `robot_mode`、`chassis_state`、`gimbal_state`、`shoot_state`、`ins_state`、`system_health`
- **订阅模块私有 Topic**: DT7、VT13、MasterMachine、Referee、SuperCap、CANBridge、BMI088 等模块状态
- **内部共享对象**: `OperatorInputSnapshot`、`MotionCommand`、`AimCommand`、`FireCommand`

注意：`MotionCommand`、`AimCommand`、`FireCommand` 虽然位于 `App/Topics/`，但当前只是类型定义和共享命令对象，不是已发布的 Topic。

---

## 关键依赖

- **LibXR**: `ApplicationManager`、`Application`、`Topic`、`HardwareContainer`、`Thread`、`Timebase`、`Mutex`
- **Eigen**: 云台和底盘中使用的 3D 向量类型（INS 模块和 ChassisController 中使用）
- **Modules**: `BMI088`、`CANBridge`、`DMMotor`、`DJIMotor`、`DT7`、`VT13`、`MasterMachine`、`Referee`、`SharedTopicClient`、`SuperCap`、`INS`、`RLS`、`SMC`

---

## 常见问题 (FAQ)

**Q: 为什么 AppRuntime 中控制器声明顺序是反的？**  
A: `ApplicationManager` 使用 `LockFreeList` 头插注册，后构造的控制器先执行。因此 `ShootController` 声明在前面但最后执行，`InputController` 声明在最后面但最先执行。

**Q: 为什么 command 类型放在 Topics 目录，却不创建 Topic？**  
A: `MotionCommand`、`AimCommand`、`FireCommand` 是跨控制器稳定契约，所以类型定义放在 `App/Topics/`；但当前数据只需要在同一个 `AppRuntime` 周期内共享，按引用传递比创建 Topic 更直接，也避免把内部控制命令暴露成外部通信面。

**Q: 如何新增一个控制器？**  
A: 新控制器应继承 `ControllerBase`，由 `AppRuntime` 持有并注入依赖；若它发布跨角色长期稳定状态，再在 `App/Topics/` 增加公共状态类型并由 `AppRuntime` 创建 Topic。新增后必须检查 `LockFreeList` 头插带来的执行顺序。

**Q: 底盘功率管理的控制链路是怎样的？**  
A: `ChassisController` 在力控 PID 计算出四轮参考力矩后，将结果通过 `ChassisPowerLimiter.Apply()` 路由到 `ChassisPowerController.Update()`。功率控制器内部执行：错误标志更新 -> 裁判功率限制解析 -> 能量环 PD 控制 -> RLS 参数更新 -> 功率环分配 -> 力矩上限钳位。输出钳位后的力矩直接写入 DJIMotor 的 kCurrent 外环。

**Q: INS 模块的初始化和收敛需要多长时间？**  
A: INS 需要采集 100 次加速度样本（~100ms at 1kHz）进行初始对准。之后 EKF 通过卡方检验判断收敛（chi_square < 0.5 * threshold）。在陀螺仪稳定且加速度幅值接近重力时收敛最快。收敛前 `ins_state.online` 为 false，HealthController 会将其视为 INS 丢失并触发急停。

**Q: HealthController 的降级策略是什么？**  
A: 健康状态分为四级：kOk（正常）、kWarning（告警，如裁判离线）、kFault（故障，如底盘断连）、kEmergencyStop（紧急停机，如输入丢失或 INS 丢失）。SafeAction 按照严重程度叠加：输入/INS 丢失 -> EmergencyStop；底盘/云台故障 -> EmergencyStop；发射故障 -> DisableShoot；超级电容丢失 -> LimitOutput；裁判/CANBridge 丢失 -> Warning（不限制输出）。

**Q: 为什么升级到 C++20？有哪些需要注意的？**  
A: LibXR 已建立 C++20 基线（使用 `concept`/`requires` 约束、`consteval` 编译期格式上界计算等）。Xro_template 需同步升级标准到 C++20（`cmake/LibXR.CMake` 中 `CMAKE_CXX_STANDARD 20`）。注意事项：(1) `RawData` 构造已拒绝 const 左值（`requires(!std::is_const_v<DataType>)`），必须传入可写对象引用；(2) 旧宏 `M_PI`/`M_2PI`/`M_1G` 已替换为 `LibXR::PI`/`LibXR::TWO_PI`/`LibXR::STANDARD_GRAVITY`；(3) `OFFSET_OF`/`MEMBER_SIZE_OF`/`CONTAINER_OF` 宏已移除；(4) LibXR `system/FreeRTOS` 目录已重命名为 `system/freertos`，CMake 中 `LIBXR_SYSTEM` 需相应更新。

**Q: 如何用 RuntimeStringView 构建 Topic/设备名称？**  
A: (1) 静态文本: `RuntimeStringView<> name("sensor_0")` → 隐式转 `const char*`；(2) 文本拼接: `RuntimeStringView<> name("camera", "_gyro")` → `"camera_gyro"`；(3) 格式化: `RuntimeStringView<"motor_{}", int> name; name.Reformat(idx)` → `"motor_1"`；(4) 空指针检查: `RuntimeStringView<> name("base", static_cast<const char*>(nullptr))` → `!name.Ok()` 且 `name.Status() == ErrorCode::PTR_NULL`。对象析构不释放内存（设计用于长期保留名称）。
