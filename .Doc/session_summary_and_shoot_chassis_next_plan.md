# 当前会话总结与下一阶段 `shoot motor / chassis motor` 路线规划

## 1. 文档目的

本文档用于收口当前会话已完成的工作，并为下一步继续推进：

- `shoot motor`
- `chassis motor`

的 fan-out 任务拆分与实现路线提供统一基线。

本文档不是最终设计说明，而是后续继续执行时的“当前状态快照 + 下一步路线图”。

## 2. 本次会话已完成事项

### 2.1 架构与规划层

已完成并固化的规划文档：

- [basic_framework_to_xrobot_migration_plan.md](/F:/Xrobot/Xro_template/.Doc/basic_framework_to_xrobot_migration_plan.md)
- [basic_framework_xrobot_execution_blueprint.md](/F:/Xrobot/Xro_template/.Doc/basic_framework_xrobot_execution_blueprint.md)
- [real_remote_imu_motor_fanout_plan.md](/F:/Xrobot/Xro_template/.Doc/real_remote_imu_motor_fanout_plan.md)

已完成的核心架构结论：

- 当前工程必须保持唯一运行链：
  `FreeRTOS defaultTask -> app_main() -> XRobotMain(peripherals) -> ApplicationManager::MonitorAll()`
- 当前工程已正式采用：
  `User -> App -> Modules -> LibXR`
  的单向依赖
- `App/` 是业务角色编排层
- `Modules/` 是可复用能力层
- `App/Topics/*` 是跨角色稳定契约层

### 2.2 已落地的代码结构

当前仓库已经不再是模板初始状态，而是已经落地以下结构：

- `App/Runtime`
- `App/Roles`
- `App/Topics`
- `App/Config`
- `App/Integration`

已固定的 6 个角色：

- `InputRole`
- `DecisionRole`
- `ChassisRole`
- `GimbalRole`
- `ShootRole`
- `TelemetryRole`

已固定的 8 个 Topic：

- `robot_mode`
- `motion_command`
- `aim_command`
- `fire_command`
- `chassis_state`
- `gimbal_state`
- `shoot_state`
- `system_health`

### 2.3 第一批：骨架冻结批

已完成：

- `App/` 目录正式接入 CMake
- `AppRuntime` 已建立
- 6 个角色骨架已建立
- 8 个 Topic schema 已建立
- 构建可通过 `cmake --build --preset Debug`

### 2.4 第二批：命令闭环批

已完成：

- `InputRole -> DecisionRole -> GimbalRole / ShootRole`
  最小命令闭环
- 真实 `remote` 第一轮接入已经完成：
  - 新增 [RemoteControl.hpp](/F:/Xrobot/Xro_template/Modules/RemoteControl/RemoteControl.hpp)
  - `InputRole` 不再依赖板载 `KEY`
  - 当前使用 DJI DBUS 18 字节帧驱动最小输入链
- `DecisionRole` 继续负责发布：
  - `robot_mode`
  - `motion_command`
  - `aim_command`
  - `fire_command`

### 2.5 第三批：执行与遥测闭环批

已完成：

- `ChassisRole` 已消费 `motion_command` 并发布 `chassis_state`
- `TelemetryRole` 已聚合：
  - `robot_mode`
  - `chassis_state`
  - `gimbal_state`
  - `shoot_state`
  并发布 `system_health`

### 2.6 真实 `imu` 第一轮接入

已完成：

- 在 [app_main.cpp](/F:/Xrobot/Xro_template/User/app_main.cpp) 中补齐 `BMI088` 所需硬件别名：
  - `spi_bmi088`
  - `bmi088_accl_cs`
  - `bmi088_gyro_cs`
  - `bmi088_gyro_int`
  - `pwm_bmi088_heat`
- 同时补了：
  - `ramfs`
  - `database`
- 在 [AppRuntime.cpp](/F:/Xrobot/Xro_template/App/Runtime/AppRuntime.cpp) 中实例化 `BMI088`
- `GimbalRole` 和 `ChassisRole` 已开始消费：
  - `bmi088_gyro`
  - `bmi088_accl`

