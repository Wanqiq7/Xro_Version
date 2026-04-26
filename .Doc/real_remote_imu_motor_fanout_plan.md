# real remote / imu / motor 接入 fan-out 规划

## 1. 文档目标

本文档用于在当前三批占位闭环已经完成的基础上，规划下一轮真实能力接入：

- `remote`
- `imu`
- `motor`

目标不是直接写实现，而是冻结这三条链的：

- 落点
- 并行边界
- 前置依赖
- 验收门槛
- 子任务拆分方式

本文档默认遵循当前仓库已冻结的基线：

- 唯一运行链保持 `FreeRTOS defaultTask -> app_main() -> XRobotMain(peripherals) -> ApplicationManager::MonitorAll()`
- `User -> App -> Modules -> LibXR` 单向依赖不变
- `App/` 负责角色编排
- `Modules/` 负责可复用能力
- `App/Topics/*` 负责跨角色稳定契约

## 2. 总体结论

这三条链不应被当成一个“大任务”顺序硬做，而应拆成 4 条 fan-out 分析/实施流：

1. `Remote Input Flow`
2. `IMU Perception Flow`
3. `Motor Execution Flow`
4. `Integration & Constraint Flow`

其中：

- `remote` 更像 `Module + InputRole`
- `imu` 更像 `Module + Topic schema + Role 消费`
- `motor` 更像 `Module`，再由 `ChassisRole / GimbalRole / ShootRole` 编排消费

## 3. 统一实现约束

### 3.1 明确禁止

- 禁止把 donor 的 `.c/.h` 模块原样迁入再外套 C++ 壳
- 禁止把 `remote / imu / motor` 直接写进 `App/` 作为“伪模块”
- 禁止迁移 `message_center`、`daemon` 的旧实现
- 禁止让 `App/` 或新模块直接暴露 HAL 句柄
- 禁止新增白名单之外的公共 Topic，除非先更新 `AGENTS.md` 和架构文档

### 3.2 必须遵守

- 所有可复用能力优先写成继承 `LibXR::Application` 的模块
- 所有硬件依赖都从 `HardwareContainer` 查找
- 所有跨角色数据先写 Topic schema，再落实现
- 所有 ISR/回调路径必须遵守 LibXR 的 `in_isr` / `FromCallback` 语义
- 所有运行时对象必须长期存活，不能把短生命周期对象绑定进回调/Topic/Event

## 4. 当前实际状态

### 4.1 已完成的占位闭环

- `InputRole` 当前用板载 `KEY` 注入最小输入
- `DecisionRole` 已发布：
  - `robot_mode`
  - `motion_command`
  - `aim_command`
  - `fire_command`
- `GimbalRole` 已消费 `robot_mode + aim_command` 并发布 `gimbal_state`
- `ShootRole` 已消费 `robot_mode + fire_command` 并发布 `shoot_state`
- `ChassisRole` 已消费 `robot_mode + motion_command` 并发布 `chassis_state`
- `TelemetryRole` 已聚合状态并发布 `system_health`

### 4.2 当前缺口

- `remote` 仍是 `KEY` 占位，不是真实输入链
- `imu` 尚未接入 `Role`
- `motor` 尚未接入 `Role`
- 当前 `gimbal/chassis/shoot` 仍是命令镜像，不是执行器闭环

## 5. fan-out 规划

## Flow A: Remote Input Flow

### 定位

`remote` 不应直接成为顶层业务模块，而应拆成：

- 一个 `Module`：负责遥控器/键鼠协议接收、解码、在线性管理
- 一个或多个 `Topic schema`：如果需要可先定义内部输入状态
- `InputRole`：消费该模块输出，再统一写入 `OperatorInputSnapshot`

### 推荐落点

- `Modules/RemoteControl/...`
- `App/Roles/InputRole.*`
- 如确有必要，可补 `App/Topics/OperatorIntent`，但当前建议先不要新增公共 Topic

### 最小子任务

1. 新建 `RemoteControl` 模块骨架
2. 在模块内完成 UART 查找、接收状态、在线性/丢链语义
3. 让模块输出稳定的输入状态接口
4. 修改 `InputRole`，把 `KEY` 占位逻辑替换为读取 `RemoteControl`
5. 保留 `KEY` 作为 fallback 或调试输入，可选

### 前置依赖

- 确认目标遥控器/输入链协议
- 确认板级接线使用的 UART alias

### 验收

- `InputRole` 不再直接依赖 `KEY`
- `robot_mode / motion_command / aim_command / fire_command` 能被真实输入驱动
- 丢链后能回落到 `safe`

## Flow B: IMU Perception Flow

### 定位

`imu` 更像：

- `Module`
- `Topic schema`
- 被 `GimbalRole / ChassisRole` 消费

当前仓库已经有 `Modules/BMI088`，因此优先不是“新写一个 imu 模块”，而是先决定如何复用 BMI088 及其输出。

### 推荐落点

- 优先复用 `Modules/BMI088`
- 如当前 Topic 不够用，可补新的姿态/惯导 schema，但应谨慎新增公共 Topic
- `GimbalRole` 消费姿态/角速
- `ChassisRole` 按需消费 yaw/姿态参考

### 最小子任务

1. 审核并确定 `BMI088` 模块当前输出的 Topic 和数据结构
2. 定义 `Role` 侧最小所需的观测接口
3. 修改 `GimbalRole`，让其不再只镜像 `aim_command`，而是基于姿态反馈构造 `gimbal_state`
4. 修改 `ChassisRole`，视需要使用 yaw 参考而不是永远 `yaw_deg = 0.0f`

