# Shoot / Chassis 真实执行链下一阶段实施计划

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在当前 `remote -> imu -> yaw gimbal motor` 已落地的基础上，优先把 `ShootRole` 与 `ChassisRole` 从 Topic 镜像升级为真实执行闭环，同时保持 `User -> App -> Modules -> LibXR` 的边界不退化。

**Architecture:** 不直接搬运 donor 的 `shoot.c/chassis.c`，而是提取其中的控制语义、执行链拆分和参数约束，重构为 XRobot 风格的 `App` 编排层与 `Modules` 执行器层。执行顺序固定为“硬件/语义冻结 -> Shoot 最小真实闭环 -> Chassis 最小真实闭环 -> 板上验证与收口”，明确延后 `referee/master_machine/can_comm/lid` 等非关键路径能力。

**Tech Stack:** STM32F407, FreeRTOS, LibXR Topic/Application, XRobot Modules, CMake, J-Link + GDB

---

## 为什么下一步不能直接照搬 donor

- 当前 [ShootRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ShootRole.cpp) 仍只是 `fire_command -> shoot_state` 的占位镜像，没有真实执行器。
- 当前 [ChassisRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ChassisRole.cpp) 只消费 `motion_command` 与 `bmi088_*`，没有底盘执行器和速度反馈闭环。
- donor 的 `shoot` 语义依赖 `friction_mode / bullet_speed / shoot_rate / lid_mode`，但当前 [FireCommand.hpp](/F:/Xrobot/Xro_template/App/Topics/FireCommand.hpp) 只有 `fire_enable / loader_mode / burst_count`。
- donor 的 `chassis` 语义依赖轮组几何、云台偏角、功率限制、裁判系统反馈，但当前 [MotionCommand.hpp](/F:/Xrobot/Xro_template/App/Topics/MotionCommand.hpp) 只覆盖 `vx / vy / wz / motion_mode`。
- 当前唯一真实执行器样板 [GM6020GimbalMotor.hpp](/F:/Xrobot/Xro_template/Modules/GM6020GimbalMotor/GM6020GimbalMotor.hpp) 是“第一条过渡闭环”，可以借鉴控制循环与 CAN 发包方式，但不应把它变成后续所有执行链的默认边界模型。

## 范围冻结

### 本轮必须完成

- `shoot motor` 最小真实闭环
- `chassis motor` 最小真实闭环
- 每条闭环对应的构建验证与板上验证

### 本轮明确延后

- `referee`
- `master_machine`
- `can_comm`
- `shoot` 弹舱盖（lid/servo）
- `chassis` 功率限制与超级电容联动
- `chassis` 跟随云台偏角和完整场地坐标控制

---

## 文件责任图

### 现有关键文件

- [App/Runtime/AppRuntime.cpp](/F:/Xrobot/Xro_template/App/Runtime/AppRuntime.cpp): 模块与角色的统一实例化入口，后续新增执行器模块必须先于消费它们的角色构造。
- [App/Roles/ShootRole.hpp](/F:/Xrobot/Xro_template/App/Roles/ShootRole.hpp)
- [App/Roles/ShootRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ShootRole.cpp)
- [App/Roles/ChassisRole.hpp](/F:/Xrobot/Xro_template/App/Roles/ChassisRole.hpp)
- [App/Roles/ChassisRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ChassisRole.cpp)
- [App/Roles/DecisionRole.cpp](/F:/Xrobot/Xro_template/App/Roles/DecisionRole.cpp): 当前高层命令唯一来源，若 Topic 契约扩展，需要同步补默认值。
- [User/app_main.cpp](/F:/Xrobot/Xro_template/User/app_main.cpp): 硬件别名注入入口，只有在新增执行器确实需要新的硬件别名时才改。

### 建议新增文件

- `Modules/ShootFrictionMotorGroup/ShootFrictionMotorGroup.hpp`
- `Modules/ShootLoaderMotor/ShootLoaderMotor.hpp`
- `Modules/MecanumChassisDrive/MecanumChassisDrive.hpp`
- `App/Config/ActuatorConfig.hpp`

### 已确认硬件拓扑

