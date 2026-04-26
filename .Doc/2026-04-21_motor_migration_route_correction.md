# Basic_framework 电机移植路线纠偏总结

## 1. 文档目的

本文档用于正式记录当前 `basic_framework -> XRobot` 迁移中已经确认的一次架构路线错误，
并冻结后续修正方向，避免继续沿错误路线扩展。

本次纠偏聚焦于：

- `motor` 相关能力应该如何落到 `Modules/`
- `App/Role` 与 `Modules/` 的真实职责边界
- 当前哪些文件属于错误方向
- 后续应该先做什么

---

## 2. 本次确认的错误

当前仓库曾以下列文件体现错误迁移路线；这两个文件已在 `DJIMotor` 通用层完成落地、`GimbalRole / ChassisRole / ShootRole` 完成回绑后正式删除：

- `Modules/GM6020GimbalMotor/GM6020GimbalMotor.hpp`
- `Modules/MecanumChassisDrive/MecanumChassisDrive.hpp`

问题不在于“文件能不能工作”，而在于它们把 **APP 层控制器职责** 下沉进了 `Modules/`。

具体表现：

### 2.1 `GM6020GimbalMotor.hpp` 的错误点

- 直接订阅 `AimCommand`
- 直接订阅 `RobotMode`
- 直接订阅 IMU Topic
- 在模块内做 yaw 外环/内环控制
- 在模块内决定使能/停机逻辑
- 直接构造并发送电机控制 CAN 帧

这已经不是“通用电机模块”，而是“云台 yaw 控制器”。

### 2.2 `MecanumChassisDrive.hpp` 的错误点

- 直接订阅 `MotionCommand`
- 直接订阅 `RobotMode`
- 在模块内做底盘输出使能判定
- 在模块内做麦轮逆解
- 在模块内维护底盘控制命令缓存
- 在模块内把 `motion_command` 直接转成执行器输出

这已经不是“底层执行器接口”，而是“底盘控制器”。

---

## 3. 为什么这是错误路线

根据当前项目已经冻结的边界：

- `Modules/` 只负责可复用能力
- `App/` 才负责角色化业务编排

因此：

- `Modules/` 可以知道“电机怎么初始化、怎么解码反馈、怎么发一组 CAN 控制帧”
- `Modules/` 不应该知道“机器人现在是不是 safe、当前是云台模式还是底盘模式、应该怎么从 AimCommand/MotionCommand 推导控制行为”

换句话说：

### 模块层应该知道的

- 电机类型
- 电机 ID
- 反馈报文格式
- 闭环参数
- 分组发帧规则
- 统一 `Enable / Stop / SetRef / SetOuterLoop / UpdateFeedback`

### 模块层不应该知道的

- `AimCommand`
- `MotionCommand`
- `FireCommand`
- `RobotMode`
- “当前该不该发射 / 该不该跟随 / 该不该停机”
- “底盘速度该如何根据模式变化”

这些都属于 `App/Role`。

---

## 4. donor 的正确迁移对象其实是什么

本次复核后，正确的模块层移植目标不是：

- `云台控制器 module`
- `底盘控制器 module`

而是 donor 的：

- `DJIMotorInit`
- `DJIMotorSetRef`
- `DJIMotorChangeFeed`
- `DJIMotorStop`
- `DJIMotorEnable`
- `DJIMotorOuterLoop`
- `DJIMotorControl`
- 反馈解码
- 分组发帧

也就是 **完整的通用 `DJIMotor` 能力层**。

donor 中它的角色本质是：

- 提供通用电机实例
- 提供通用反馈解码
- 提供通用闭环计算
- 提供通用发帧逻辑

上层应用只负责：

- 创建实例
- 选择闭环目标
- 设定参考值
- 根据业务模式决定何时启停

这正符合当前仓库的 `Module -> Role` 理想边界。

---

## 5. 修正后的正确路线

## Step 1: 先把 `DJIMotor` 通用能力迁入 `Modules/`

目标：