### 前置依赖

- 确认 `BMI088` 模块实例化方式
- 确认当前板子对应的硬件别名和线程/Topic 配置

### 验收

- `gimbal_state` 来自真实姿态数据而不是纯命令镜像
- `chassis_state.yaw_deg` 可来自真实观测
- 不把 BMI088 的内部控制/温控逻辑泄漏到 `Role`

## Flow C: Motor Execution Flow

### 定位

`motor` 不应写进 `Role`，而应拆成：

- 执行器 `Module`
- 可选控制器/反馈适配层
- `Role` 只消费高层执行器接口

当前 donor 的 `motor` 是最容易把 C 风格坏味道带进来的区域，因此必须严格分层。

### 推荐落点

- `Modules/Motor/...` 或按电机族拆模块
- `ChassisRole / GimbalRole / ShootRole` 只持有或消费执行器抽象

### 最小子任务

1. 先选一条最小真实执行闭环
2. 优先建议从 `gimbal` 开始，其次 `shoot`，最后 `chassis`
3. 为目标链路抽出执行器接口与状态接口
4. 让对应 `Role` 从“命令镜像”升级为“命令 -> 执行器目标 -> 状态反馈”

### 推荐优先级

1. `gimbal motor`
2. `shoot motor`
3. `chassis motor`

原因：

- `gimbal` 的输入/输出边界最清晰
- `shoot` 次之
- `chassis` 最后，因为它通常还会牵涉功率限制、裁判系统和运动学耦合

### 验收

- 至少一条链路从“命令镜像”升级为“真实执行器闭环”
- `Role` 不直接接触 CAN/UART/HAL 细节
- 电机控制器与业务决策没有重新缠死

## Flow D: Integration & Constraint Flow

### 定位

这一流不实现能力，而是负责守住边界。

### 需要持续检查的内容

- 是否有新代码误放进 `App` 而实际应是 `Module`
- 是否有模块开始反向依赖 `AppRuntime` 或具体 `Role`
- 是否有 Topic schema 被协议帧或 HAL 字段污染
- 是否有 `TelemetryRole`、`DecisionRole` 越长越像新的超级模块

### 前置门槛

- 每接入一条真实链路前，先判定该代码是：
  - `Role`
  - `Module`
  - `Topic schema`

### 验收

- 新实现仍满足 `User -> App -> Modules -> LibXR`
- 新模块保持 `LibXR::Application` 风格
- `cmake --build --preset Debug` 持续通过

## 6. 并行/串行顺序

### 可并行

- `remote` 分析与 `imu` 分析可并行
- `imu` 分析与 `motor` 分析可并行
- `remote Module` 与 `BMI088 消费改造` 在不同写入域下可并行

### 必须串行

- 在开始真实 `motor` 接入前，应先明确 `imu` 与 `remote` 输出契约
- `gimbal` 真实闭环优先于 `chassis`
- `Integration & Constraint Flow` 必须贯穿始终，不能后补

### 推荐顺序

```text
Step 1: remote 接入规划冻结
Step 2: imu 接入规划冻结
Step 3: motor 最小真实执行链选择冻结
Step 4: 先做 remote Module + InputRole
Step 5: 再做 BMI088 -> GimbalRole
Step 6: 再做 gimbal motor 最小真实闭环
Step 7: 再扩展到 shoot / chassis
```

## 7. 可直接下发的下一轮任务建议

### Task A: 真实 remote 接入规划与最小替换

**Files**
- 主要涉及：`Modules/` 新建 remote 模块、`App/Roles/InputRole.*`

**Goal**
- 用真实输入模块替换 `KEY` 占位输入

**DoD**
- `InputRole` 不再依赖 `KEY`
- 丢链自动回 `safe`
- `robot_mode / motion_command / aim_command / fire_command` 可被真实输入驱动

### Task B: BMI088 接入 Role 消费规划

**Files**
- 主要涉及：`Modules/BMI088/*`、`App/Roles/GimbalRole.*`、`App/Roles/ChassisRole.*`

**Goal**
- 让姿态状态来自真实 IMU，而不是命令镜像

**DoD**
- `gimbal_state` 至少部分字段来自真实观测
- `chassis_state.yaw_deg` 不再固定 `0.0f`

### Task C: 第一条真实 motor 闭环规划

**Files**
- 主要涉及：`Modules/Motor/*` 或 donor motor 迁移目标、`App/Roles/GimbalRole.*`

**Goal**
- 让 `gimbal` 先从命令镜像升级为真实执行器闭环

**DoD**
- `GimbalRole` 不再只镜像 `aim_command`
- 至少一条真实执行器路径跑通

## 8. 主代理 fan-in 结论

本轮 fan-out 规划的总判断是：

- **先接 `remote`**
  原因：当前输入链仍是假输入，所有后续真实闭环都建立在它之上。
- **再接 `imu`**
  原因：`gimbal/chassis` 的状态不能一直靠命令镜像。
- **再接 `motor`**
  原因：执行器闭环应建立在真实输入和真实观测都已确定之后。

也就是说，后续真正的实现主线应是：

```text
remote
-> imu
-> gimbal motor
-> shoot motor
-> chassis motor
```

而不是一开始就同时硬接三条真实链路。这样更稳，也更符合当前 C++ 工程已经冻结下来的 `App / Modules / Topics` 边界。
