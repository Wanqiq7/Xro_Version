# Phase 2 To Phase 5 Execution Cards Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `Phase 2: referee fan-in`、`Phase 3: master_machine / can_comm bridge fan-in/fan-out`、`Phase 4: health tree & degraded mode`、`Phase 5: architecture cleanup` 拆成可直接执行的任务卡，并保持当前 `C++ + LibXR + Role/Module` 架构不回退。

**Architecture:** 后续任务继续按 `fan-in / fan-out` 主线推进，而不是按 donor 原目录平移。所有外部输入、外部约束、执行器反馈都先进入 `Modules` 或 `InputRole/TelemetryRole`，再由 `DecisionRole`、各业务 `Role` 和 `Bridge` 统一编排；禁止新增第二入口、第二调度中心或第二消息系统。

**Tech Stack:** STM32F407, FreeRTOS, LibXR Topic/Application, XRobot Modules, CMake, J-Link + GDB

---

## 文件责任图

### Phase 2 预期涉及

- Create: `App/Config/RefereeConfig.hpp`
- Create: `Modules/Referee/Referee.hpp`
- Modify: `App/Roles/DecisionRole.hpp`
- Modify: `App/Roles/DecisionRole.cpp`
- Modify: `App/Roles/TelemetryRole.hpp`
- Modify: `App/Roles/TelemetryRole.cpp`
- Modify: `App/Topics/FireCommand.hpp`
- Modify: `App/Topics/MotionCommand.hpp`
- Modify: `App/Topics/SystemHealth.hpp`
- Modify: `App/Runtime/AppRuntime.hpp`
- Modify: `App/Runtime/AppRuntime.cpp`

### Phase 3 预期涉及

- Create: `App/Config/BridgeConfig.hpp`
- Create: `Modules/MasterMachine/MasterMachine.hpp`
- Create: `Modules/CANBridge/CANBridge.hpp`
- Modify: `App/Roles/InputRole.hpp`
- Modify: `App/Roles/InputRole.cpp`
- Modify: `App/Roles/TelemetryRole.hpp`
- Modify: `App/Roles/TelemetryRole.cpp`
- Modify: `App/Runtime/OperatorInputSnapshot.hpp`
- Modify: `App/Runtime/AppRuntime.hpp`
- Modify: `App/Runtime/AppRuntime.cpp`

### Phase 4 预期涉及

- Create: `App/Config/DegradedModeConfig.hpp`
- Modify: `App/Topics/SystemHealth.hpp`
- Modify: `App/Roles/DecisionRole.hpp`
- Modify: `App/Roles/DecisionRole.cpp`
- Modify: `App/Roles/TelemetryRole.hpp`
- Modify: `App/Roles/TelemetryRole.cpp`
- Modify: `App/Topics/RobotMode.hpp`
- Modify: `App/Runtime/AppRuntime.hpp`
- Modify: `App/Runtime/AppRuntime.cpp`

### Phase 5 预期涉及

- Modify: `App/Config/ActuatorConfig.hpp`
- Modify: `App/Config/BridgeConfig.hpp`
- Modify: `App/Topics/ChassisState.hpp`
- Modify: `App/Topics/ShootState.hpp`
- Modify: `App/Roles/ChassisRole.cpp`
- Modify: `App/Roles/ShootRole.cpp`
- Modify: `Modules/MecanumChassisDrive/MecanumChassisDrive.hpp`
- Modify: `Modules/ShootFrictionMotorGroup/ShootFrictionMotorGroup.hpp`
- Modify: `Modules/ShootLoaderMotor/ShootLoaderMotor.hpp`
- Modify: `.Doc/2026-04-21_fan_in_fan_out_future_migration_roadmap.md`

---

## Chunk 1: Phase 2 - Referee Fan-in

### Task Card P2-1: 冻结 Referee fan-in 边界与配置出口

**Fan-in / Fan-out:** `Perception & Constraint Fan-in`

**Files:**
- Create: `App/Config/RefereeConfig.hpp`
- Modify: `.Doc/2026-04-21_fan_in_fan_out_future_migration_roadmap.md`
- Reference: `AGENTS.md`

- [ ] 明确 `referee` 只作为外部约束输入源，不得成为新的决策中心。
- [ ] 冻结 `referee` 的硬件接入方式、帧来源、任务栈和超时配置出口。
- [ ] 定义第一批允许进入上层的稳定语义：`fire_allowed`、`power_budget`、`heat_limit`、`match_state`、`online`。
- [ ] 明确哪些字段仍留在模块私有状态，不进入 `App/Topics`。

