# 2026-04-22 剩余 Fan-in / Fan-out 迁移执行计划

## 1. 文档目的

本文档用于在当前 `road_map.md` 已冻结长期方向、且 `DT7 / VT13 / referee / master_machine / SharedTopicClient / DJIMotor` 第一轮接入已经落地的基础上，
继续把 **basic_framework -> XRobot** 的剩余迁移工作拆成可直接执行的后续批次。

这份文档的目标不是重复历史阶段，而是：

- 只规划 **当前真实还没完成** 的迁移任务
- 继续按 `fan-in / fan-out` 主线拆分
- 为后续实现提供明确依赖顺序、边界和验收标准
- 避免下一轮实现继续沿用已经过期的“先创建模块骨架”思路

---

## 2. 当前重排后的基线判断

基于当前仓库与现有文档，可以确认：

- `Flow A` 第一轮输入接入已经完成：
  - `DT7 primary`
  - `optional DT7 secondary`
  - `VT13`
  - `master_machine` 最小白名单覆盖
- `Flow B` 第一轮观测/约束接入已经完成：
  - `BMI088`
  - `referee` 第一轮稳定语义
- `Flow E` 第一轮执行器归一化已经完成：
  - `DJIMotor` 通用层
  - `GimbalRole / ChassisRole / ShootRole` 回绑
- `Flow F` 第一轮桥接白名单已经完成：
  - `SharedTopicClient` 状态输出白名单
  - `master_machine` 输入白名单
- `Flow C + Flow D` 已存在半成品：
  - `HealthAggregator`
  - `DegradedPolicy`
  - `TelemetryRole` / `DecisionRole` 第一轮接线

因此，后续不应再按“创建 Referee / CANBridge / MasterMachine / health helper”的旧顺序推进，
而应改为围绕下面 5 个剩余批次继续收口：

1. `Batch 1`: `SystemHealth` 契约补齐与降级动作显式化
2. `Batch 2`: `InputRole` 输入扇入边界收口
3. `Batch 3`: `CANBridge` 真实协议闭环
4. `Batch 4`: `referee` 第二轮稳定语义
5. `Batch 5`: bring-up 清理与 `.ioc` / 文档同步

---

## 3. 总体依赖顺序

后续建议严格按下面顺序推进：

```text
Batch 1: Flow C + Flow D
-> Batch 2: Flow A
-> Batch 3: Flow F
-> Batch 4: Flow B
-> Batch 5: Phase 6 cleanup
```

原因如下：

- 只有先把 `system_health -> degraded action` 契约收口，后续 `InputRole`、`CANBridge`、`referee` 才不会继续散写第二套判断。
- `InputRole` 应在当前第一轮接入成功后尽快收紧边界，否则后续 `VT13.mouse / key`、`automation`、`host assist` 很容易直接冲进主链。
- `CANBridge` 的在线/离线与桥接治理必须接在统一 health 契约之后，否则 bridge 语义会再次散落。
- `referee` 第二轮语义应该建立在统一健康树与统一降级动作之上，而不是再次直接堆进 `DecisionRole`。
- `.ioc` 同步、bring-up 清理和文档收口必须后置，避免在主契约尚未冻结时过早固化临时实现。

---

## 4. Batch 1: `SystemHealth` 契约补齐

### 主线定位

- `fan-in`: `Flow C: Health Fan-in`
- `fan-out`: `Flow D: Decision Fan-out`

### 目标

把当前已经存在的 `HealthAggregator / DegradedPolicy` 从“内部 helper + 第一轮接线”推进为真正稳定的跨角色契约。

### Fan-in 输入

- `control link` 在线性
- `imu` 在线性
- `actuator` 在线性
- `referee` 在线性
- `master_machine` 在线性
- `can_bridge` 在线性

### Fan-out 输出

- `system_health.primary_reason`
- `system_health.control_action_mask`
- `DecisionRole` 的统一降级动作
- 更明确的 `robot_mode` 降级表达

### 任务卡

#### Card 1.1: 补齐 `SystemHealth` 稳定字段

**涉及文件**

- `App/Topics/SystemHealth.hpp`
- `App/Runtime/HealthAggregator.hpp`
- `App/Runtime/DegradedPolicy.hpp`

**动作**

- 增加 `primary_reason`
- 增加 `control_action_mask`
- 明确 `fault_severity / recovery_hint / degraded_source_mask` 的口径
- 保持 Topic 名称不变，不新增第二个 health topic

**验收**

- `system_health` 能表达“为什么 degraded”
- `system_health` 能表达“应该限制什么”
- 不引入第二套健康摘要结构

#### Card 1.2: 显式化 health-to-action 映射

**涉及文件**

- `App/Roles/TelemetryRole.cpp`
- `App/Roles/DecisionRole.cpp`
- `App/Config/HealthPolicyConfig.hpp`
- `App/Config/DegradedModeConfig.hpp`

**动作**

- 把“来源离线 -> 动作限制”的规则集中到 helper / config
- 明确区分：
  - `force_safe`
  - `limit_motion`
  - `block_fire`
  - `bridge_only_warning`
- 禁止角色内部继续长局部 if/else 分叉

**验收**

- `TelemetryRole` 与 `DecisionRole` 理解同一套原因字典
- 修改降级规则时，改动点集中

#### Card 1.3: 补一份板上验证清单

**涉及文件**

- 新增或更新健康联调文档

**动作**

- 列出输入失联、IMU 失联、执行器失联、裁判系统失联、bridge 失联的期望输出
- 写明 `system_health` 与 `robot_mode` 应如何变化

**验收**

- 后续板上联调可以直接复用

### 明确不做

- 不在本批次扩展 `InputRole`
- 不在本批次实现 `CANBridge` 真实协议
- 不在本批次补 `referee` 第二轮详细规则

---

## 5. Batch 2: `InputRole` 输入扇入边界收口

### 主线定位

- `fan-in`: `Flow A: Operator Intent Fan-in`

