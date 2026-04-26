# Phase 4-6 Fan-in / Fan-out Migration Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不破坏当前唯一运行链与 `App / Modules / Topics` 边界的前提下，把 basic_framework 剩余可迁移能力收敛为当前仓库的 Phase 4-6 执行计划，优先完成 `health tree / degraded mode`、`master_machine` 输入仲裁、`CANBridge / can_comm` 真实桥接闭环，以及最终架构收口。

**Architecture:** 继续按 `fan-in / fan-out` 推进，而不是 donor 目录平移。先把 `TelemetryRole -> SystemHealth -> DecisionRole` 这条健康/降级主线收紧为统一契约，再补 `InputRole` 多输入源仲裁与 `CANBridge` 真实桥接，最后做 `referee` 第二轮语义补齐和 Phase 6 清理。

**Tech Stack:** STM32CubeMX, FreeRTOS, LibXR, XRobot, C++ embedded modules, UART/CAN bridge, Topic-based runtime

---

## Recommended Order

1. `Flow C + Flow D`: 先统一 `health tree / degraded mode` 契约。
2. `Flow A`: 再把 `remote + master_machine` 输入仲裁收紧到 `InputRole`。
3. `Flow F`: 然后完成 `SharedTopicClient` 输出治理与 `CANBridge` 真实闭环。
4. `Flow B`: 最后补 `referee` 第二轮稳定语义。
5. `Phase 6`: 清理 bring-up 遗留与文档/配置碎片。

原因：

- 当前代码里最接近“半成品”的是 `TelemetryRole + DecisionRole`，先收口这里可以给后续桥接、输入优先级和 `referee` 扩展提供统一落点。
- donor 的 `daemon` 与 `can_comm` 都是“能力语义”而不是“目录模板”；若跳过契约统一，后续只会把 donor 的临时设计重新散落到当前仓库。

## Rebase Notes

本计划在继续分析后需要以当前仓库真实状态为准，而不是继续假设相关 helper 尚不存在。当前已经确认：

- `App/Runtime/HealthAggregator.hpp` 已存在。
- `App/Runtime/DegradedPolicy.hpp` 已存在。
- `App/Runtime/HealthPolicy_compile_test.cpp` 已存在。
- 但 `TelemetryRole.cpp` 与 `DecisionRole.cpp` 仍未使用这些统一策略入口。

因此，后续执行时应把 Task 1-3 理解为：

- 不是“先创建健康/降级策略文件”；
- 而是“基于已存在 helper 完成 live role 接线、字段补齐与策略收口”。

## Near-term Batches

### Batch A: Health Policy Wiring

**Priority:** P0  
**Depends on:** none

- 把 `TelemetryRole` 改为通过 `HealthAggregator` 产出 `system_health`
- 把 `DecisionRole` 改为通过 `DegradedPolicy` 消费统一降级动作
- 用 `HealthPolicy_compile_test.cpp` 继续冻结关键规则，不再允许 Role 内散写第二套判断

### Batch B: Multi-Remote Ingress Upgrade

**Priority:** P1  
**Depends on:** Batch A

- 目标收缩为“多遥控器接入”，不扩展为整条控制链重构
- 在 `InputRole` 内支持多个遥控器实例接入
- 保持现有 `OperatorInputSnapshot` 下游契约基本不变
- 第一版采用“单设备独占 + 主备切换”策略，不做求和、不做字段级混控
- `RemoteControl` 只做最小必要改造，以允许多个实例共存

### Batch C: Bridge Closure

**Priority:** P2  
**Depends on:** Batch A

- 先冻结 `CANBridge` 的桥接边界与健康语义
- 再补 donor `can_comm` 中可迁移的通用协议能力
- 最后接到 `TelemetryRole` 的健康树与 `BridgeConfig` 的治理出口

## Task 1: Freeze Health / Degraded Contract

**Fan direction:** `Flow C: Health Fan-in` + `Flow D: Decision Fan-out`

**Files:**
- Modify: `App/Config/HealthPolicyConfig.hpp`
- Modify: `App/Config/DegradedModeConfig.hpp`
- Modify: `App/Topics/SystemHealth.hpp`
- Modify: `App/Topics/RobotMode.hpp`
- Modify: `App/Runtime/HealthAggregator.hpp`
- Modify: `App/Runtime/DegradedPolicy.hpp`
- Modify: `App/Runtime/HealthPolicy_compile_test.cpp`
- Reference: `App/Roles/TelemetryRole.cpp`
- Reference: `App/Roles/DecisionRole.cpp`

- [ ] **Step 1: 基于已存在 helper 统一健康源与降级原因字典**