- 在 `Modules/` 中形成通用 DJI 电机模块层

最小能力应包括：

- 电机配置结构
- 电机实例
- 反馈解码
- 分组发帧
- `SetRef / Enable / Stop / OuterLoop / ChangeFeed`
- 统一 `OnMonitor()` 控制循环

注意：

- 这一步不应该直接理解 `AimCommand/MotionCommand/FireCommand`

## Step 2: 让 `App/Role` 消费通用电机模块

目标：

- `GimbalRole` 编排 yaw/pitch 控制
- `ChassisRole` 编排底盘运动学与模式
- `ShootRole` 编排单发/连发/卡弹/摩擦轮策略

注意：

- `Role` 不直接碰 HAL
- `Role` 不直接写底层发帧细节
- `Role` 通过通用电机模块接口驱动执行器

## Step 3: 废弃当前错误方向的伪控制器模块

对象：

- `Modules/GM6020GimbalMotor/GM6020GimbalMotor.hpp`
- `Modules/MecanumChassisDrive/MecanumChassisDrive.hpp`

处理原则：

- 在通用 `DJIMotor` 模块接好并完成 Role 回绑前，只允许冻结，不允许继续扩展
- 一旦 `AppRuntime` 完成新装配路径切换，应立即删除，不再保留为仓库内参考实现
- 当前状态：**已完成删除**

---

## 6. 后续需要做的任务

### Task A: 设计通用 DJI 电机模块边界

目标：

- 定义 `DJIMotor` 在当前工程里的 C++ / LibXR 版本边界

需要冻结：

- 配置结构
- 电机实例结构
- 反馈结构
- 分组发送结构
- 控制循环入口
- `Enable / Stop / SetRef / OuterLoop / ChangeFeed` 接口

### Task B: 把 donor `DJIMotor` 的通用能力迁入 `Modules/`

目标：

- 完整迁移可复用能力，不迁移 donor 的 APP 层逻辑

应优先迁入：

- 反馈解码
- 多电机分组发送
- 通用闭环控制

### Task C: 重绑 `GimbalRole`

目标：

- 从“直接依赖 `GM6020GimbalMotor` 控制器模块”改为“消费通用 `DJIMotor` 实例”

### Task D: 重绑 `ShootRole`

目标：

- 让摩擦轮和拨盘都通过通用 `DJIMotor` 模块层驱动

### Task E: 重绑 `ChassisRole`

目标：

- 让底盘四轮通过通用 `DJIMotor` 模块层驱动
- `Mecanum` 逆解留在 `ChassisRole` 或 `App/` 下的控制编排层，不留在 `Modules/`

### Task F: 删除/废弃错误路线文件

目标：

- 在新通用模块接好后，逐步废弃：
  - `GM6020GimbalMotor.hpp`
  - `MecanumChassisDrive.hpp`

---

## 7. 当前冻结的禁令

从本文档起，后续迁移必须遵守：

- 禁止继续把 `AimCommand / MotionCommand / FireCommand / RobotMode` 直接喂给 `Modules/` 中的电机类
- 禁止继续在 `Modules/` 中新增“具体业务控制器”
- 禁止以“先跑起来”为理由继续扩大错误边界
- 禁止把 donor 的 APP 控制逻辑伪装成 `LibXR::Application` 后塞进 `Modules/`

允许：

- 在 `Modules/` 中保留真正通用、可跨角色复用的电机能力
- 在 `App/Role` 中保留业务模式、控制策略、状态机和跨模块协调

---

## 8. 结论

这次确认的核心不是“实现细节不理想”，而是：

**`motor` 迁移层级选错了。**

正确修正方式不是继续打补丁，而是：

**把 donor 的 `DJIMotor` 通用能力完整迁入 `Modules/`，然后让 `App/Role` 去消费它。**

只有这样，后续：

- `gimbal`
- `shoot`
- `chassis`

三条链路才会重新回到当前工程已经冻结的：

```text
User -> App -> Modules -> LibXR
```

的正确边界上。