### 目标

让 `InputRole` 稳定停在“来源选择 + 最小映射”的层级，避免继续膨胀成协议细节和未来自动化输入的堆积点。

### Fan-in 输入

- `DT7 primary`
- `DT7 secondary`
- `VT13`
- `master_machine` 白名单字段

### Fan-out 输出

- 单一 `OperatorInputSnapshot`
- 明确的 active input 选择逻辑
- 可解释但仍私有的选择原因 / 来源状态

### 任务卡

#### Card 2.1: 提炼输入映射 helper

**涉及文件**

- `App/Roles/InputRole.cpp`
- `App/Roles/InputRole.hpp`
- `App/Runtime/OperatorInputSnapshot.hpp`
- 可选新增：
  - `App/Runtime/InputMappingHelpers.hpp`
  - 或 `App/Runtime/InputMapping/*.hpp`

**动作**

- 把 `DT7State -> OperatorInputSnapshot` 的映射抽出
- 把 `VT13State -> OperatorInputSnapshot` 的映射抽出
- 让 `InputRole` 只做：
  - 拉取 state
  - 选择 active source
  - 调用 helper 写 snapshot

**验收**

- `InputRole` 不再长期驻留大段协议特化映射代码
- `OperatorInputSnapshot` 契约不扩张

#### Card 2.2: 冻结来源仲裁字典

**涉及文件**

- `App/Roles/InputRole.cpp`
- `App/Roles/InputRole.hpp`

**动作**

- 明确 active source 枚举
- 明确 selection reason 枚举
- 保留至少以下私有诊断量：
  - 当前激活源
  - 最近更新时间
  - 各输入源在线性
  - safe/manual 请求状态

**验收**

- 第一版多输入源接入具备可解释性
- 后续自动化输入扩展有结构落点

#### Card 2.3: 冻结“暂不进入主链”的高级语义

**涉及文件**

- `App/Roles/InputRole.cpp`
- `.Doc/road_map.md`

**动作**

- 显式保持以下语义暂不进入主链：
  - `VT13.mouse`
  - `VT13.key`
  - `target_wz`
  - `track_target`
- 把“不做字段级混控 / 不做多源求和”继续写明

**验收**

- 后续不会因为新输入协议接入而隐式重构控制链

### 明确不做

- 不新建 `RemoteMux`
- 不改 `DecisionRole / MotionCommand / AimCommand / FireCommand`
- 不做多来源求和

---

## 6. Batch 3: `CANBridge` 真实协议闭环

### 主线定位

- `fan-in`: `Flow C` 的 bridge health source
- `fan-out`: `Flow F: Telemetry / Bridge Fan-out`

### 目标

把当前只记录在线活动的 `CANBridge` 升级为“最小真实桥接模块”，只迁移 donor `can_comm` 的通用桥接能力，不把 donor 的 C 风格业务对象模型搬进来。

### Fan-in 输入

- 内部待桥接状态
- whitelist frame ID
- bridge rx/tx activity

### Fan-out 输出

- bridge rx/tx 统计
- 在线状态与超时
- 最小打包 / 解包闭环
- 接入 `system_health`

### 任务卡

#### Card 3.1: 冻结 bridge frame 边界

**涉及文件**

- `App/Config/BridgeConfig.hpp`
- `Modules/CANBridge/CANBridge.hpp`

**动作**

- 明确哪些 CAN ID 属于 bridge
- 明确哪些不是执行器控制帧
- 明确发送 / 接收各自的 whitelist
- 明确 bridge direction 与 health 影响级别

**验收**

- `CANBridge` 的职责边界冻结
- 不因 bridge 协议回改内部 Topic schema

#### Card 3.2: 补齐最小真实闭环

**涉及文件**

- `Modules/CANBridge/CANBridge.hpp`

**动作**

- 增加最小打包 / 解包
- 增加校验与统计
- 区分 rx 活动、tx 活动、online 状态
- 为后续上板调试保留必要但轻量的诊断量

**验收**

- `CANBridge` 不再只是在线状态骨架
- 仍保持纯模块边界

#### Card 3.3: 把 bridge health 接回 `TelemetryRole`

**涉及文件**

- `App/Roles/TelemetryRole.cpp`
- `App/Roles/TelemetryRole.hpp`

**动作**

- 将 bridge 的真实在线状态纳入统一 health 树
- 明确 bridge offline 是否只告警，还是会触发控制限制

**验收**

- bridge health 与本地 degraded 的关系显式化

### 明确不做

- 不把 bridge 协议解析塞进 `DecisionRole`
- 不把 Role 变成帧编解码器

---

## 7. Batch 4: `referee` 第二轮稳定语义

### 主线定位

- `fan-in`: `Flow B: Perception & Constraint Fan-in`

### 目标

在第一轮最小稳定语义已经接入后，继续补齐比赛约束，但只补“可长期存在的领域语义”，不碰 UI 和协议细枝末节。

### Fan-in 输入

- 比赛阶段
- 功率预算
- 热量窗口
- 允许开火条件

### Fan-out 输出

- 稳定的 `DecisionRole` 约束输入
- 更完整的 `TelemetryRole` 状态摘要
- 与 `DegradedPolicy` 对齐的外部约束

### 任务卡

#### Card 4.1: 冻结第二轮稳定语义清单

**涉及文件**

- `App/Config/RefereeConfig.hpp`
- `Modules/Referee/Referee.hpp`

**动作**

- 明确哪些字段属于长期稳定语义
- 明确哪些字段仍保留在模块私有协议层

**验收**

- 不把原始裁判协议对象抬升进 `App/Topics`

#### Card 4.2: 回绑到 `DecisionRole`

**涉及文件**

- `App/Roles/DecisionRole.cpp`
- `App/Roles/TelemetryRole.cpp`

**动作**

- 让 `DecisionRole` 只消费稳定语义
- 与现有 `DegradedPolicy` 对齐
- 禁止额外长出第二套 `fire_allow / referee offline` 判定