当前真实 IMU 消费结果：

- `GimbalRole.yaw_rate_degps / pitch_rate_degps`
  已来自真实 gyro
- `ChassisRole.wz_radps`
  已优先来自真实 gyro z
- `ChassisRole.yaw_deg`
  当前为基于 gyro z 的相对积分估计，不是绝对朝向

### 2.7 真实 `motor` 第一轮接入

已完成：

- 新增 [GM6020GimbalMotor.hpp](/F:/Xrobot/Xro_template/Modules/GM6020GimbalMotor/GM6020GimbalMotor.hpp)
- `AppRuntime` 中已实例化该模块
- 当前仅做：
  - `yaw` 轴最小真实闭环
  - 不做 `pitch`

当前闭环策略：

- 消费：
  - `robot_mode`
  - `aim_command`
  - `bmi088_gyro`
- 用 gyro z 积分得到相对 yaw
- 外环生成目标角速度
- 内环生成 GM6020 电流指令
- `safe/disabled/emergency_stop` 时发送零电流

## 3. 板上调试结论

### 3.1 已验证的事实

主机侧调试链已验证正常：

- `JLink.exe`
- `JLinkGDBServerCL.exe`
- `arm-none-eabi-gdb.exe`

均可用，且 J-Link 能识别目标板。

### 3.2 关于 BMI088 读值

本次板上调试最终已经确认：

- `BMI088::ThreadFunc` 能命中
- `App::GimbalRole::OnMonitor` 能命中
- 已抓到 3 组 `gyro_data_` 样本

样本结果：

```text
Sample 1: 0, 0, 0
Sample 2: 0, 0, 0
Sample 3: 0.0276968759, 0.0170442313, 0.00852211565
```

阶段性结论：

- 当前不存在“始终全零”
- 当前不存在明显 `NaN/Inf`
- 当前量级在静止或微动条件下是可解释的
- 启动初期存在短暂零值窗口，更像启动时序问题，而不是 IMU 本体完全失效

### 3.3 关于启动稳定性

本次还发现过一个重要问题：

- 增大 `defaultTask` 栈之前，程序曾在板上较早进入 `HardFault`
- 在 [freertos.c](/F:/Xrobot/Xro_template/Core/Src/freertos.c) 中把默认任务栈从 `128 * 4` 调到 `1024 * 4` 后，问题明显缓和，程序能够继续进入应用层和 BMI088 线程

这说明当前板上运行稳定性与任务栈空间直接相关。

## 4. 当前仍未完成的事项

### 4.1 `shoot motor`

当前 `ShootRole` 仍处于：

- 命令镜像
- 状态镜像

阶段，尚未接真实执行器。

未完成点：

- 未接真实摩擦轮电机
- 未接真实拨盘电机
- 未建立真实发射执行闭环
- `shoot_state` 仍主要是逻辑状态，不是执行器真实反馈

### 4.2 `chassis motor`

当前 `ChassisRole` 虽然已接入真实 IMU 消费，但执行部分仍是：

- `motion_command` 镜像
- `chassis_state` 占位发布

未完成点：

- 未接真实底盘执行器
- 未接真实底盘运动学
- 未接功率限制与裁判系统约束
- 未接真实速度/电流闭环

### 4.3 其他能力

以下仍未开始或未进入真实闭环：

- `referee`
- `master_machine`
- `can_comm`
- `shoot motor`
- `chassis motor`

## 5. 当前统一约束

后续继续推进时，默认必须遵守：

### 5.1 边界约束

- `User/` 只做板级接线和入口转交
- `App/` 只做角色编排
- `Modules/` 只做可复用能力
- `App/Topics` 只放跨角色稳定契约

### 5.2 风格约束