- `shoot`
  - 双摩擦轮：`2 x M3508`
  - 拨盘：`1 x M3508`
  - 总线：`can2`
- `chassis`
  - 四轮：`4 x M3508`
  - 总线：`can1`
- 迁移语义继续沿用 donor 思路，但实现形式必须保持当前仓库的 `C++ + LibXR + Role/Module` 风格，不回退到 donor 的 C 风格 `Init/Task + 全局静态单例` 结构

### 建议可能扩展的 Topic 契约文件

- [App/Topics/FireCommand.hpp](/F:/Xrobot/Xro_template/App/Topics/FireCommand.hpp)
- [App/Topics/ShootState.hpp](/F:/Xrobot/Xro_template/App/Topics/ShootState.hpp)
- [App/Topics/ChassisState.hpp](/F:/Xrobot/Xro_template/App/Topics/ChassisState.hpp)

---

## 当前 fan-in / fan-out 边界确认

### Fan-in 1: 原始输入汇聚边界

- `RemoteControl` 模块发布私有强类型 Topic `remote_control_state`
- `InputRole` 只负责把遥控器原始状态映射为 `OperatorInputSnapshot`
- `OperatorInputSnapshot` 只作为 Runtime 内部共享快照，不进入公共 Topic 契约

**当前已确认的字段来源：**

- 移动输入：当前 [InputRole.cpp](/F:/Xrobot/Xro_template/App/Roles/InputRole.cpp) 尚未把 `left_x / left_y` 映射到底盘速度，`target_vx_mps / target_vy_mps / target_wz_radps` 仍固定为 0
- 云台输入：`right_x / right_y -> target_yaw_deg / target_pitch_deg`
- 发射使能：`left_switch up -> friction_enabled`，`friction_enabled && mouse_left_pressed -> fire_enabled`

**边界结论：**

- `RemoteControlState` 不升级成 `App/Topics` 公共契约
- `InputRole` 不直接输出执行器命令，只产出统一输入意图
- 若后续接入键鼠、视觉或裁判系统输入，应继续 fan-in 到 `OperatorInputSnapshot`，而不是绕过 `DecisionRole`

### Fan-in 2: 决策汇聚边界

- `DecisionRole` 是当前唯一高层命令扇出点
- 当前它输出 4 条公共命令 Topic：
  - `robot_mode`
  - `motion_command`
  - `aim_command`
  - `fire_command`

**当前缺口：**

- `motion_command` 已有结构，但上游还没有真实底盘移动输入
- `fire_command` 当前只有 `fire_enable / loader_mode / burst_count`，不足以表达 donor `shoot` 的摩擦轮、射频和目标弹速语义

**边界结论：**

- 新增 shoot / chassis 执行能力时，优先补 `DecisionRole` 输出契约，不允许让执行器模块自己猜业务模式
- `DecisionRole` 仍然只做“意图到命令”的转换，不下沉电机闭环、CAN 帧细节或发射节奏状态机

### Fan-out 1: 命令到执行器边界