**验收**

- 规则入口集中
- `DecisionRole` 不直接理解底层协议格式

### 明确不做

- 不补 referee UI
- 不扩展协议展示杂项

---

## 8. Batch 5: bring-up 清理与交付收口

### 主线定位

- 收口任务，跨 `Flow A/B/C/F`

### 目标

在主契约稳定后，把当前 bring-up 过程中留下的手工接线、命名兼容和文档偏差统一收口。

### 任务卡

#### Card 5.1: `.ioc` 与板级接线同步

**涉及文件**

- `basic_framework.ioc`
- `Core/Src/usart.c`
- `Core/Inc/usart.h`
- `Core/Src/stm32f4xx_it.c`
- `Core/Src/main.c`
- `User/app_main.cpp`

**动作**

- 把 `USART2 -> VT13` 的现有手工启用同步回 `.ioc`
- 确保重新生成代码后接线不丢

**验收**

- CubeMX 再生成后，`uart_vt13` 仍可恢复

#### Card 5.2: 清理临时命名与保守默认值

**涉及文件**

- `App/Config/*.hpp`
- `App/Roles/*.cpp`
- `User/app_main.cpp`
- `.Doc/road_map.md`

**动作**

- 清理旧命名残留
- 清理只为 bring-up 存在的临时常量和兼容路径
- 保留必要注释，标明仍是临时实现的部分

**验收**

- 文档、代码、硬件接线三者一致

#### Card 5.3: 产出最终交接材料

**涉及文件**

- `.Doc/road_map.md`
- `task_plan.md`
- `progress.md`
- `findings.md`

**动作**

- 把最新真实边界写回路线图
- 保留后续主线、禁令、验证清单和已知 warning

**验收**

- 下一轮会话可以不回溯历史，也能直接接手推进

---

## 9. 推荐并行度

允许的并行方式如下：

- `Batch 1 / Card 1.1` 与 `Card 1.3` 可并行
- `Batch 2 / Card 2.1` 与 `Card 2.2` 可并行
- `Batch 3 / Card 3.1` 完成后，`Card 3.2` 与 `Card 3.3` 可并行
- `Batch 4` 必须在 `Batch 1` 之后
- `Batch 5` 必须最后执行

不建议并行的组合：

- `Batch 1` 与 `Batch 3` 不建议并行，因为 bridge health 影响级别依赖统一降级契约
- `Batch 2` 与 `Batch 4` 不建议并行，因为两者都可能触碰 `DecisionRole` 的输入假设

---

## 10. 退出条件

当这份计划被执行完成时，应该满足：

- `SystemHealth` 已能稳定表达 primary degraded reason 与 control action
- `TelemetryRole` 成为唯一 health fan-in 汇聚出口
- `DecisionRole` 成为唯一 degraded action fan-out 出口
- `InputRole` 停在“来源选择 + 最小映射”边界
- `CANBridge` 具备最小真实桥接闭环
- `referee` 第二轮语义已补齐，但仍保持 `Module -> Role` 单向依赖
- `.ioc`、代码与路线图保持一致

如果后续任务不再满足这些退出条件，就说明迁移正在重新偏回 donor 平移式思路，需要先回到本计划重新校准。

---

## 12. Batch 9: donor 二次裁剪

### 主线定位

- 收口任务，作用于 donor 剩余内容的范围治理

### 目标

把 donor 剩余内容正式分成三类：

1. 继续迁移
2. 只迁义，不迁实现
3. 明确放弃

这一步的目标不是再开新实现，而是结束“按 donor 原目录惯性推进”的状态。

### 12.1 继续迁移

#### 应用层继续迁移

- `application/chassis/*`
  继续迁移其中尚未被当前 [ChassisRole.cpp](F:/Xrobot/Xro_template/App/Roles/ChassisRole.cpp) 吃干净的底盘执行语义：
  - 麦轮正逆解的剩余细节
  - 云台系到车体系速度投影
  - `follow / independent / spin / stop` 模式切换
  - 基于 IMU/轮速的状态估计
- `application/gimbal/*`
  继续迁移云台执行语义：
  - `yaw / pitch` 双轴编排
  - IMU 外反馈闭环
  - 限位
  - 重力补偿
  - 零力 / 保持 / 手动瞄准切换
- `application/shoot/*`
  继续迁移发射执行语义：
  - 单发 / 连发 / 反转 / 三连发装填器状态机
  - 摩擦轮目标弹速映射
  - 卡弹检测与恢复

#### 模块层继续迁移

- `modules/referee/*`
  仅继续迁“裁判协议接收与约束提炼”层，落点仍是当前 [Referee.hpp](F:/Xrobot/Xro_template/Modules/Referee/Referee.hpp)
- `modules/master_machine/*`
  仅继续迁“需要兼容现役上位机/视觉协议时的 ingress 解析层”，落点仍是当前 [MasterMachine.hpp](F:/Xrobot/Xro_template/Modules/MasterMachine/MasterMachine.hpp)
- `modules/can_comm/*`
  仅在后续出现真实跨板业务载荷需求时继续迁，并且只能并入当前 [CANBridge.hpp](F:/Xrobot/Xro_template/Modules/CANBridge/CANBridge.hpp) 能力线
- `modules/super_cap/*`
  建议继续迁，但必须按“通用模块能力 + 私有状态 Topic”重写，优先汇入 `Telemetry / Decision / SystemHealth`
- `modules/motor/DMmotor`、`HTmotor`、`LKmotor`、`servo_motor`
  仅在后续机型确有硬件需求时继续迁，而且必须完全对齐当前 [DJIMotor.hpp](F:/Xrobot/Xro_template/Modules/DJIMotor/DJIMotor.hpp) 的归一化路线

### 12.2 只迁义

#### 应用层只迁义

