# Fan-in / Fan-out Migration Task Plan

## Goal

在现有 `.Doc/2026-04-21_fan_in_fan_out_future_migration_roadmap.md` 的长期路线图基础上，
补出一份面向当前仓库代码现状的可执行迁移任务计划，优先覆盖：

- Phase 4: `health tree` 与 `degraded mode`
- Phase 5: `CANBridge / can_comm`
- Phase 6: 清理 bring-up 遗留与架构收口

## Current Phase

- `phase_1_context_scan`: completed
- `phase_2_gap_analysis`: completed
- `phase_3_execution_plan_write`: completed
- `phase_4_execution_plan_rebase`: completed
- `phase_5_batch_a_health_policy_wiring`: completed
- `phase_6_rmpp_input_chain_research`: completed
- `phase_7_rmpp_parallel_research_synthesis`: completed
- `phase_8_multi_remote_scope_refine`: completed
- `phase_9_multi_remote_first_pass_impl`: completed
- `phase_10_dt7_vt13_plan_write`: completed
- `phase_11_dt7_vt13_first_pass_impl`: completed
- `phase_12_vt13_usart2_enable`: completed
- `phase_13_vt13_inputrole_first_pass`: completed
- `phase_14_remaining_plan_rebase`: completed
- `phase_15_subagent_batch_refine`: completed
- `phase_16_batch12_impl`: completed
- `phase_17_batch34_impl`: completed
- `phase_18_batch5_first_pass_cleanup`: completed
- `phase_19_batch6_research_synthesis`: completed
- `phase_20_batch6_impl`: completed
- `phase_21_batch7_research_synthesis`: completed
- `phase_22_batch7_impl`: completed
- `phase_23_batch8_impl`: completed
- `phase_24_batch9_scope_trim`: completed
- `phase_25_role_semantics_absorb_impl`: completed

## Deliverables

- `.Doc/2026-04-21_phase4_phase6_fan_in_fan_out_execution_plan.md`
- `.Doc/2026-04-21_rmpp_input_chain_fan_in_fan_out_research.md`
- `.Doc/2026-04-22_remaining_fan_in_fan_out_execution_plan.md`
- `findings.md`
- `progress.md`

## Key Constraints

- 继续遵守唯一运行链：`defaultTask -> app_main() -> XRobotMain() -> ApplicationManager::MonitorAll()`
- 继续遵守依赖方向：`User -> App -> Modules -> LibXR`
- 禁止把 donor 的 `daemon / can_comm / master_process` 原样搬运到当前仓库
- 禁止新增第二入口、第二主循环、第二消息系统

## Risks

- 当前 `TelemetryRole` 与 `DecisionRole` 对降级语义存在双点判定，若不先统一契约，后续 `CANBridge` 和 `master_machine` 很容易继续散落判断逻辑
- donor 的 `master_machine` 本质更接近视觉/上位机协议壳，不应被误判为当前仓库的最终输入架构模板
- 当前已存在 `HealthAggregator / DegradedPolicy` 但尚未接入 live roles，若后续规划仍按“从零创建”推进，会造成计划与仓库真实状态脱节
- `Batch A` 完成后，剩余主要风险已转移为：
  - `SystemHealth` 契约仍未显式公开 `primary_reason / control_action_mask`
  - `InputRole` 还未提供可解释的来源仲裁，`control_source` 语义仍偏保守
- `rmpp-master` 调研后，已明确新的 Batch B 风险：
  - 若仍以 whole-source switch 方式升级 `InputRole`，后续 `automation / vision / host assist` 会再次逼出第二套决策路径
  - 若过度照搬 `rmpp::Robot`，会破坏当前 `InputRole -> DecisionRole -> Roles` 边界
- 并行综合后，已进一步明确：
  - 若不先冻结“来源字典 + 控制域字典 + 仲裁原因字典”，Batch B 很容易再次退化成局部 if/else
  - 若第一版就直接引入多来源求和，会在当前缺乏 provenance 的前提下削弱可解释性