- 长期运行单元优先继承 `LibXR::Application`
- 周期行为优先收敛到 `OnMonitor()`
- 硬件依赖必须从 `HardwareContainer` 查找
- `ASyncSubscriber` 构造后要 `StartWaiting()`
- 每次 `GetData()` 后要重新 `StartWaiting()`
- `Timebase::GetMicroseconds()` 的差值优先用 `Duration::ToSecondf()`
- 不直接在 `App/` 或 `Modules/` 中泄漏 HAL 句柄

### 5.3 donor 迁移禁令

- 禁止把 donor `.c/.h` 原样迁入
- 禁止迁移 `message_center`、`daemon` 的旧实现
- 禁止把协议帧直接当业务对象
- 禁止把 `DJIMotorInit/DJIMotorControl` 整套对象模型直接照搬

## 6. 下一步路线：围绕 `shoot motor / chassis motor` 继续推进

当前最合理的下一阶段主线是：

```text
shoot motor
-> chassis motor
-> referee
-> master_machine / can_comm
```

原因：

- `remote` 已接入
- `imu` 已接入
- 第一条真实 `gimbal motor` 已接入

接下来最有价值的不是继续扩展 `gimbal`，而是把剩余两条执行链从“镜像状态”升级为“真实执行闭环”。

## 7. 推荐的 fan-out 路线

建议下一轮任务拆成 4 条流：

### Flow A: Shoot Motor Flow

目标：

- 让 `ShootRole` 接入真实执行器

应优先拆成：

- 摩擦轮执行器模块
- 拨盘执行器模块
- `ShootRole` 消费与状态反馈

最低验收：

- 至少一条真实发射执行链跑通
- `shoot_state` 至少部分字段来自真实执行器

### Flow B: Chassis Motor Flow

目标：

- 让 `ChassisRole` 从镜像状态升级为真实底盘执行闭环

应优先拆成：

- 执行器抽象
- 最小运动学映射
- `ChassisRole` 输出到真实电机目标

最低验收：

- `motion_command` 不再只停留在 Topic 层
- 至少一条真实底盘执行路径跑通

### Flow C: Constraint & Integration Flow

目标：

- 在推进 `shoot/chassis` 时持续守住边界

要持续检查：

- 新代码是否误放进 `App` 或 `Modules`
- 是否有新的协议帧污染 `App/Topics`
- 是否有角色长成新的“超级模块”

### Flow D: Verification Flow

目标：

- 保持“每推进一条真实执行链，就做一次构建验证和板上验证”

最低要求：

- `cmake --build --preset Debug`
- 板上执行链验证
- 状态 Topic 观察

## 8. 下一轮最适合直接下发的任务

### Task 1: `shoot motor` 最小真实闭环

目标：

- 让 `ShootRole` 从命令镜像升级为真实执行闭环

建议写入域：

- `Modules/Shoot...`
- `App/Roles/ShootRole.*`
- 如确有必要，再补极少量 Runtime 接线

建议优先级：

- 先摩擦轮
- 再拨盘

### Task 2: `chassis motor` 最小真实闭环

目标：

- 让 `ChassisRole` 驱动真实底盘执行器

建议写入域：

- `Modules/ChassisMotor...`
- `App/Roles/ChassisRole.*`

建议说明：

- 第一阶段可先不做完整底盘动力学
- 先做“命令 -> 一条真实执行器输出”闭环也有价值

### Task 3: 执行链板上验证

目标：

- 对 `shoot motor` 和 `chassis motor` 的新闭环做板上验证

建议方式：

- 继续用 J-Link + GDB / 断点 / Topic 状态观察
- 验证真实执行器状态是否与 `Role` 发布一致

## 9. 结论

当前会话已经把工程从“模板 + 架构规划”推进到了：

- 有真实 `remote`
- 有真实 `imu`
- 有第一条真实 `gimbal motor`

的阶段。

下一步最合理、收益最高的方向，不再是继续做抽象，而是：

**把 `ShootRole` 和 `ChassisRole` 从镜像状态继续推进到真实执行闭环。**

如果后续继续执行，建议直接围绕：

- `shoot motor`
- `chassis motor`

做新的 fan-out 任务拆分，而不是再回头重做输入链或 IMU 链。