- `GimbalRole` 已有真实链路旁路样板：命令 Topic 被真实执行器模块消费
- `ShootRole` 当前仍是“命令镜像 -> shoot_state”
- `ChassisRole` 当前仍是“命令镜像 + IMU 辅助估计 -> chassis_state`

**推荐目标形态：**

- `Role` 负责业务编排、准入条件、状态机和状态发布
- `Module` 负责执行器控制、传感器读取和最小硬件抽象
- `AppRuntime` 只负责实例化顺序与依赖注入，不承载控制逻辑

### Fan-out 2: 状态回流边界

- `ShootRole / ChassisRole / GimbalRole` 发布各自状态 Topic
- `TelemetryRole` 汇聚上述状态与 `robot_mode`，统一发布 `system_health`

**边界结论：**

- 执行器真实反馈先进入各自 `*_state` Topic，再由 `TelemetryRole` 汇总
- 不允许执行器模块直接写 `system_health`
- `TelemetryRole` 不承接发射或底盘控制决策，只做聚合与可观测性出口

### 当前仍未冻结的执行器参数

以下问题不再阻塞架构和任务拆分，但仍然影响最终参数定稿与板上调参：

- `shoot friction / loader / chassis 4-wheel` 的电机 ID 分配
- 各电机转向与符号约定
- 底盘轮序与几何参数的最终值
- `shoot` 摩擦轮目标转速与拨盘单发/连发参数

**处理原则：**

- 现在可以继续细化“契约层 + 模块边界层 + 执行器模块任务”
- 进入板上调试前必须把这些参数收敛到配置文件，而不是散写在 Role 或 Module 里

---

## Task 0: 冻结硬件映射与 donor 迁移边界

**Files:**
- Modify: [.Doc/2026-04-21_shoot_chassis_real_actuation_next_plan.md](/F:/Xrobot/Xro_template/.Doc/2026-04-21_shoot_chassis_real_actuation_next_plan.md)
- Reference: [User/app_main.cpp](/F:/Xrobot/Xro_template/User/app_main.cpp)
- Reference: `D:\RoboMaster\HeroCode\Code\basic_framework-master\basic_framework-master\application\shoot\shoot.c`
- Reference: `D:\RoboMaster\HeroCode\Code\basic_framework-master\basic_framework-master\application\chassis\chassis.c`

- [ ] 确认当前板级上 `gimbal / shoot / chassis` 的 CAN 总线分配、目标电机 ID、方向和减速比，不默认沿用 donor 的 `hcan1/hcan2 + tx_id` 配置。
- [ ] 固化当前已确认拓扑：`shoot = can2 上 2 x M3508 friction + 1 x M3508 loader`，`chassis = can1 上 4 x M3508`。
- [ ] 记录本轮“迁移语义”与“明确延后”的边界，禁止把 donor 的 `message_center / daemon / referee / super_cap` 一并带入。
- [ ] 若发现 `shoot` 需要新 PWM/Servo 或额外 GPIO 别名，再决定是否修改 [User/app_main.cpp](/F:/Xrobot/Xro_template/User/app_main.cpp)。
- [ ] 记录“继续 donor 控制语义，但实现必须保持当前 C++ 工程风格”的约束。

**Acceptance Criteria:**
- 形成一张可执行的硬件配置表，至少覆盖 `shoot friction / loader / chassis 4-wheel`。
- 明确写出“本轮不做 lid、不做 referee 功率限制”的冻结结论。

### Task 0A: 确认 fan-in 不扩边界

**Files:**
- Modify: [.Doc/2026-04-21_shoot_chassis_real_actuation_next_plan.md](/F:/Xrobot/Xro_template/.Doc/2026-04-21_shoot_chassis_real_actuation_next_plan.md)
- Reference: [App/Roles/InputRole.cpp](/F:/Xrobot/Xro_template/App/Roles/InputRole.cpp)
- Reference: [App/Roles/DecisionRole.cpp](/F:/Xrobot/Xro_template/App/Roles/DecisionRole.cpp)
- Reference: [App/Runtime/OperatorInputSnapshot.hpp](/F:/Xrobot/Xro_template/App/Runtime/OperatorInputSnapshot.hpp)

- [ ] 确认 `RemoteControlState -> OperatorInputSnapshot -> DecisionRole` 是唯一输入汇聚主链。
- [ ] 明确后续 `shoot/chassis` 不直接订阅 `remote_control_state`，仍通过 `DecisionRole` 接收统一命令。
- [ ] 标注当前 `InputRole` 尚未输出真实底盘移动输入，这会影响 `chassis` 链路的第一阶段验收定义。

**Acceptance Criteria:**
- 输入 fan-in 边界被文档化。
- 后续实现不会绕开 `DecisionRole` 直接消费原始遥控器 Topic。

### Task 0B: 确认 fan-out 不扩边界

**Files:**
- Modify: [.Doc/2026-04-21_shoot_chassis_real_actuation_next_plan.md](/F:/Xrobot/Xro_template/.Doc/2026-04-21_shoot_chassis_real_actuation_next_plan.md)
- Reference: [App/Roles/ShootRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ShootRole.cpp)
- Reference: [App/Roles/ChassisRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ChassisRole.cpp)
- Reference: [App/Roles/TelemetryRole.cpp](/F:/Xrobot/Xro_template/App/Roles/TelemetryRole.cpp)

- [ ] 确认状态回流路径固定为 `Role -> *_state -> TelemetryRole -> system_health`。
- [ ] 明确 `TelemetryRole` 只做聚合，不吸收故障处理、功率限制或控制状态机。
- [ ] 明确 `ShootRole/ChassisRole` 负责业务编排，执行器模块只负责控制与反馈。

**Acceptance Criteria:**
- 输出 fan-out 边界被文档化。
- 不会把 `TelemetryRole` 扩成新的总调度器。

---

## Task 1: 对齐 Shoot 的最小 XRobot 契约

**Files:**
- Modify: [App/Topics/FireCommand.hpp](/F:/Xrobot/Xro_template/App/Topics/FireCommand.hpp)
- Modify: [App/Topics/ShootState.hpp](/F:/Xrobot/Xro_template/App/Topics/ShootState.hpp)
- Modify: [App/Roles/DecisionRole.cpp](/F:/Xrobot/Xro_template/App/Roles/DecisionRole.cpp)
- Modify: [App/Runtime/OperatorInputSnapshot.hpp](/F:/Xrobot/Xro_template/App/Runtime/OperatorInputSnapshot.hpp)

- [ ] 把 donor `shoot_cmd` 中真正影响执行链的语义拆出来，只保留本轮需要的字段。
- [ ] 推荐第一批只补这 3 类能力：`friction_enable`、`target_bullet_speed_mps`、`shoot_rate_hz`；不要把 `lid_mode` 一起带入。
- [ ] 让 [DecisionRole.cpp](/F:/Xrobot/Xro_template/App/Roles/DecisionRole.cpp) 在现有输入能力不足时填充稳定默认值，而不是制造半初始化命令。
- [ ] 扩展 [ShootState.hpp](/F:/Xrobot/Xro_template/App/Topics/ShootState.hpp) 的可观测字段，使其能表达“真实摩擦轮是否启动、拨盘是否工作、是否处于卡弹恢复”。

**Acceptance Criteria:**
- `FireCommand` 能完整表达本轮 shoot 执行器所需最小意图。
- `ShootState` 不再只是逻辑镜像，具备承载真实反馈的字段。
- 不引入 `lid`、裁判系统热量或 donor 协议结构体污染。

### Task 1A: 拆 donor shoot 语义，只保留第一批必需字段

**Files:**
- Modify: [App/Topics/FireCommand.hpp](/F:/Xrobot/Xro_template/App/Topics/FireCommand.hpp)
- Reference: `D:\RoboMaster\HeroCode\Code\basic_framework-master\basic_framework-master\application\robot_def.h`
- Reference: `D:\RoboMaster\HeroCode\Code\basic_framework-master\basic_framework-master\application\shoot\shoot.c`

- [ ] 从 donor `shoot_mode / friction_mode / load_mode / bullet_speed / shoot_rate / lid_mode` 中挑出第一批真实闭环真正需要的字段。
- [ ] 推荐冻结为：`fire_enable`、`friction_enable`、`loader_mode`、`target_bullet_speed_mps`、`shoot_rate_hz`。
- [ ] 明确 `lid_mode` 与 `rest_heat` 本轮不进入公共契约。
- [ ] 明确由于当前工程是 C++，这些语义要落成类型清晰的 `enum class / struct`，而不是 donor 的 C 枚举和 packed 传输结构。

**Acceptance Criteria:**
- `FireCommand` 的字段集合与第一批闭环直接对齐。
- 没有把 donor 的历史字段整包照搬。

### Task 1B: 拆 ShootState 的“执行器反馈字段”和“上层状态字段”

**Files:**
- Modify: [App/Topics/ShootState.hpp](/F:/Xrobot/Xro_template/App/Topics/ShootState.hpp)
- Reference: [App/Roles/TelemetryRole.cpp](/F:/Xrobot/Xro_template/App/Roles/TelemetryRole.cpp)

- [ ] 区分 `ready / fire_enable / jammed` 这类上层状态字段，与 `friction_on / bullet_speed_mps / loader_active` 这类执行器反馈字段。
- [ ] 为后续 `TelemetryRole` 保留最小可观测字段，不把调参细节塞进 Topic。

**Acceptance Criteria:**
- `ShootState` 的字段语义分层清楚。
- Telemetry 消费方不需要理解执行器内部细节也能判断在线和准备状态。

---

## Task 2: 落地 Shoot 执行器模块

**Files:**
- Create: `Modules/ShootFrictionMotorGroup/ShootFrictionMotorGroup.hpp`
- Create: `Modules/ShootLoaderMotor/ShootLoaderMotor.hpp`
- Modify: [App/Runtime/AppRuntime.cpp](/F:/Xrobot/Xro_template/App/Runtime/AppRuntime.cpp)
- Modify: [App/Runtime/AppRuntime.hpp](/F:/Xrobot/Xro_template/App/Runtime/AppRuntime.hpp)
- Modify: [User/app_main.cpp](/F:/Xrobot/Xro_template/User/app_main.cpp)（仅在新增硬件别名时）

- [ ] 参考 [GM6020GimbalMotor.hpp](/F:/Xrobot/Xro_template/Modules/GM6020GimbalMotor/GM6020GimbalMotor.hpp) 的 `LibXR::Application + Topic + CAN` 结构，但不要把 `shoot` 业务节奏判断塞进模块内部。
- [ ] `ShootFrictionMotorGroup` 负责双摩擦轮速度设定与安全停机。
- [ ] `ShootLoaderMotor` 负责拨盘单发/连发/反转等执行器级行为，支持最小卡弹恢复接口，但不在模块里实现整套上层发射策略。
- [ ] 所有模块在 `safe/disabled/emergency_stop` 下必须输出零命令，并可独立实例化与独立验证。
- [ ] 明确 `ShootFrictionMotorGroup` 与 `ShootLoaderMotor` 都挂在 `can2`，且默认电机型号都是 `M3508`。

**Acceptance Criteria:**
- `AppRuntime` 能实例化 shoot 相关真实执行器模块。
- 在不触发发射命令时，所有 shoot 执行器保持安全静止。
- 编译通过且没有破坏现有 `remote/imu/gimbal motor` 链路。

### Task 2A: 先冻结 Shoot 模块接口，不急着写控制参数

**Files:**
- Create: `Modules/ShootFrictionMotorGroup/ShootFrictionMotorGroup.hpp`
- Create: `Modules/ShootLoaderMotor/ShootLoaderMotor.hpp`
- Create: `App/Config/ActuatorConfig.hpp`

- [ ] 先定义双摩擦轮模块与拨盘模块的构造参数、输入 Topic、输出状态接口。
- [ ] 模块接口里显式表达 `can2`、`M3508`、电机 ID、方向和限幅参数来源。
- [ ] 不把具体 PID 调参值硬编码到 Role 中。

**Acceptance Criteria:**
- shoot 两个模块的接口职责清晰。
- 后续调参只需要改配置，不需要改编排层。

### Task 2B: 落地 ShootFrictionMotorGroup

**Files:**
- Create: `Modules/ShootFrictionMotorGroup/ShootFrictionMotorGroup.hpp`
- Modify: [App/Runtime/AppRuntime.cpp](/F:/Xrobot/Xro_template/App/Runtime/AppRuntime.cpp)

- [ ] 实现双 M3508 摩擦轮最小控制闭环。
- [ ] 支持 `enable/disable`、目标转速设定、统一安全停机。
- [ ] 先不在模块内部处理开火节奏和拨盘动作。

**Acceptance Criteria:**
- `ShootFrictionMotorGroup` 可被独立实例化。
- 两个摩擦轮都能在 `can2` 上输出正确命令框架。

### Task 2C: 落地 ShootLoaderMotor

**Files:**
- Create: `Modules/ShootLoaderMotor/ShootLoaderMotor.hpp`
- Modify: [App/Runtime/AppRuntime.cpp](/F:/Xrobot/Xro_template/App/Runtime/AppRuntime.cpp)

- [ ] 实现单个 M3508 拨盘最小控制闭环。
- [ ] 第一批支持 `stop / single / continuous / reverse` 四种执行器级行为。
- [ ] 把“一发对应的目标角增量”与“连续供弹速度”放进配置，而不是散写在模块正文。

**Acceptance Criteria:**
- `ShootLoaderMotor` 可被独立实例化。
- 拨盘动作模式与上层 `loader_mode` 能建立一一映射。

---

## Task 3: 升级 ShootRole 为真实编排层

**Files:**
- Modify: [App/Roles/ShootRole.hpp](/F:/Xrobot/Xro_template/App/Roles/ShootRole.hpp)
- Modify: [App/Roles/ShootRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ShootRole.cpp)
- Modify: [App/Topics/ShootState.hpp](/F:/Xrobot/Xro_template/App/Topics/ShootState.hpp)

- [ ] 让 `ShootRole` 从“直接镜像命令”升级为“基于 robot_mode + fire_command 的发射策略编排”。
- [ ] 第一批只实现 3 个场景：停止、单发、连续供弹；三连发可以先映射为固定 burst。
- [ ] 处理 fire edge、冷却/不应期、ready 状态、jam 恢复触发点，并把策略结果交给执行器模块。
- [ ] `shoot_state` 的 `ready / fire_enable / friction_on / jammed` 应来自真实执行链状态，而不是纯逻辑推断。
- [ ] 明确 `ShootRole` 只编排 donor 语义，不直接写 CAN 帧、不直接做底层电机对象管理。

**Acceptance Criteria:**
- `ShootRole` 不再只做 Topic 镜像。
- 至少一条真实 shoot 执行链可以被触发并反馈到 `shoot_state`。
- 模块层不承载整车级“是否允许开火”的业务判断。

---

## Task 4: 冻结 Chassis 第一批控制边界与参数位置

**Files:**
- Create: `App/Config/ActuatorConfig.hpp`
- Modify: [App/Topics/ChassisState.hpp](/F:/Xrobot/Xro_template/App/Topics/ChassisState.hpp)
- Reference: `D:\RoboMaster\HeroCode\Code\basic_framework-master\basic_framework-master\application\chassis\chassis.c`

- [ ] 把 donor 中与底盘执行链强相关的常量迁移为 XRobot 配置，而不是继续散落在角色或模块里。
- [ ] 第一批只冻结：轮序、轮距/轴距、轮半径、减速比、轮向反转、速度上限。
- [ ] 明确第一批不做：裁判系统功率限制、超级电容联动、云台偏角跟随。
- [ ] 视需要扩展 [ChassisState.hpp](/F:/Xrobot/Xro_template/App/Topics/ChassisState.hpp) 以承载轮组在线状态、实际角速度或估计速度质量。
- [ ] 明确底盘执行器固定为 `can1` 上的 `4 x M3508`，并按当前 C++ 项目风格写入统一配置入口。

**Acceptance Criteria:**
- 底盘几何和执行参数有单一配置出口。
- 第一批底盘闭环的“做什么/不做什么”被写死，避免中途插入 `referee` 需求。

### Task 4A: 明确第一批 chassis 命令语义只支持机体系速度

**Files:**
- Modify: [.Doc/2026-04-21_shoot_chassis_real_actuation_next_plan.md](/F:/Xrobot/Xro_template/.Doc/2026-04-21_shoot_chassis_real_actuation_next_plan.md)
- Reference: [App/Topics/MotionCommand.hpp](/F:/Xrobot/Xro_template/App/Topics/MotionCommand.hpp)
- Reference: `D:\RoboMaster\HeroCode\Code\basic_framework-master\basic_framework-master\application\chassis\chassis.c`

- [ ] 确认第一批 `chassis` 只消费 `vx / vy / wz` 机体系目标。
- [ ] 明确 donor 的 `offset_angle`、`follow_gimbal_yaw`、`rotate mode` 暂不迁入。
- [ ] 若上游移动输入仍未接好，允许第一阶段只验证“Role/Module 通路正确 + 单轮/四轮输出正确”，不要求整车遥控闭环完整。

**Acceptance Criteria:**
- `chassis` 第一批命令语义收敛。
- 不因 donor 的跟随模式把任务范围重新放大。

### Task 4B: 明确底盘状态估计的可信度分层

**Files:**
- Modify: [App/Topics/ChassisState.hpp](/F:/Xrobot/Xro_template/App/Topics/ChassisState.hpp)
- Modify: [App/Roles/ChassisRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ChassisRole.cpp)

- [ ] 区分“真实传感器反馈字段”和“命令镜像/估计字段”。
- [ ] 第一批允许 `wz_radps` 来自 IMU，`vx/vy` 来自命令镜像或电机逆解估计，但要在注释中标明可信度。

**Acceptance Criteria:**
- `ChassisState` 不会给人“所有字段都是真实反馈”的假象。
- 后续接入 encoder/referee 时可平滑替换估计字段来源。

---

## Task 5: 落地底盘执行器模块与最小运动学

**Files:**
- Create: `Modules/MecanumChassisDrive/MecanumChassisDrive.hpp`
- Modify: [App/Runtime/AppRuntime.cpp](/F:/Xrobot/Xro_template/App/Runtime/AppRuntime.cpp)
- Modify: [App/Runtime/AppRuntime.hpp](/F:/Xrobot/Xro_template/App/Runtime/AppRuntime.hpp)
- Modify: [User/app_main.cpp](/F:/Xrobot/Xro_template/User/app_main.cpp)（仅在硬件别名需要补充时）

- [ ] 新模块负责 4 个底盘电机的初始化、轮速目标下发和安全停机。
- [ ] 在模块内部完成最小麦轮正运动学，把机体系 `vx/vy/wz` 转成四轮目标。
- [ ] 不在模块内部引入 `robot_mode` 之外的整车业务规则，不处理裁判系统功率预算。
- [ ] 保持模块本身可复用，避免把当前英雄车的业务分支硬编码成未来其他车不可复用的实现。
- [ ] 明确模块底层使用 `can1` 上的 `4 x M3508`，不复用 donor 的 C 风格电机对象模型。

**Acceptance Criteria:**
- 底盘执行器模块可以独立消费机体系速度目标并输出 4 路真实电机命令。
- `safe/disabled/emergency_stop` 下所有底盘输出归零。
- 轮序、符号和方向全部由配置定义，不靠临时手写正负号。

### Task 5A: 冻结 MecanumChassisDrive 的模块接口

**Files:**
- Create: `Modules/MecanumChassisDrive/MecanumChassisDrive.hpp`
- Create: `App/Config/ActuatorConfig.hpp`

- [ ] 明确模块输入是机体系 `vx / vy / wz`，不是 donor 的 `offset_angle` 或跟随模式命令。
- [ ] 明确模块配置包含 `can1`、4 个 M3508 的 ID/方向/轮序、底盘几何和速度限幅。
- [ ] 模块对外只暴露底盘执行与最小反馈接口，不暴露 HAL/CAN 细节。

**Acceptance Criteria:**
- `MecanumChassisDrive` 的职责边界与配置入口冻结。
- 编排层和执行器层的接口不混杂。

### Task 5B: 落地四轮 M3508 输出链

**Files:**
- Create: `Modules/MecanumChassisDrive/MecanumChassisDrive.hpp`
- Modify: [App/Runtime/AppRuntime.cpp](/F:/Xrobot/Xro_template/App/Runtime/AppRuntime.cpp)

- [ ] 实现 `can1` 上 4 个 M3508 的初始化和统一停机。
- [ ] 在模块内部把 `vx / vy / wz` 转成四轮目标输出。
- [ ] 第一阶段优先保证轮序、方向、正负号正确，再谈速度闭环细化。

**Acceptance Criteria:**
- 四轮都能进入统一输出链。
- 单轮和整车输出验证路径明确。

---

## Task 6: 升级 ChassisRole 为真实状态闭环

**Files:**
- Modify: [App/Roles/ChassisRole.hpp](/F:/Xrobot/Xro_template/App/Roles/ChassisRole.hpp)
- Modify: [App/Roles/ChassisRole.cpp](/F:/Xrobot/Xro_template/App/Roles/ChassisRole.cpp)
- Modify: [App/Topics/ChassisState.hpp](/F:/Xrobot/Xro_template/App/Topics/ChassisState.hpp)

- [ ] 让 `ChassisRole` 从“命令镜像 + gyro z 积分”升级为“机体系命令编排 + 状态估计发布”。
- [ ] 第一批继续保留当前 `motion_command` 直接作为机体系目标，不引入 `offset_angle` 与跟随模式。
- [ ] `wz_radps` 优先来自 IMU，`vx/vy` 可先来自命令镜像或电机反馈估计，但必须在注释中说明可信度等级。
- [ ] 若底盘模块有反馈能力，`ChassisRole` 负责把执行器反馈整合进 `chassis_state`。

**Acceptance Criteria:**
- `motion_command` 不再只停留在 Topic 层，而是进入真实底盘执行器。
- `chassis_state` 至少有一部分字段来自真实执行链或真实传感器，而不是完全镜像。
- 不回退到 donor 的超级 `chassis app` 结构。

---

## Task 7: 验证、调参与风险收口

**Files:**
- Modify: [Core/Src/freertos.c](/F:/Xrobot/Xro_template/Core/Src/freertos.c)（仅当新模块引入栈压力后必须调整）
- Reference: [Modules/BMI088/BMI088.hpp](/F:/Xrobot/Xro_template/Modules/BMI088/BMI088.hpp)

- [ ] 每完成一条执行链，都执行一次 `cmake --build --preset Debug`。
- [ ] 板上验证顺序固定为：`StartDefaultTask -> app_main -> AppRuntime -> 执行器模块构造 -> Role OnMonitor -> 电机命令发出`。
- [ ] `shoot` 先验证摩擦轮空转与拨盘单步，再验证角色状态反馈。
- [ ] `chassis` 先验证单轮方向与轮序，再验证整车 `vx/vy/wz`。
- [ ] 若新模块再次触发早期 `HardFault` 或堆异常，优先复查 `defaultTask` 栈、堆大小和对象生命周期。
- [ ] 若顺手触达 [Modules/BMI088/BMI088.hpp](/F:/Xrobot/Xro_template/Modules/BMI088/BMI088.hpp)，修复 `GetGyroLSB()/GetAcclLSB()` 的既有 warning，但不作为本轮主路径阻塞项。
- [ ] 验证过程中持续检查是否有实现回退到 donor 的 C 风格全局状态与过程式调度。

**Acceptance Criteria:**
- `Debug` 构建可通过。
- Shoot 与 Chassis 各至少完成一次板上真实执行验证。
- 新增风险点有明确记录，不把“待调参数”伪装成“功能已完成”。

---

## 推荐执行顺序

1. `Task 0A`
2. `Task 0B`
3. `Task 0`
4. `Task 1A`
5. `Task 1B`
6. `Task 1`
7. `Task 2A`
8. `Task 2B`
9. `Task 2C`
10. `Task 2`
11. `Task 3`
12. `Task 4A`
13. `Task 4B`
14. `Task 4`
15. `Task 5A`
16. `Task 5B`
17. `Task 5`
18. `Task 6`
19. `Task 7`

## 为什么先 Shoot 再 Chassis

- `shoot` 的机械链更短、依赖面更小，适合先把“Role 编排 -> 执行器模块 -> 状态反馈”的模式跑通。
- `chassis` 依赖轮组几何、轮序方向、运动学和状态估计，错误面更大，放在第二条主线更稳妥。
- 当前 `DecisionRole` 对 `shoot` 的输出比对 `chassis` 的输出更接近 donor 的最小场景，收敛成本更低。

## 本轮完成定义

- `ShootRole` 不再是纯镜像，占位 `shoot_state` 被真实执行链部分替代。
- `ChassisRole` 不再只是发布 `motion_command` 镜像，底盘执行器开始真实输出。
- 下一阶段再进入 `referee -> master_machine / can_comm`，而不是提前并线。