- 目标收缩后，当前 Phase B 的主要风险改为：
  - 若把多遥控器仲裁塞进 `RemoteControl`，会破坏模块边界
  - 若第一版就引入新 `RemoteMux` 模块，会扩大改动面并偏离“最小改动”
  - 若第一版做字段级混控或模拟量求和，会削弱安全性与可解释性
- 第一版实现完成后，当前剩余风险转移为：
  - secondary remote 仍默认关闭，后续需要结合真实空闲 UART 或新增别名再启用
  - 当前还没有来源解释字段，只能在代码结构层面保留主备切换逻辑
- 当前新阶段的主要风险为：
  - 若直接把 `VT13` 压扁进 `RemoteControlState`，会丢失键鼠/模式语义
  - 若因 `VT13` 接入而同时改 `InputRole -> DecisionRole` 主链，会扩大任务范围
- 第一版实现完成后，当前剩余风险转移为：
  - `VT13` 还未板级启用，仍缺实际 UART alias 与上板验证
  - `VT13State -> OperatorInputSnapshot` 的映射策略尚未冻结
- `USART2` 启用后，当前剩余风险改为：
  - `.ioc` 尚未同步，后续 CubeMX 再生成会覆盖手工接入的 `USART2`
  - `VT13State` 已进入 `InputRole`，但当前只做最小摇杆/模式/trigger 映射
  - `VT13` 的 mouse/key 高级语义尚未定义进入主链的策略
- 重新对齐当前真实基线后，后续主风险已重排为：
  - `SystemHealth` 尚未显式公开 `primary_reason / control_action_mask`
  - `TelemetryRole` 与 `DecisionRole` 虽已接入 helper，但对外契约还不够稳定
  - `InputRole` 仍有继续膨胀为协议堆积点的风险
  - `CANBridge` 仍只具备在线状态骨架，尚未形成最小真实桥接闭环
  - `referee` 第二轮稳定语义仍未补齐
  - bring-up 阶段的 `.ioc / 文档 / 手工接线` 仍未完全收口
- 主线程复核后，新增必须显式跟踪的细粒度风险为：
  - `HealthPolicyConfig.hpp` 当前直接 `#include "SystemHealth.hpp"`，因此 Batch 1 若直接把 `HealthPrimaryReason / ControlActionMask` 反向塞回 `SystemHealth.hpp`，会形成 include 循环
  - `Core/Src/usart.c` 中 `USART2 / USART3` 当前都表现为“先 `UART_MODE_TX_RX`，再在 USER CODE 强制收口到 `UART_MODE_RX`”，说明 `.ioc` 与运行时收口仍未统一
  - `basic_framework.ioc` 当前检索不到 `USART2`，但 `Core/Src/main.c` 已调用 `MX_USART2_UART_Init()`，说明 `VT13` 仍处于“生成文件已接入、配置源未收口”的危险态
  - `Core/Src/dma.c` 当前未检索到 `DMA1_Stream5` 或 `DMA1_Stream5_IRQn`，但 `USART2 RX` 已使用 `DMA1_Stream5`，需要在 Batch 5 显式回归 NVIC/DMA 一致性
  - `bridge offline` 当前在健康契约中仍属于 `Info` 且无控制动作，因此 Batch 3 不能私自提升其降级等级，必须先经过 Batch 1 契约变更
  - `Modules/Referee/Referee.hpp` 第二轮扩展要重点防止按帧更新顺序产生 `fire_allowed / remaining_heat` 的错误瞬态