**Verification:**
- [ ] 文档中写明 `Modules/Referee -> DecisionRole/TelemetryRole` 的单向依赖。
- [ ] 配置文件中不存在 HAL 句柄或协议原始缓冲区。

**Acceptance Criteria:**
- `referee` 的输入边界和配置出口冻结。
- 后续实现不会把协议结构体直接扩散到 `App/Topics`。

### Task Card P2-2: 实现 Referee 协议适配模块

**Fan-in / Fan-out:** `Perception & Constraint Fan-in`

**Files:**
- Create: `Modules/Referee/Referee.hpp`
- Modify: `App/Runtime/AppRuntime.hpp`
- Modify: `App/Runtime/AppRuntime.cpp`

- [ ] 新建 `Referee` 模块，继承 `LibXR::Application`。
- [ ] 模块只负责协议接收、在线性判断、稳定领域字段提取。
- [ ] 模块内部可保留私有 Topic 或状态缓存，但不直接写 `system_health`。
- [ ] 在 `AppRuntime` 中按 `context -> module -> role` 顺序实例化。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- `Referee` 模块可编译、可实例化。
- 模块层不包含整车级策略判断。

### Task Card P2-3: 把 Referee 约束接入 DecisionRole

**Fan-in / Fan-out:** `Perception & Constraint Fan-in` -> `Decision Fan-out`

**Files:**
- Modify: `App/Roles/DecisionRole.hpp`
- Modify: `App/Roles/DecisionRole.cpp`
- Modify: `App/Topics/FireCommand.hpp`
- Modify: `App/Topics/MotionCommand.hpp`

- [ ] 让 `DecisionRole` 订阅或读取 `Referee` 的稳定语义。
- [ ] 把 `是否允许开火`、`底盘功率预算` 等约束转化为高层命令字段，而不是让执行器模块自己判断。
- [ ] 为 `fire_command` 和 `motion_command` 增加必要但最小的约束字段。
- [ ] 明确 `referee offline` 时的保守默认行为。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- `fire_command` 和/或 `motion_command` 能反映比赛约束。
- `DecisionRole` 仍只输出高层命令，不下沉底层控制。

### Task Card P2-4: 把 Referee 状态接入 TelemetryRole 与 SystemHealth

**Fan-in / Fan-out:** `Health Fan-in`

**Files:**
- Modify: `App/Roles/TelemetryRole.hpp`
- Modify: `App/Roles/TelemetryRole.cpp`
- Modify: `App/Topics/SystemHealth.hpp`

- [ ] 扩展 `SystemHealth`，显式承载 `referee_online` 与第一批比赛约束健康信号。
- [ ] `TelemetryRole` 汇聚 `referee` 在线性和约束状态。
- [ ] 不在 `TelemetryRole` 中引入新的控制决策。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- `system_health` 能反映 `referee_online`
- `TelemetryRole` 仍是聚合层，不是策略层。

---

## Chunk 2: Phase 3 - Master Machine / CAN Bridge Fan-in-Fan-out

### Task Card P3-1: 冻结桥接白名单与配置出口

**Fan-in / Fan-out:** `Operator Intent Fan-in` + `Telemetry / Bridge Fan-out`

**Files:**
- Create: `App/Config/BridgeConfig.hpp`
- Modify: `.Doc/2026-04-21_fan_in_fan_out_future_migration_roadmap.md`

- [ ] 定义桥接白名单：哪些内部 Topic 可对外、哪些外部消息可进内部。
- [ ] 定义 `master_machine` 和 `can_comm` 的优先级、超时与输入源身份。
- [ ] 明确桥接只做协议适配与路由，不成为新的业务中心。

**Verification:**
- [ ] 文档中存在对外 Topic 白名单。
- [ ] 配置里没有把内部 Topic schema 改造成协议帧。

**Acceptance Criteria:**
- 桥接规则在实现前冻结。
- 后续不会为桥接修改内部业务契约。

### Task Card P3-2: 实现 MasterMachine 输入适配模块

**Fan-in / Fan-out:** `Operator Intent Fan-in`

**Files:**
- Create: `Modules/MasterMachine/MasterMachine.hpp`
- Modify: `App/Runtime/AppRuntime.hpp`
- Modify: `App/Runtime/AppRuntime.cpp`

- [ ] 新建 `MasterMachine` 模块，负责上位机输入协议适配。
- [ ] 输出稳定的模块私有状态，而不是直接写公共命令 Topic。
- [ ] 在 `AppRuntime` 中完成实例化和依赖注入。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- `master_machine` 已作为输入源存在。
- 仍未绕过 `InputRole/DecisionRole` 主链。