- `application/cmd/robot_cmd.c`
  只迁：
  - 输入映射表
  - 模式切换表
  - 急停条件表
  迁移落点只能是 [InputRole.cpp](F:/Xrobot/Xro_template/App/Roles/InputRole.cpp) 与 [DecisionRole.cpp](F:/Xrobot/Xro_template/App/Roles/DecisionRole.cpp)
- `application/robot_def.h`
  只迁命令语义与 mode 字典，不迁结构体原型
- `application/robot.c / robot.h / robot_task.h`
  只迁“先感知、再执行、再编排”的顺序语义，不迁初始化框架

#### 模块层只迁义

- `modules/daemon/*`
  只迁“在线 / 离线 / 超时 / 恢复”语义，不迁实现
- `modules/remote/*`
  只迁“DBUS/键鼠字段解释”和控制输入语义，不迁 donor 的组织方式
- `modules/message_center/*`
  只迁“话题拆分思想”，不迁实现
- `modules/master_machine` 的回传部分
  只迁“上位机协同字段白名单”和“人工接管语义”，不迁历史耦合的姿态/弹速/模式回发方式
- `modules/referee/referee_task`、`referee_UI`
  只迁展示层语义，不迁任务/UI 实现
- `modules/imu/ins_task`
  只迁“BMI088 原始数据 -> 姿态解算 -> 温控 -> 上层消费”的语义链，不迁 donor 源码
- donor `BMI088`
  只迁校准、温控、触发采样、姿态前置处理经验，不迁第二套 IMU 栈
- `modules/algorithm/*`
  只迁控制与估计思想，不迁整包源码
- `modules/motor/motor_def` 与 `power_control`
  只迁 outer-loop / feedback-source / feedforward 分层语义，以及功率约束语义，不迁 donor 写法

### 12.3 明确放弃

#### 应用层放弃

- `application/robot.c / robot_task.h` 的 `RobotInit / RobotTask / OSTaskInit` 组织方式
- `robot_cmd.c` 的“单文件总控器”实现形态
- `ONE_BOARD / GIMBAL_BOARD / CHASSIS_BOARD` 这类靠编译宏切应用拓扑的组织方式
- `application/chassis/balance.h`
- `application/chassis/steering.h`

#### 模块层放弃

- `modules/message_center` 作为独立模块实现
- `modules/daemon` 作为全局 watchdog 框架实现
- `modules/standard_cmd`
- `modules/unicomm`
- `modules/alarm` donor 版本
- `modules/bluetooth/HC05`
- `modules/oled`
- `modules/TFminiPlus`
- `modules/encoder`
- `modules/ist8310`
- `modules/motor/step_motor`
- `modules/motor/motor_task`

### 12.4 冻结结论

Batch 9 之后，donor 剩余内容的主线应正式收敛为：

```text
只保留“通信模块 + Role 编排 + 健康聚合”这条迁移主线
```

更具体地说，后续真正还值得进 backlog 的只剩：

1. `Referee` 的真实帧覆盖与约束稳定化
2. `MasterMachine` 与现役上位机协议字段对齐
3. `CANBridge` 在真实跨板业务需求出现后的业务载荷扩展
4. `ChassisRole / GimbalRole / ShootRole` 对 donor 应用控制语义的继续吸收
5. `super_cap` 与“确有硬件需求时”的非 DJI 通用电机模块

而以下内容从现在开始应视为正式移出主迁移视角：

- 第二套消息系统
- 第二套 watchdog
- 第二套命令抽象
- donor 的 UI / 展示实现
- donor 的外设杂项与未完成模块

---

## 13. Batch 10: super_cap 第一阶段接入规划

### 主线定位

- `fan-in`: 外部电源/能量模块状态进入系统观测链
- `fan-out`: `TelemetryRole / SystemHealth` 的健康与遥测摘要

### 用户已确认的硬约束

本批次必须严格遵守以下 4 条约束：

1. 协议来源固定为：
   `D:\RoboMaster\HeroCode\Code\Chassis\modules\super_cap`
2. 物理链路固定接到 `CAN1`
3. 当前阶段只把它作为一个简单通信 `Module`
4. 当前阶段只做“可观测模块”：
   先把数据稳定读出来，再考虑把数值接进控制链路

因此，本批次**不是**功率控制迁移，也**不是**底盘功率闭环迁移。

### 13.1 Fan-in 输入

来自 donor `super_cap` 协议层的最小输入语义为：

- `error_code`
- `chassis_power`
- `chassis_power_limit`
- `cap_energy`

这些字段来自 donor 的 `SuperCap_Rx_Data_s`，属于模块私有协议态。

### 13.2 Fan-out 输出

当前阶段建议只 fan-out 两层：

#### 模块私有状态层

新增或等价提供一个 `SuperCapState`，用于本仓库内部消费，建议至少包含：

- `online`
- `error_code`
- `chassis_power_w`
- `chassis_power_limit_w`
- `cap_energy_percent`

这层是后续可能接入 `DecisionRole / ChassisRole / TelemetryRole` 的事实源。

#### 系统健康摘要层

当前阶段只建议把以下摘要进入系统级健康：

- `supercap_online`

若要扩更多摘要，优先考虑：

- `supercap_degraded`

但当前阶段不是必须。

### 13.3 明确边界

#### 当前阶段明确要做

- 在 `Modules/` 下新增或接入一个 XRobot / LibXR 风格的 `super_cap` 模块
- 固定使用 `CAN1`
- 固定兼容 donor `super_cap` 协议头和收发结构
- 先把接收态读出来并发布为模块私有强类型状态
- 让 `TelemetryRole / SystemHealth` 至少能看到 `supercap_online`

#### 当前阶段明确不做

- 不迁 donor `power_controller`
- 不把 `cap_energy / chassis_power_limit / chassis_power` 直接接进 `DecisionRole`
- 不把 `super_cap` 直接接进 `ChassisRole` 的功率分配
- 不引入第二套功率控制器
- 不因为 `super_cap` 接入而修改 `MotionCommand / ChassisState / FireCommand` 契约