冻结以下概念，禁止后续继续以“局部 bool + 局部 if/else”追加：

- `health source`
- `degraded reason`
- `fault severity`
- `recovery hint`
- `control action`

- [ ] **Step 2: 把当前零散语义继续收敛到配置/契约层**

建议把以下策略从角色代码中抽离到配置或稳定辅助类型：

- 某类故障是否触发 `safe`
- 某类故障是否只触发 `limited`
- 哪些故障只影响 `fire`
- 哪些故障影响 `motion`

- [ ] **Step 3: 扩展 Topic 契约但不改 Topic 名称**

要求：

- `system_health` 继续保持当前 Topic 名称
- `robot_mode` 继续保持当前 Topic 名称
- 优先追加稳定字段，不回退现有公开语义

- [ ] **Step 4: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- 编译通过
- 不引入新的架构层级越界

**Acceptance criteria:**

- `TelemetryRole` 与 `DecisionRole` 对“降级原因”的理解来自同一份契约，而不是各自散写逻辑
- `SystemHealth` 能表达“为什么降级”，不只是“谁离线了”
- `RobotMode` 能表达“当前降级行为类型”，不只是布尔开关
- 不新增第二套 health/degraded helper

## Task 2: Refactor TelemetryRole Into a Real Health Fan-in Aggregator

**Fan direction:** `Flow C: Health Fan-in`

**Files:**
- Modify: `App/Runtime/HealthAggregator.hpp`
- Modify: `App/Roles/TelemetryRole.cpp`
- Modify: `App/Roles/TelemetryRole.hpp`
- Modify: `App/Runtime/RuntimeContext.hpp`
- Reference: `Modules/MasterMachine/MasterMachine.hpp`
- Reference: `Modules/CANBridge/CANBridge.hpp`
- Reference: `Modules/Referee/Referee.hpp`

- [ ] **Step 1: 把健康聚合逻辑从 `OnMonitor()` 的大段内联 if/else 切换到现有 `HealthAggregator`**

要求：

- `TelemetryRole` 只负责拉 Topic 和发布结果
- 健康判定规则由单独聚合器承担

- [ ] **Step 2: 为每类来源建立稳定输入映射**

至少覆盖：

- control link
- imu
- actuator
- referee
- master_machine
- can_bridge

- [ ] **Step 3: 明确桥接/输入源离线对健康树的影响层级**

不要把以下状态混为一谈：

- 可选桥接离线
- 关键控制链离线
- 执行器失能

- [ ] **Step 4: 发布统一 `system_health`**

要求：

- `system_health` 的各字段来自单点聚合结果
- 角色内部不再自行拼装重复位图

- [ ] **Step 5: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- `TelemetryRole` 仍能正常编译并发布 `system_health`

**Acceptance criteria:**

- 当前健康树语义集中在一个稳定入口
- `TelemetryRole` 不再同时承担“拉 Topic、解释策略、拼位图、决定恢复建议”四种职责
- `HealthAggregator.hpp` 不再只是“已存在但未接线”的孤立 helper

## Task 3: Rebind DecisionRole to the Unified Degraded Policy

**Fan direction:** `Flow D: Decision Fan-out`

**Files:**
- Modify: `App/Runtime/DegradedPolicy.hpp`
- Modify: `App/Roles/DecisionRole.cpp`
- Modify: `App/Roles/DecisionRole.hpp`
- Reference: `App/Config/DegradedModeConfig.hpp`
- Reference: `App/Topics/RobotMode.hpp`
- Reference: `App/Topics/SystemHealth.hpp`

- [ ] **Step 1: 删除 `DecisionRole` 内部散写的降级判断分叉，并切换到现有 `DegradedPolicy`**

当前需要收口的最小问题：

- `referee_online`
- `imu_online`
- `chassis_online / shoot_online`
- `can_bridge_online / master_machine_online`

- [ ] **Step 2: 把“降级原因 -> 命令限制”显式化**

至少拆出：

- 对 `robot_mode` 的影响
- 对 `motion_command` 的影响
- 对 `fire_command` 的影响

- [ ] **Step 3: 保持 `DecisionRole` 只扇出高层命令**

禁止在该任务中新增：

- 电机级细节
- CAN 帧
- 模块重启逻辑

- [ ] **Step 4: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- 降级模式变更后，`robot_mode / motion_command / fire_command` 仍由 `DecisionRole` 单点发布

**Acceptance criteria:**

- `DecisionRole` 不再拥有“另一套健康树”
- 降级策略修改时，修改点集中且可解释
- `DegradedPolicy.hpp` 成为实际被消费的唯一降级视图