### Task Card P3-3: 升级 InputRole 为多输入源汇聚器

**Fan-in / Fan-out:** `Operator Intent Fan-in`

**Files:**
- Modify: `App/Roles/InputRole.hpp`
- Modify: `App/Roles/InputRole.cpp`
- Modify: `App/Runtime/OperatorInputSnapshot.hpp`

- [ ] 让 `InputRole` 同时消费 `remote` 与 `master_machine`。
- [ ] 建立输入源优先级与失效回退规则。
- [ ] 保持输出仍是 `OperatorInputSnapshot`，不直接写执行器命令。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- `InputRole` 成为统一输入汇聚点。
- 任一输入源失效时有明确回退行为。

### Task Card P3-4: 实现对外桥接模块与 Telemetry 出口

**Fan-in / Fan-out:** `Telemetry / Bridge Fan-out`

**Files:**
- Create: `Modules/CANBridge/CANBridge.hpp`
- Modify: `App/Roles/TelemetryRole.hpp`
- Modify: `App/Roles/TelemetryRole.cpp`
- Modify: `App/Runtime/AppRuntime.hpp`
- Modify: `App/Runtime/AppRuntime.cpp`

- [ ] 新建 `CANBridge` 或 `CANComm` 模块，只做对外协议桥接。
- [ ] `TelemetryRole` 按白名单选择可对外发送的状态。
- [ ] 不允许桥接模块直接消费内部私有实现细节。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- 对外桥接存在稳定出口。
- 桥接链路不破坏内部 Topic 契约。

---

## Chunk 3: Phase 4 - Health Tree & Degraded Mode

### Task Card P4-1: 扩展 SystemHealth 为健康树骨架

**Fan-in / Fan-out:** `Health Fan-in`

**Files:**
- Modify: `App/Topics/SystemHealth.hpp`
- Create: `App/Config/DegradedModeConfig.hpp`

- [ ] 把 `system_health` 从布尔集合扩展为健康树骨架。
- [ ] 至少加入：`source bitmap`、`degraded reason`、`fault severity`、`recovery hint`。
- [ ] 所有新字段保持轻量、稳定、可跨角色理解。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- `system_health` 可承载统一健康与降级摘要。
- 没有把模块内部调试细节塞进公共状态。

### Task Card P4-2: 建立 TelemetryRole 健康树汇聚逻辑

**Fan-in / Fan-out:** `Health Fan-in`

**Files:**
- Modify: `App/Roles/TelemetryRole.hpp`
- Modify: `App/Roles/TelemetryRole.cpp`

- [ ] 为输入链、观测链、执行链、桥接链分别汇聚健康源。
- [ ] 输出统一健康树与故障摘要。
- [ ] 明确各健康源的更新口径和默认值。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- `TelemetryRole` 能汇总多健康源。
- 仍不承接控制策略。

### Task Card P4-3: 把 Degraded Mode 接入 DecisionRole

**Fan-in / Fan-out:** `Health Fan-in` -> `Decision Fan-out`

**Files:**
- Modify: `App/Roles/DecisionRole.hpp`
- Modify: `App/Roles/DecisionRole.cpp`
- Modify: `App/Topics/RobotMode.hpp`

- [ ] 为 `DecisionRole` 引入统一降级模式判定。
- [ ] 把关键链路失效映射到 `safe / degraded / limited fire / limited motion` 等高层模式。
- [ ] 禁止在执行器模块中各自定义降级逻辑。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- 存在统一的 degraded mode 入口。
- 输入链、感知链、执行链失效时行为一致。

### Task Card P4-4: 建立健康场景验证脚本与人工验收清单

**Fan-in / Fan-out:** `Health Fan-in`

**Files:**
- Create: `.Doc/phase4_health_validation_checklist.md`
- Reference: `App/Topics/SystemHealth.hpp`
- Reference: `App/Roles/DecisionRole.cpp`
- Reference: `App/Roles/TelemetryRole.cpp`

- [ ] 列出输入失联、IMU 失联、执行器失联、referee 失联、桥接失联的验收场景。
- [ ] 为每类场景定义期望的 `system_health` 与 `robot_mode` 变化。

**Verification:**
- [ ] 清单可直接用于板上联调。

**Acceptance Criteria:**
- 后续 health tree 联调有统一标准。

---

## Chunk 4: Phase 5 - Architecture Cleanup

### Task Card P5-1: 清理执行器 bring-up 代理输出

**Fan-in / Fan-out:** `Actuator Fan-out`