### 13.4 设计落点建议

#### Module 层

推荐新增：

- `Modules/SuperCap/SuperCap.hpp`

职责：

- 通过 `HardwareContainer` 查找 `CAN1`
- 注册 CAN 接收回调
- 解析 donor `SuperCap_Rx_Data_s`
- 保存并发布 `SuperCapState`
- 提供最小安全发送接口，但当前阶段不由上层定时驱动

#### Telemetry / Health 层

推荐修改：

- `App/Roles/TelemetryRole.cpp`
- `App/Roles/TelemetryRole.hpp`
- `App/Topics/SystemHealth.hpp`

职责：

- 订阅 `SuperCapState`
- 先接 `supercap_online`
- 若后续觉得必要，再扩摘要级 `supercap_degraded`

#### Runtime 装配层

推荐修改：

- `App/Runtime/AppRuntime.hpp`
- `App/Runtime/AppRuntime.cpp`

职责：

- 实例化 `SuperCap`
- 把模块按 `context -> module -> role` 顺序接入

### 13.5 建议任务卡

#### Card S1: donor 协议结构迁移到本仓库模块边界

目标：

- 固化 `SuperCap_Rx_Data_s / SuperCap_Tx_Data_s`
- 保持和 donor 协议兼容
- 但不沿用 donor 的全局单例和 C 回调组织方式

#### Card S2: `SuperCap` 模块 first-pass 接入

目标：

- `CAN1` 上稳定接收反馈
- 发布模块私有 `SuperCapState`
- 提供安全默认发送接口但不接业务策略

#### Card S3: `TelemetryRole / SystemHealth` 接线

目标：

- fan-in `supercap_online`
- 先形成最小系统可观测性

#### Card S4: 板上验收清单

目标：

- 验证 CAN ID、收包、在线超时和状态发布
- 明确当前阶段不涉及功率控制闭环

### 13.6 验收标准

当前阶段完成的标准应为：

- `SuperCap` 模块已在 `AppRuntime` 中实例化
- `CAN1` 上能收到 donor 协议反馈
- 模块私有 `SuperCapState` 能表达 donor 反馈字段
- `TelemetryRole` 已接到 `supercap_online`
- `SystemHealth.supercap_online` 已可对外发布
- `cmake --build --preset Debug` 通过

### 13.7 下一阶段接口预留

当前阶段完成后，后续若继续推进 `super_cap`，下一阶段才允许讨论：

- 是否把 `cap_energy_percent` 变成系统级摘要
- 是否把 `chassis_power_limit_w` 接入 `DecisionRole / ChassisRole`
- 是否迁移 donor `power_controller`
- 是否让 `super_cap` 影响底盘功率模式

在这些问题被单独确认前，当前模块始终只是“可观测模块”。

---

## 11. 详细执行卡片

本节是在三路 subagent 并行细化后，由主线程按当前代码基线统一收口得到的执行卡片。
这些卡片优先解决“下一轮实现最容易再次走偏”的位置。

### 11.1 Batch 1: `SystemHealth` 契约补齐

#### Card B1-1: 给 `SystemHealth` 增补稳定字段，并先解头文件依赖闭环

**目标**

让 `system_health` 能稳定表达：

- 为什么 degraded
- 建议采取什么控制动作

**涉及文件**

- `App/Topics/SystemHealth.hpp`
- `App/Config/HealthPolicyConfig.hpp`
- `App/Runtime/HealthAggregator.hpp`

**关键符号**

- `App::SystemHealth`
- `App::HealthSourceMask`
- `App::Config::HealthPrimaryReason`
- `App::Config::ControlActionMask`

**必须遵守的约束**

- 当前 [HealthPolicyConfig.hpp](F:/Xrobot/Xro_template/App/Config/HealthPolicyConfig.hpp) 已包含 `SystemHealth.hpp`，因此不能简单让 `SystemHealth.hpp` 反向包含 `HealthPolicyConfig.hpp`，否则会形成 include 循环。
- `SystemHealth` 仍是唯一对外健康 Topic，不新增第二个 health topic。

**明确不做**

- 不新建新的 health/degraded helper
- 不新增新的 health topic
- 不在本批扩新 health source

**验收标准**

- `SystemHealth` 具备稳定字段：`primary_reason`、`control_action_mask`
- `fault_code` 与 `degraded_source_mask` 的关系被文档化并冻结口径

**建议验证**

- `cmake --build --preset Debug`
- `App/Runtime/HealthPolicy_compile_test.cpp` 补充或冻结“reason/action -> topic 字段”一致性断言

#### Card B1-2: 让 `TelemetryRole` 发布完整 health 契约

**目标**

让 `TelemetryRole` 成为唯一 health fan-in 汇聚出口，而不是只发布在线状态摘要。

**涉及文件**

- `App/Roles/TelemetryRole.cpp`
- `App/Runtime/HealthAggregator.hpp`
- `App/Topics/SystemHealth.hpp`

**关键符号**

- `TelemetryRole::OnMonitor()`
- `HealthAggregator::Evaluate(...)`
- `HealthAggregation`

**必须遵守的约束**

- `TelemetryRole` 只做聚合与发布，不在 Role 内重新散写降级规则
- 继续基于现有订阅源发布健康契约，不借机扩大角色职责

**明确不做**

- 不修改 `SharedTopicClient` topic 列表
- 不引入新的桥接输出 topic

**验收标准**

- 发布的 `SystemHealth` 同时包含：
  - 在线布尔
  - `online_source_mask / degraded_source_mask`
  - `fault_severity / recovery_hint`
  - `primary_reason / control_action_mask`

**建议验证**

- `cmake --build --preset Debug`
- 断开 `referee` 或 `bridge` 后，`system_health.primary_reason` 与 `control_action_mask` 应同步变化

#### Card B1-3: 让 `DecisionRole` 只消费契约，不再二次推导