- 当前实现推进后的新增状态为：
  - `Batch 3` 已完成最小 heartbeat 协议闭环、compile test 与 CRC-8 校验升级，当前剩余风险转为“双端 frame id 需要镜像配置、尚缺板上 CAN 抓包验证”
  - `Batch 4` 已完成 `RawStateCache -> DeriveStableState` 收口，当前剩余风险转为“尚未引入更细粒度的分帧时效/过期窗口策略”
  - `Batch 5` 已完成 first-pass：`.ioc` 回填 `USART2` 最小存在配置、README/交接材料同步；当前剩余风险转为“尚未完成一次真正的 CubeMX 再生成 + 上板回归，且 `USART2/USART3` 的 RX-only workaround 仍在 `usart.c` 用户区”
  - `Batch 6` 已完成 `RefereeConstraintView` 收口与 `DecisionRole / TelemetryRole` 回绑，当前剩余风险转为：
    - `TelemetryRole` 的裁判摘要位已可用，但尚未形成运行时集成测试
    - `RefereeConstraintView` 仍携带部分预算类数值，若要进一步去协议近邻化，需要配合 command schema 演进
  - `Batch 7` 已完成 `CANBridgeHealthView` 收口与 `TelemetryRole / SystemHealth` 回绑，当前剩余风险转为：
    - `can_bridge_degraded` 仍是 first-pass 规则，只表达“在线但已出现协议错误累计”
    - 若要更细地区分单向活性、持续 CRC 异常或 peer stall，需要在 bridge 模块内先做阈值化摘要，而不是直接把私有计数器抬到公共契约
  - `Batch 8` 已完成 `MasterMachineCoordinationView` 收口与 `InputRole / TelemetryRole` 回绑，当前剩余风险转为：
    - 当前只显式冻结了 `manual override only`，`assist` 仍未进入实现
    - 若未来要把 `master_machine` 升级成强依赖输入源，必须先决定它通过 `InputRole` 还是 `DecisionRole` 进入主链，不能直接扩字段
  - `Batch 9` 已完成 donor 二次裁剪，当前后续 backlog 已正式收敛为：
    - `Referee` 协议与约束层继续完善
    - `MasterMachine` 现役协议兼容继续完善
    - `CANBridge` 在真实跨板业务需求出现后的业务载荷扩展
    - `ChassisRole / GimbalRole / ShootRole` 继续吸收 donor 应用控制语义
    - `super_cap` 与确有需求时的非 DJI 通用电机模块
  - 当前 3 条 donor 应用控制语义主线已推进到新的基线：
    - `ChassisRole` 已具备 `follow gimbal` 偏角投影接线
    - `GimbalRole` 已具备 `pitch_motor` 真实执行链
    - `ShootRole` 已具备热量摘要、弹速映射、保守卡弹恢复的 first-pass 状态机
    - `super_cap` 建议作为下一轮 backlog，而不是插队到这 3 条执行链之前
  - 当前进一步深化后的状态为：
    - `ChassisRole` 已把 donor 的 `stop / follow gimbal / spin` 模式语义收口为 `ChassisControlHelpers`
    - `ShootRole` 已把 single/burst 的 shot count、cadence、dead time 收口为 helper，并让 jam recovery 期间的对外状态更可解释
    - 后续若继续沿顺序推进，优先级应转向：
      1. `super_cap`
  - 当前 `ChassisRole` 的更完整状态估计已完成 first-pass：
    - 新增 `ChassisStateEstimator`
    - wheel invalid 时的速度/角速度退化不再硬切
    - 后续若继续深化，重点转为：
      1. 部分轮可用时的降级融合
      2. 板上回放后的估计参数整定
      3. 再之后才进入 `super_cap`
  - 进入 `super_cap` 前的额外门槛已冻结：
    - 必须先由用户补充事项
    - 当前不得直接启动 `super_cap` 实现
  - 当前 `super_cap` 的规划状态已更新为：
    - 约束事项已确认
    - `Fan in / Fan out` 规划已完成
    - 进入实现前仍保持“当前阶段只做可观测模块”的边界
  - 当前 `super_cap` 已进入 first-pass 实现完成态：
    - donor 协议已适配为 `Modules/SuperCap`
    - `AppRuntime + TelemetryRole + SystemHealth` 已完成最小接线
    - 当前剩余风险转为：
      1. 缺少 `CAN1` 板上实测
      2. 仍未接入控制链路
      3. 若后续要扩 `supercap_degraded` 或接入 `DecisionRole`，必须单独立项