**Files:**
- Modify: `Modules/MecanumChassisDrive/MecanumChassisDrive.hpp`
- Modify: `Modules/ShootFrictionMotorGroup/ShootFrictionMotorGroup.hpp`
- Modify: `Modules/ShootLoaderMotor/ShootLoaderMotor.hpp`
- Modify: `App/Roles/ChassisRole.cpp`
- Modify: `App/Roles/ShootRole.cpp`

- [ ] 把 bring-up 阶段的开环代理输出替换为真实反馈闭环。
- [ ] 接入真实 M3508 反馈回灌。
- [ ] 删除仅用于占位的代理逻辑与注释。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- `shoot/chassis` 状态字段真正来自执行器反馈。
- bring-up 代理逻辑被清理。

### Task Card P5-2: 收敛配置出口与参数层次

**Fan-in / Fan-out:** `Actuator Fan-out`

**Files:**
- Modify: `App/Config/ActuatorConfig.hpp`
- Modify: `App/Config/BridgeConfig.hpp`
- Modify: `App/Config/DegradedModeConfig.hpp`

- [ ] 把执行器、桥接、降级相关参数收敛到集中配置文件。
- [ ] 清理散落在 Role/Module 内的魔法数。
- [ ] 区分长期参数与 bring-up 临时参数。

**Verification:**
- [ ] Run: `rg -n "3000|1800|36.0|timeout|frame_id" App Modules`
- [ ] Expected: 仅命中配置文件或明确允许的常量定义

**Acceptance Criteria:**
- 配置出口单一。
- Role/Module 内不再散落关键调参值。

### Task Card P5-3: 清理公共状态中的临时字段

**Fan-in / Fan-out:** `Health Fan-in` + `Telemetry / Bridge Fan-out`

**Files:**
- Modify: `App/Topics/ChassisState.hpp`
- Modify: `App/Topics/ShootState.hpp`
- Modify: `App/Topics/SystemHealth.hpp`

- [ ] 清理只为 bring-up 引入、但不适合作为长期公共契约的字段。
- [ ] 保留长期稳定、跨角色、对外可桥接的字段。
- [ ] 重新标注文档中的状态可信度和用途边界。

**Verification:**
- [ ] Run: `cmake --build --preset Debug`
- [ ] Expected: PASS

**Acceptance Criteria:**
- 公共 Topic 回到长期稳定语义。
- 状态契约不再夹杂一次性 bring-up 信息。

### Task Card P5-4: 最终架构审计与文档收口

**Fan-in / Fan-out:** 全局

**Files:**
- Modify: `.Doc/2026-04-21_fan_in_fan_out_future_migration_roadmap.md`
- Modify: `.Doc/2026-04-21_phase2_to_phase5_execution_cards.md`
- Reference: `AGENTS.md`

- [ ] 审计是否仍然满足 `User -> App -> Modules -> LibXR`。
- [ ] 审计是否仍然只有唯一运行链。
- [ ] 审计是否不存在第二套消息系统、第二套桥接中心或第二套调度中心。
- [ ] 把最终白名单、禁令和剩余技术债写回文档。

**Verification:**
- [ ] Run: `rg -n "RobotTask|OSTaskInit|message_center|HandleTypeDef" App Modules User`
- [ ] Expected: 不出现新的违禁实现

**Acceptance Criteria:**
- 架构收口完成。
- 后续进入维护和性能优化阶段，而不是继续大范围迁移。

---

## 依赖顺序

1. `P2-1`
2. `P2-2`
3. `P2-3`
4. `P2-4`
5. `P3-1`
6. `P3-2`
7. `P3-3`
8. `P3-4`
9. `P4-1`
10. `P4-2`
11. `P4-3`
12. `P4-4`
13. `P5-1`
14. `P5-2`
15. `P5-3`
16. `P5-4`

## 并行建议

- `P2-2` 与 `P2-4` 可在写入域不冲突时并行。
- `P3-2` 与 `P3-4` 可并行，但桥接白名单必须先由 `P3-1` 冻结。
- `P4-2` 与 `P4-3` 可并行，但 `P4-1` 必须先完成。
- `P5-1` 与 `P5-2` 可并行，`P5-3` 与 `P5-4` 必须后置。

## 交付定义

当 `Phase 2 ~ Phase 5` 全部完成时，应满足：

- `referee` 已作为稳定约束 fan-in 接入
- `master_machine / can_comm` 已作为受控桥接接入
- 存在统一健康树与降级模式
- bring-up 临时实现已基本清理
- 文档、配置、桥接白名单和禁令与代码一致

Plan complete and saved to `.Doc/2026-04-21_phase2_to_phase5_execution_cards.md`. Ready to execute?