**目标**

去掉 `DecisionRole` 对 `SystemHealth` 的二次推导，确保降级动作只来源于统一契约。

**涉及文件**

- `App/Roles/DecisionRole.cpp`
- `App/Runtime/DegradedPolicy.hpp`
- `App/Topics/SystemHealth.hpp`

**关键符号**

- `DecisionRole::ResolveDegradedPolicy()`
- `BuildDegradedPolicy(...)`
- `DegradedPolicyResult`

**必须遵守的约束**

- `DecisionRole` 继续是唯一 degraded action fan-out 出口
- 不在本批改变 `OperatorInputSnapshot` 或 `InputRole`

**明确不做**

- 不新建第二套 degraded topic
- 不让 `ChassisRole / GimbalRole / ShootRole` 直接订阅 `SystemHealth`

**验收标准**

- `ResolveDegradedPolicy()` 的输入只依赖 `system_health` 已公开的原因/动作字段
- `RobotMode.is_degraded / degraded_mode` 与 `system_health.primary_reason` 具有稳定对应关系

**建议验证**

- `cmake --build --preset Debug`
- 以“control link lost”场景验证 `should_force_safe` 来自 `control_action_mask`

#### Card B1-4: 冻结 health-to-action 口径与联调清单

**目标**

为后续 `CANBridge` 与 `referee` 扩展提供统一判断表，避免再次散点决策。

**涉及文件**

- `App/Runtime/HealthPolicy_compile_test.cpp`
- `.Doc/2026-04-22_remaining_fan_in_fan_out_execution_plan.md`
- `task_plan.md`

**必须覆盖的来源**

- `control link`
- `referee`
- `imu`
- `actuator`
- `bridge`

**验收标准**

- 存在“source lost -> primary_reason -> action_mask -> DecisionRole behavior”的统一表
- 编译期断言与文档口径保持一致

### 11.2 Batch 2: `InputRole` 边界收口

#### Card B2-1: 把协议映射从 `InputRole` 主体剥离成 helper

**目标**

让 `InputRole` 只保留：

- 拉取 state
- 选择 active source
- 写入 snapshot

**涉及文件**

- `App/Roles/InputRole.cpp`
- `App/Roles/InputRole.hpp`
- `App/Runtime/OperatorInputSnapshot.hpp`
- 可选新增：
  - `App/Runtime/InputMappingHelpers.hpp`
  - 或 `App/Runtime/InputMapping/*.hpp`

**关键符号**

- `InputRole::UpdateRemoteStates()`
- `InputRole::SelectActiveInput()`
- `InputRole::ApplyRemoteSnapshot(...)`
- `InputRole::ApplyVT13Snapshot(...)`

**必须遵守的约束**

- `OperatorInputSnapshot` 仍保持最小内部共享契约，不为了映射便利扩字段
- 输入模块实例化继续放在 `AppRuntime`，不回退到 `InputRole` 内构造模块

**明确不做**

- 不引入 `RemoteMux`
- 不做字段级混控
- 不做多来源求和

**验收标准**

- `InputRole.cpp` 中大段协议特化映射被抽离
- 映射规则有单点落地，可复用、可读、可测

#### Card B2-2: 冻结来源仲裁字典与私有诊断量

**目标**

让第一版多输入源接入具备可解释性，但不把 provenance 扩散成公共契约。

**涉及文件**

- `App/Roles/InputRole.hpp`
- `App/Roles/InputRole.cpp`

**建议补充的私有信息**

- active source
- selection reason
- 各源最近更新时间
- 各源在线性
- safe/manual 请求状态

**必须遵守的约束**

- 诊断量保持私有，不进入 `OperatorInputSnapshot`
- 不新增 `App/Topics/*` 来发布仲裁原因

**验收标准**

- 代码层面能解释“为什么此周期选了 primary/secondary/VT13/none”
- `master_machine` 是否覆盖可追溯

#### Card B2-3: 冻结明确“不进入主链”的高级语义

**目标**

防止后续因为 `VT13.mouse/key` 或自动化输入而隐式重构控制主链。

**涉及文件**

- `App/Roles/InputRole.cpp`
- `.Doc/road_map.md`

**当前应继续保持不进入主链的语义**

- `VT13.mouse`
- `VT13.key`
- `target_wz`
- `track_target`

**必须遵守的约束**

- 继续保持当前保守策略：
  - `target_wz_radps = 0`
  - `track_target = false`

**明确不做**

- 不改 `DecisionRole` 输入假设
- 不改 `MotionCommand / AimCommand / FireCommand` schema

**验收标准**

- 文档与代码注释明确说明：这些语义仍未进入主链；未来若引入，必须单独立项

### 11.3 Batch 3: `CANBridge` 最小真实协议闭环

#### Card B3-1: 冻结 bridge 边界、ID 空间与 online 口径

**目标**

把 `CANBridge` 从“收到白名单帧就算在线”的骨架，提升为可维护的 bridge 边界定义。

**涉及文件**

- `App/Config/BridgeConfig.hpp`
- `Modules/CANBridge/CANBridge.hpp`

**关键符号**

- `CANBridgeConfig`
- `CanIdWhitelistEntry`
- `BridgeDirection`
- `BridgeDirectionAllowsRx()`
- `BridgeDirectionAllowsTx()`
- `CANBridgeState`

**必须遵守的约束**

- `CANBridge` 只做协议适配与路由，不成为业务中心
- 严禁把执行器控制帧混进 bridge ID 空间
- `online` 必须在 `kRxOnly / kTxOnly / kBidirectional` 三种模式下都有可解释口径

**明确不做**

- 不把 `CANBridge` 变成 “DJI 电机 CAN 代理”
- 不引入第二套健康树

**验收标准**

- 形成稳定的 bridge ID 规划与 whitelist 方向定义
- whitelist 字段的用途不再含混

#### Card B3-2: 定义第一版最小可闭环协议帧