## Task 4: Add Multi-Remote Ingress Without Reworking the Control Chain

**Fan direction:** `Flow A: Operator Intent Fan-in`

**Files:**
- Modify: `Modules/RemoteControl/RemoteControl.hpp`
- Modify: `App/Roles/InputRole.cpp`
- Modify: `App/Roles/InputRole.hpp`
- Reference: `App/Runtime/OperatorInputSnapshot.hpp`
- Reference: `Modules/MasterMachine/MasterMachine.hpp`

- [ ] **Step 1: 允许多个 `RemoteControl` 实例并存**

要求：

- `RemoteControl` 不再强依赖唯一固定 Topic 名
- 不在模块内做多遥控器仲裁
- 模块仍只负责协议解析、在线性、强类型 private topic

- [ ] **Step 2: 在 `InputRole` 内持有主/备遥控器实例**

建议第一版至少支持：

- `remote_control_primary_`
- `remote_control_secondary_`
- 对应 subscriber 与状态缓存

- [ ] **Step 3: 第一版只做“单设备独占”选择**

要求：

- 同一时刻只有一个遥控器来源能生成整份人工输入
- 激活源离线、超时或进入显式 safe 后，再切到下一候选源
- 若所有源都不可用，则回到 safe
- `DecisionRole` 消费的仍是稳定快照
- `Role` 不直接订阅 `remote_control_state`
- 第一版不要做模拟量求和，也不要做字段级混控

- [ ] **Step 4: 维持现有控制链边界**

禁止在本任务中：

- 改 `DecisionRole / MotionCommand / AimCommand / FireCommand` 的主契约
- 把多遥控器仲裁下沉到 `RemoteControl`
- 为当前目标引入 `RemoteMux/RemoteAggregator` 新长期模块
- 借机重构整条控制链

- [ ] **Step 5: 保留最小来源解释能力**

要求：

- 至少在 `InputRole` 内部保留：
  - 当前激活源
  - 选择原因
  - 各源在线性
  - 最近更新时间
  - 显式 safe 请求状态
- 第一版可不把这些字段扩展进公共 Topic

- [ ] **Step 6: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- `InputRole` 能在多台遥控器之间稳定切换
- 现有下游控制链不需要跟着一起改

**Acceptance criteria:**

- 多遥控器接入逻辑集中在 `InputRole`
- `RemoteControl` 仍保持纯协议适配器角色
- 第一版已解决“多个遥控器如何接入”，但没有把问题扩大成整条控制链重构

## Task 5: Normalize Bridge Fan-out Governance

**Fan direction:** `Flow F: Telemetry / Bridge Fan-out`

**Files:**
- Modify: `App/Config/BridgeConfig.hpp`
- Modify: `App/Runtime/AppRuntime.cpp`
- Modify: `App/Roles/TelemetryRole.cpp`
- Reference: `Modules/SharedTopicClient/SharedTopicClient.hpp`
- Reference: `Modules/MasterMachine/MasterMachine.hpp`

- [ ] **Step 1: 保持内部 Topic 契约先于外部协议**

要求：

- 继续由 `App/Config/BridgeConfig.hpp` 冻结白名单
- 禁止为了桥接协议回改内部 Topic 字段

- [ ] **Step 2: 显式区分“对外发布选择”和“桥接链路健康”**

当前桥接输出治理至少要回答：

- 哪些 Topic 可以向外发
- 哪些状态只是本地可见
- 桥接离线时本地系统是否必须降级

- [ ] **Step 3: 保持官方 `SharedTopicClient` 只读**

如需适配：

- 优先通过配置与 App 层编排完成
- 不直接修改 `xrobot-org/SharedTopicClient` 源码

- [ ] **Step 4: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- 现有状态类 Topic 仍按白名单发出

**Acceptance criteria:**

- 对外桥接选择逻辑有单点配置
- bridge offline 与 local degraded 的关系是显式的，而不是散落注释

## Task 6: Implement Real CANBridge Protocol Closure from donor `can_comm` Semantics

**Fan direction:** `Flow F: Telemetry / Bridge Fan-out`

**Files:**
- Modify: `Modules/CANBridge/CANBridge.hpp`
- Modify: `App/Config/BridgeConfig.hpp`
- Modify: `App/Roles/TelemetryRole.cpp`
- Modify: `App/Roles/TelemetryRole.hpp`
- Reference: `basic_framework-master/modules/can_comm/can_comm.h`
- Reference: `basic_framework-master/modules/can_comm/can_comm.md`

- [ ] **Step 1: 只迁移 donor `can_comm` 的通用能力语义**