**目标**

在 Classic CAN 8 字节约束下定义第一版最小协议，使 bridge 具备真正的 RX/TX 闭环。

**涉及文件**

- `Modules/CANBridge/CANBridge.hpp`
- `App/Config/BridgeConfig.hpp`

**关键符号**

- `LibXR::CAN::ClassicPack`
- `CANBridge::OnCanRxStatic(...)`
- `NotifyRxActivity()`
- `NotifyTxActivity()`

**允许迁移的 donor `can_comm` 语义**

- 帧边界
- 序号
- 校验
- 超时
- 在线性
- 轻量统计

**明确不做**

- 不做多帧大包分片
- 不引入桥接旁路业务命令

**验收标准**

- 协议定义至少明确：
  - 帧类型
  - 序号
  - 校验算法
  - 对端回应规则

#### Card B3-3: 收口 RX 路径闭环

**目标**

让 `OnCanRxStatic` 完成：

- 白名单过滤
- 解包/校验
- 状态更新
- 统计更新
- online 驱动

**涉及文件**

- `Modules/CANBridge/CANBridge.hpp`

**必须遵守的约束**

- RX 回调避免复杂分配与重逻辑
- 白名单必须先于解析

**明确不做**

- 不在 RX 回调里驱动命令类 Topic

**验收标准**

- 校验正确与校验错误能在状态/统计上区分
- 超时回退逻辑与 `BridgeDirection` 一致

#### Card B3-4: 补齐 TX 路径与 health 接线

**目标**

让 TX 活动也进入 bridge 状态与 health 口径，完成“最小真实闭环”的另一半。

**涉及文件**

- `Modules/CANBridge/CANBridge.hpp`
- `App/Roles/TelemetryRole.cpp`

**必须遵守的约束**

- 当前健康契约下，`bridge offline` 仍是 `Info` 且无控制动作，除非 Batch 1 先显式改契约
- 不修改官方 `SharedTopicClient` 源码

**明确不做**

- 不把 bridge 提升成必需控制链路
- 不提供外部写入直达执行器的旁路

**验收标准**

- TX 活动能体现在 `CANBridgeState`
- `TelemetryRole` 对 bridge online/offline 的消费仍保持单点、可解释

### 11.4 Batch 4: `referee` 第二轮稳定语义

#### Card B4-1: 冻结第二轮稳定语义清单

**目标**

在现有 `RefereeState` 基础上，冻结值得长期存在的领域语义。

**涉及文件**

- `Modules/Referee/Referee.hpp`
- `App/Config/RefereeConfig.hpp`
- `App/Roles/DecisionRole.cpp`

**建议优先冻结的语义**

- `match_started`
- `chassis_power_limit_w`
- `buffer_energy`
- `remaining_energy_bits`
- `shooter_heat_limit`
- `shooter_cooling_value`
- `shooter_heat`
- `remaining_heat`
- 最终 `fire_allowed` 口径

**必须遵守的约束**

- 原始裁判协议结构体不进入 `App/Topics`
- `DecisionRole` 只消费稳定领域语义

**明确不做**

- 不做 referee UI
- 不把 referee 变成模式决策中心

#### Card B4-2: 模块内部收口为“原始输入 -> 统一派生输出”

**目标**

把当前分散在 `0x0201 / 0x0202 / 0x0204` 分支中的派生逻辑收口，避免更新顺序导致错误瞬态。

**涉及文件**

- `Modules/Referee/Referee.hpp`
- `App/Config/RefereeConfig.hpp`

**关键符号**

- `TryParseSingleFrame(...)`
- `OnMonitor()`
- `ApplyProtocolState(...)`

**必须遵守的约束**

- 离线回退保持保守默认值
- 解析失败不刷新在线时间戳
- clamp 行为统一走 `RefereeConfig`

**明确不做**

- 不追求全协议覆盖
- 只确保当前仓库真正消费到的语义稳定

**验收标准**

- `RefereeState` 派生字段保持“单调保守”
- 缺帧与乱序不会产生明显错误开火/错误功率预算

#### Card B4-3: `DecisionRole / TelemetryRole` 回绑到稳定语义

**目标**

让 `DecisionRole` 对裁判约束的使用点保持薄且单点，并把 `referee offline` 完全对齐到统一 degraded 契约。

**涉及文件**

- `App/Roles/DecisionRole.cpp`
- `App/Roles/TelemetryRole.cpp`
- `App/Runtime/HealthAggregator.hpp`
- `App/Runtime/DegradedPolicy.hpp`

**必须遵守的约束**

- `DecisionRole` 不理解协议细节
- `TelemetryRole` 继续只做聚合

**明确不做**

- 不把 referee 约束下沉到 `ShootRole`
- 不在 Role 中实现协议解析

**验收标准**

- `referee offline` 是否阻止开火，只来自统一健康/降级契约
- `fire_allowed / match_started / remaining_heat` 的使用方式在 `DecisionRole` 中一致且可解释

### 11.5 Batch 5: bring-up 清理与交付收口

#### Card B5-1: `.ioc` 回填 `USART2(VT13)` 配置

**主线程复核确认**

- [Core/Src/main.c](F:/Xrobot/Xro_template/Core/Src/main.c) 已调用 `MX_USART2_UART_Init()`
- [Core/Src/usart.c](F:/Xrobot/Xro_template/Core/Src/usart.c) 已存在 `hdma_usart2_rx` 与 `DMA1_Stream5`
- [basic_framework.ioc](F:/Xrobot/Xro_template/basic_framework.ioc) 当前检索不到 `USART2`

**目标**

消除“USART2 仅靠手工接线存在，CubeMX 再生成会覆盖”的状态。

**涉及文件**

- `basic_framework.ioc`
- `Core/Src/usart.c`
- `Core/Inc/usart.h`
- `Core/Src/stm32f4xx_it.c`
- `Core/Src/main.c`

**验收标准**

- 重新生成后，`USART2 + DMA1_Stream5 + IRQ` 由生成链路稳定产出或完全保留在 `USER CODE`
- 不再依赖运行期“先 TX_RX 再强制收口 RX”的临时 bring-up hack 才能启动

#### Card B5-2: 同步修正 `USART3(DT7)` 的 Mode/DMA 根因

**主线程复核确认**

- [Core/Src/usart.c](F:/Xrobot/Xro_template/Core/Src/usart.c) 当前出现：
  - `huart3.Init.Mode = UART_MODE_TX_RX`
  - 随后在 `USART3_Init 2` 用户区强制收口到 `UART_MODE_RX`

**目标**

让 CubeMX 配置本身与 LibXR 假设一致，减少运行期 workaround。

**涉及文件**

- `basic_framework.ioc`
- `Core/Src/usart.c`

**验收标准**

- 重新生成后不再依赖运行期 DeInit/Init 规避 `hdmatx == NULL` 风险
- DT7 接收链路不回退

#### Card B5-3: 收口 DMA/NVIC 一致性，尤其是 `DMA1_Stream5`

**主线程复核确认**

- [Core/Src/stm32f4xx_it.c](F:/Xrobot/Xro_template/Core/Src/stm32f4xx_it.c) 已存在 `DMA1_Stream5_IRQHandler()`
- [Core/Src/dma.c](F:/Xrobot/Xro_template/Core/Src/dma.c) 当前未检索到 `DMA1_Stream5` 或 `DMA1_Stream5_IRQn`

**目标**

消除“ISR 已写但 NVIC 未开”的诊断盲区。

**涉及文件**

- `basic_framework.ioc`
- `Core/Src/dma.c`
- `Core/Src/stm32f4xx_it.c`

**验收标准**

- `MX_DMA_Init()` 覆盖 `DMA1_Stream5_IRQn` 优先级与使能
- 生成链与实际 `USART2 RX DMA` 完全一致

#### Card B5-4: 建立 CubeMX 再生成回归门

**目标**

把“.ioc 修改后必须复核什么”冻结成统一清单。

**每次 `.ioc` 再生成后必须逐项核对**

1. 唯一启动链仍成立：`defaultTask -> app_main() -> XRobotMain() -> MonitorAll()`
2. `USART2 / USART3` 的最终 HAL Mode 与 DMA 链接满足 LibXR，不触发 `hdmatx` 断言
3. `User/app_main.cpp` 中硬件别名仍可被模块查到：
   - `uart_vt13`
   - `uart_master_machine`
   - `uart_referee`
   - `spi_bmi088`
   - `bmi088_*`

#### Card B5-5: 文档与接线一致性收口

**主线程复核确认**

- [User/app_main.cpp](F:/Xrobot/Xro_template/User/app_main.cpp) 当前已注册：
  - `uart_master_machine`
  - `uart_vt13`
  - `uart_referee`

**目标**

让交接材料能直接复现当前 UART/别名/波特率/中断/DMA 事实。

**建议同步位置**

- `.Doc/road_map.md`
- `task_plan.md`
- `progress.md`
- `findings.md`
- `README.md`

#### Card B5-6: 把临时默认值与兼容路径变成“可追溯临时项”

**目标**

集中列出当前仍保留的临时行为，并写清：

- 保留原因
- 退出条件
- 风险提示

**当前至少要继续标注的临时项**

- `VT13` 第一版映射仍保留：
  - `target_wz_radps = 0`
  - `track_target = false`
- `USART2 / USART3` 仍存在运行期强制收口到 `UART_MODE_RX` 的 bring-up workaround

### 11.6 跨 Batch 1-5 验证矩阵

| Batch | 构建验证 | 板级验证 | 风险回归点 |
|---|---|---|---|
| B1 | `cmake --preset Debug` + `cmake --build --preset Debug` | 观察 `system_health` 字段变化 | `TelemetryRole` 与 `DecisionRole` 不再双点散写；内存占用变化需复核堆/栈 |
| B2 | 同上 | 切换输入源时优先级与回退理由符合文档 | 防止 `InputRole` 再次膨胀；继续回归 UART Mode/DMA 风险 |
| B3 | 同上 | bridge 在线/离线与统计可观测 | bridge 协议不反向塑造内部契约；Role 内不能出现帧编解码 |
| B4 | 同上 | referee 约束能经由 `DecisionRole` 生效且可被 Telemetry 观察 | 协议对象不上抬；离线路径继续走统一 health-to-action |
| B5 | 同上，且改 `.ioc` 后必须全量再生成并编译 | 至少验证 `VT13(USART2)`、`DT7(USART3)`、`referee/master_machine` 不回退 | 三大启动期风险必须重复核对：`hdmatx` 断言、总堆大小、defaultTask 栈水位 |

### 11.7 已过期的旧计划假设

- “需要新建 `HealthAggregator / DegradedPolicy` 骨架”已过期
- “`SystemHealth` 只需要在线布尔与 mask”已过期
- “InputRole 仍使用板载 KEY 占位”已过期
- “需要先新建 `Referee / MasterMachine / CANBridge` 骨架才能推进剩余批次”已过期

### 11.8 允许迁移的 donor 通用语义边界

#### donor `can_comm`

**允许迁移**

- 帧边界
- 最小协议层
- 校验
- 白名单
- 在线/离线检测
- 轻量统计

**禁止迁移**

- C 风格实例数组和全局对象模型
- donor daemon 绑定方式
- 任何让 bridge 成为业务中心的逻辑

#### donor `referee`

**允许迁移**

- 比赛阶段/开始状态
- 底盘功率预算/输出使能的保守语义
- 热量窗口与冷却预算的保守语义
- 能量缓冲与能量位图的保守语义
- 在线/离线与超时回退

**禁止迁移**

- 原始协议对象直接上抬到 `App/Topics/*`
- UI/展示杂项
- 让 referee 直接驱动机器人模式或执行器