允许迁移：

- 打包/解包边界
- 白名单帧 ID
- 校验和
- 在线状态与超时

禁止迁移：

- donor 的实例数组风格
- donor 的 daemon 绑定方式
- donor 的 app 级协议对象

- [ ] **Step 2: 定义当前仓库的桥接边界**

要先冻结：

- 发送什么类型的数据
- 接收后回灌到哪个模块私有状态
- 哪些 CAN ID 属于 bridge 而非执行器控制

- [ ] **Step 3: 完成最小真实闭环**

第一版目标应是：

- whitelist 生效
- 可统计 rx/tx 活动
- 有最小打包/解包
- 能与 `system_health` 联动

- [ ] **Step 4: 明确不越界到 Role**

要求：

- `CANBridge` 不直接做高层决策
- `Role` 不直接处理桥接帧格式

- [ ] **Step 5: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- `CANBridge` 不再只是“在线状态骨架”
- 仍保持 `Module` 边界纯净

**Acceptance criteria:**

- donor `can_comm` 的通用桥接能力已经在 `Modules/CANBridge` 落地
- 没有重新把 donor 的 C 风格 app 层对象模型搬进当前仓库

## Task 7: Expand Referee Fan-in to Second-round Stable Semantics

**Fan direction:** `Flow B: Perception & Constraint Fan-in`

**Files:**
- Modify: `Modules/Referee/Referee.hpp`
- Modify: `App/Config/RefereeConfig.hpp`
- Modify: `App/Roles/DecisionRole.cpp`
- Modify: `App/Roles/TelemetryRole.cpp`
- Reference: `basic_framework-master/modules/referee/referee_task.h`
- Reference: `basic_framework-master/modules/referee/referee.md`

- [ ] **Step 1: 明确第二轮只补“稳定语义”，不补 UI / 交互杂项**

优先考虑：

- 比赛阶段
- 功率预算
- 枪口热量窗口
- 允许开火条件
- 与健康树联动的关键状态

- [ ] **Step 2: 保持协议对象与领域对象分层**

要求：

- 原始裁判协议结构不进入 `App/Topics`
- `DecisionRole` 只消费稳定领域语义

- [ ] **Step 3: 与 `DegradedPolicy` 对齐**

不要再额外长出：

- 第二套 `referee offline` 判定
- 第二套 `fire allowed` 回退逻辑

- [ ] **Step 4: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- `DecisionRole` 的火控/底盘约束能使用更稳定的 `referee` 语义

**Acceptance criteria:**

- `referee` 第二轮扩展仍保持 `Module -> Role` 单向依赖
- `DecisionRole` 不直接持有协议解析负担

## Task 8: Phase 6 Cleanup and Architecture Closure

**Fan direction:** 收口任务，跨 `Flow A-F`

**Files:**
- Modify: `.Doc/2026-04-21_fan_in_fan_out_future_migration_roadmap.md`
- Modify: `.Doc/2026-04-21_phase4_phase6_fan_in_fan_out_execution_plan.md`
- Modify: `App/Config/*.hpp`
- Modify: `App/Roles/*.cpp`
- Modify: `App/Topics/*.hpp`

- [ ] **Step 1: 清理 bring-up 临时语义**

优先清理：

- 占位状态字段
- 只为板上 bring-up 存在的保守默认值
- 重复配置出口

- [ ] **Step 2: 校正文档与代码的一致性**

要求：

- 公共 Topic、角色职责、桥接白名单与实际代码一致
- 新增契约先落文档，再保留到代码

- [ ] **Step 3: 保留板上验证清单**

至少记录：

- `app_main()` 启动链
- `InputRole` 输入仲裁
- `TelemetryRole` 健康树
- `DecisionRole` 降级动作
- `CANBridge` / `master_machine` 在线回退

- [ ] **Step 4: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- 规划完成后的主干代码仍可编译

**Acceptance criteria:**

- 当前仓库不再残留 donor 平移式思维的“半迁移结构”
- Phase 4-6 的后续工作有稳定入口，而不是继续按问题驱动零碎修补

## Exit Criteria for This Plan

满足以下条件，说明这份计划已被正确执行：

- `SystemHealth` 与 `RobotMode` 形成统一健康/降级契约
- `TelemetryRole` 成为真正的健康聚合扇入点
- `DecisionRole` 成为统一降级动作扇出点
- `InputRole` 成为真实多输入源仲裁器
- `CANBridge` 具备最小真实桥接闭环
- `referee` 第二轮语义补齐后，仍保持 `Module -> Role` 单向边界
- bring-up 临时结构完成收口，路线图与代码一致
