# Findings

## Current Repository

- `App/Config/DegradedModeConfig.hpp` 已存在最小保守降级参数，但只覆盖限速、限射频与“是否允许无 referee 运动/射击”。
- `App/Runtime/HealthAggregator.hpp`、`App/Runtime/DegradedPolicy.hpp` 与 `App/Runtime/HealthPolicy_compile_test.cpp` 已经存在，说明：
  - “统一健康树 / 降级策略” 这条线不是从零开始；
  - 当前真正缺的不是新建 helper，而是把它们接入 `TelemetryRole` 与 `DecisionRole`；
  - 现有 Phase 4 计划需要从“create files” 改成 “wire existing policy into live roles”。
- `App/Roles/TelemetryRole.cpp` 已经在做第一版 `health fan-in`，能生成：
  - `online_source_mask`
  - `degraded_source_mask`
  - `fault_severity`
  - `recovery_hint`
- `App/Roles/DecisionRole.cpp` 已经消费 `system_health`，但降级判断仍是本地 `if/else`，尚未与 `TelemetryRole` 共享统一策略模型。
- `App/Roles/InputRole.cpp` 已具备 `remote` 与 `master_machine` 双输入来源，但目前仲裁仍是“主机手动覆盖即直接切走”，没有稳定的优先级、回退理由与来源解释。
- `Modules/CANBridge/CANBridge.hpp` 仍是状态骨架：
  - 已有白名单与在线超时
  - 尚无真正的协议打包、收发闭环与统计语义
- `Modules/MasterMachine/MasterMachine.hpp` 已经不是空壳，当前能解析一版 UART 输入协议，并通过白名单裁剪稳定语义后发布 `master_machine_state`。

## Donor Repository

- donor `daemon` 的核心职责是：
  - 在线计数器
  - 离线回调
  - 模块级“喂狗”
- donor `can_comm` 的核心职责是：
  - 结构体打包/解包
  - 帧头帧尾 + 校验
  - 在线状态检测
- donor `master_process` 的核心职责更偏向：
  - 视觉/上位机协议收发
  - 标志位解析
  - 姿态/目标数据交换
- 因此当前仓库的正确迁移方式不是“补齐 donor 模块目录”，而是：
  - 把 `daemon` 语义吸收到 `TelemetryRole + system_health + degraded policy`
  - 把 `can_comm` 语义吸收到 `Modules/CANBridge`
  - 把 `master_process` 可复用输入语义吸收到 `Modules/MasterMachine + InputRole`

## Planning Implication

- 下一主线应继续以 `health tree / degraded mode` 为中心，因为它已经在当前代码里形成半成品骨架。
- 当前主线需要再细分成两个子阶段：
  - 先完成 `HealthAggregator / DegradedPolicy -> TelemetryRole / DecisionRole` 的接线收口；
  - 再继续扩展 `InputRole` 仲裁与 `CANBridge` 真实协议闭环。
- `CANBridge` 真实闭环要放在统一健康契约之后，否则桥接在线/离线语义会再次散落。
- `referee` 第二轮语义补齐不应先于健康契约统一，否则又会把规则判断直接堆回 `DecisionRole`。

## Batch A Status

- `TelemetryRole.cpp` 已切到 `HealthAggregator::Evaluate(...)`，不再本地拼 `online_source_mask / degraded_source_mask / severity / recovery_hint`。
- `DecisionRole.cpp` 已切到 `ResolveDegradedPolicy()`，并把：
  - `should_force_safe` 映射到 `RobotMode`
  - `should_limit_motion` 映射到 `MotionCommand`
  - `should_block_fire` 映射到 `FireCommand`
- 当前 `Batch A` 已完成代码接线与编译验证。
- 仍然保留的下一批缺口：
  - `SystemHealth` 还不能显式对外发布 `primary_reason / control_action_mask`
  - `InputRole` 仍未升级为“可解释的输入仲裁器”

## rmpp-master Research

- `rmpp-master` 的多输入源不是单点 whole-source switch，而是：
  - `RC` 先把 `VT13 + FSi6X` 做设备级 fan-in
  - `Robot` 再把输入拆成 `rc / client / software` 三类 lane
  - `Chassis / Gimbal / Shooter` 按各自规则做 fan-out
- 其最有价值的不是具体兵种逻辑，而是“按控制域拆 lane”的思想：
  - motion lane
  - aim lane
  - fire lane
  - mode/safety lane
- 当前仓库不能平移 `rmpp::Robot` 的超级编排器设计，但应吸收其结构性经验：
  - `InputRole` 不应继续做 whole snapshot 切换
  - 应升级为按 domain 仲裁的 fan-in 层
  - 未来 automation input 应与 `remote / host` 并列预留 lane
- 因此下一轮的正确方向不是“remote vs host 三选一”，而是：
  - source-specific draft
  - domain-aware arbitration
  - enriched `OperatorInputSnapshot`

## rmpp-master N Researchers -> Synthesizer

- 并行研究后，3 条主线已经形成共识：
  - 设备层只应产出稳定输入快照，不应提前解释业务模式
  - 真正的架构价值在“按控制域分别仲裁”，而不是整源切换
  - `host assist / vision / automation` 必须是结构上的独立来源，不应伪装成新的 `remote`
- 相比上一轮，本轮进一步细化出：
  - `aim` 至少要拆成 `aim mode + aim speed + aim angle`
  - `fire` 至少要拆成 `fire enable + fire modulation`
  - `host` 至少要拆成 `host direct + host assist`
- 当前仓库下一轮更合理的第一版实现，不是 rmpp 式多来源求和，而是：
  - 先做“可解释的单域优先级仲裁”
  - 保留 future additive arbitration 的扩展位

## Scope Correction

- 用户当前真正关心的是：`rmpp-master` 如何处理“多遥控器接入”
- 用户当前不希望把这个问题扩展成“整条遥控 -> 控制链路优化”
- 因此下一轮应收缩为：
  - 多个遥控器如何进入当前人工输入入口
  - 第一版如何做主备/优先级切换
  - 如何保持现有 `basic_framework` 风格控制链基本不动
- 当前综合后的明确推荐是：
  - 方案落点选 `InputRole`
  - `RemoteControl` 只做最小多实例化改造
  - 第一版采用“单设备独占”，不做求和

## Multi-Remote First Pass

- `Modules/RemoteControl/RemoteControl.hpp` 已完成最小多实例化改造：
  - 新增 `RemoteControl::Config`
  - 允许实例级 `state_topic_name`
  - 允许实例级 `thread_name`
  - 保留旧四参构造，避免连锁改动
- `App/Roles/InputRole.hpp/.cpp` 已完成第一版主备切换落地：
  - `primary_remote_control_` 固定存在
  - `secondary_remote_control_` 为可选实例，默认关闭
  - `SelectActiveRemote()` 使用“单设备独占 + primary > secondary”
  - `master_machine` 继续在主备选择之后覆盖输入快照
- 当前第一版仍刻意保持：
  - 不扩 `OperatorInputSnapshot`
  - 不改 `DecisionRole`
  - 不引入 `RemoteMux/RemoteAggregator`
  - 不做多遥控器字段混控或模拟量求和

## DT7 / VT13 Migration Direction

- 用户已明确接受：`DT7State` 与 `VT13State` 分开
- 当前最合理的迁移方向是：
  - 现有 `RemoteControl.hpp` 收口为 `DT7.hpp`
  - 在同目录新增 `VT13.hpp`
  - 两个模块都作为独立协议适配器存在
  - 第一版不要求 `VT13` 立即映射进 `OperatorInputSnapshot`
- 当前要避免的错误方向是：
  - 把 `VT13` 硬塞进当前 `RemoteControlState`
  - 因为接入 `VT13` 而顺手重构整条控制链

## DT7 / VT13 Current Status

- `Modules/RemoteControl/DT7.hpp` 已落地，并接替原 `RemoteControl.hpp` 的现有职责。
- `Modules/RemoteControl/VT13.hpp` 已落地，并已通过最小 compile-check 进入编译链。
- `App/Roles/InputRole.hpp/.cpp` 已切到 `DT7` / `DT7State`。
- 当前仍刻意保持：
  - `VT13State` 尚未映射进 `OperatorInputSnapshot`
  - `InputRole` 仍只消费 `DT7State`
  - `VT13` 当前已板级启用到 `USART2 -> uart_vt13`，但尚未映射进 `InputRole`

## VT13 on USART2

- 当前工程已补齐 `USART2` 板级初始化：
  - `921600`
  - `8N1`
  - 当前在用户区收敛为 `RX only`
  - `DMA1_Stream5` 作为 `USART2_RX`
- `User/app_main.cpp` 已注册：
  - `usart2`
  - `uart_vt13`
- `AppRuntime` 已实例化 `VT13` 模块，因此 `VT13.hpp` 不再只是“编译存在”，而是实际运行时对象。
- 当前仍有一个后续收口点：
  - `.ioc` 尚未同步到 `USART2` 配置，后续若重新生成 CubeMX 代码，需要一并回补

## VT13 First-pass Ingress Mapping

- `InputRole` 已开始最小消费 `VT13State`，并保持边界不扩散：
  - 不改 `DecisionRole`
  - 不扩 `OperatorInputSnapshot`
  - 不消费 VT13 的 mouse/key 高级语义
- 当前人工输入优先级已固定为：
  - `DT7 primary > DT7 secondary > VT13 > none`
  - 之后才允许 `master_machine` 做最终覆盖
- 输入模块持有边界已收紧为：
  - `AppRuntime` 统一持有 `DT7 / VT13`
  - `InputRole` 只订阅 `DT7State / VT13State / MasterMachineState`
- VT13 当前第一版映射只覆盖：
  - `request_safe_mode`
  - `request_manual_mode`
  - `friction_enabled`
  - `fire_enabled`
  - `target_vx_mps / target_vy_mps`
  - `target_yaw_deg / target_pitch_deg`
  - 并保持 `target_wz_radps = 0`、`track_target = false`

## 2026-04-22 Planning Rebase

- 重新核对 `.Doc/road_map.md`、`task_plan.md`、`progress.md` 与当前代码结构后，已确认：
  - 旧的 Phase 2-5 / Phase 4-6 执行卡片中，存在明显“落后于当前代码基线”的问题
  - 例如它们仍假设需要“创建 `Referee` / `MasterMachine` / `CANBridge` / `HealthAggregator` 骨架”
  - 这些假设已经不再成立，会误导后续会话重复规划已完成阶段
- 当前更合理的剩余迁移拆分应改为：
  - `Batch 1`: `SystemHealth` 契约补齐与 health-to-action 显式化
  - `Batch 2`: `InputRole` 输入扇入边界收口
  - `Batch 3`: `CANBridge` 最小真实协议闭环
  - `Batch 4`: `referee` 第二轮稳定语义
  - `Batch 5`: bring-up 清理与 `.ioc` / 文档同步
- 该重排继续遵守原有 fan-in / fan-out 主线：
  - `Batch 1` 对应 `Flow C + Flow D`
  - `Batch 2` 对应 `Flow A`
  - `Batch 3` 对应 `Flow F`
  - `Batch 4` 对应 `Flow B`
  - `Batch 5` 为跨流向收口阶段
- 新计划已落文档：
  - `.Doc/2026-04-22_remaining_fan_in_fan_out_execution_plan.md`

## 2026-04-22 Subagent Refinement Review

- 已按 `Batch 1/2`、`Batch 3/4`、`Batch 5 + 验证矩阵` 三路下放给 subagents 细化，再由主线程复核并收口。
- 主线程已复核确认的关键事实：
  - `App/Runtime/HealthAggregator.hpp` 已经产出 `primary_reason / control_action_mask`，但 `App/Topics/SystemHealth.hpp` 还未承载它们。
  - `App/Config/HealthPolicyConfig.hpp` 当前直接 `#include "SystemHealth.hpp"`，因此 Batch 1 必须先处理类型归属或依赖方向，不能简单互相包含。
  - `Core/Src/main.c` 已调用 `MX_USART2_UART_Init()`，但 `basic_framework.ioc` 当前检索不到 `USART2`，说明 `VT13` 的 CubeMX 配置源还没回填。
  - `Core/Src/usart.c` 当前对 `USART2 / USART3` 都存在“先 `UART_MODE_TX_RX`，再在 USER CODE 强制收口到 `UART_MODE_RX`”的 bring-up workaround。
  - `Core/Src/usart.c` 已把 `USART2 RX` 绑到 `DMA1_Stream5`，`Core/Src/stm32f4xx_it.c` 已有 `DMA1_Stream5_IRQHandler()`，但 `Core/Src/dma.c` 当前未检索到 `DMA1_Stream5` 或 `DMA1_Stream5_IRQn`。
  - `User/app_main.cpp` 当前已注册：
    - `uart_master_machine`
    - `uart_vt13`
    - `uart_referee`
- 细化后的剩余计划已进一步冻结为：
  - `Batch 1`: `SystemHealth` 契约补齐，重点是字段补齐、去二次推导、冻结 health-to-action 表
  - `Batch 2`: `InputRole` 只保留“来源选择 + 最小映射”，协议映射抽 helper，冻结 provenance 为私有诊断量
  - `Batch 3`: `CANBridge` 只迁移 donor `can_comm` 的通用桥接语义，先冻结 ID/online 口径，再补最小 RX/TX 闭环
  - `Batch 4`: `referee` 第二轮只补稳定领域语义，模块内部统一派生，Role 只消费稳定语义
  - `Batch 5`: `.ioc`、DMA/NVIC、文档、临时 workaround 与再生成回归门统一收口

## 2026-04-22 Batch 3/4/5 Execution Notes

- `Batch 3` 当前已落地为最小 bridge heartbeat 协议：
  - `CANBridgeProtocol::HeartbeatFrame` 固定为 8 字节
  - 当前字段为 `magic/version/seq/flags/rx_count/tx_count/peer_seq/checksum`
  - 当前 checksum 已升级为 CRC-8，可满足当前这轮跨板协议 bring-up 的基本一致性要求
- `Batch 3` 当前 `online` 口径已收口为：
  - 若方向允许 RX，则以“最近有效 RX 是否仍在超时窗内”为准
  - 仅在 `TX-only` 模式下，才退化为“最近 TX 是否仍在超时窗内”为准
- `Batch 4` 当前已把 `Referee` 的派生语义收口为：
  - `RawStateCache`
  - `ApplyGameStatusFrame / ApplyRobotStatusFrame / ApplyPowerHeatFrame / ApplyBuffFrame`
  - `DeriveStableState`
- `Batch 4` 当前冻结的关键保证是：
  - `fire_allowed` 不再受 `0x0201 / 0x0202 / 0x0204` 到达顺序影响
  - 缺少关键帧时保持保守
  - 离线后清空 raw cache，避免旧状态跨离线泄漏
- `Batch 5` 当前主线程已复核并更新的事实为：
  - `basic_framework.ioc` 之前确实没有 `USART2`
  - 现在已手工回填 `USART2` 的最小存在配置、RX DMA request 与 `USART2_IRQn`
  - `README.md` 已补入当前 UART/别名/波特率/DMA 基线与再生成注意事项
  - `USART2 / USART3` 的根因收口仍未完成：当前 `.ioc` 只补到“存在与接线事实”，尚未消除 `usart.c` 里 USER CODE 区的 `RX only` workaround

## 2026-04-22 Batch 6 Execution Notes

- `Batch 6` 当前已把 `Referee` 对上层的消费收口为统一稳定约束视图：
  - 新增 `RefereeConstraintView`
  - `DecisionRole` 现在消费：
    - `motion_scale`
    - `max_fire_rate_hz`
    - `referee_allows_fire`
    - `chassis_power_limit_w`
    - `remaining_heat`
  - 不再直接在 `DecisionRole` 内解释：
    - `buffer_energy`
    - `remaining_energy_bits`
    - `shooter_cooling_value`
    - `match_started`
- `TelemetryRole` 当前已补 3 个摘要级裁判约束位：
  - `referee_match_gate_active`
  - `referee_fire_gate_active`
  - `referee_power_gate_active`
- 主线程 review 后额外修正了一点：
  - 离线时，上述 3 个摘要位不应全部置为 active
  - 离线故障仍主要由 `referee_online == false` 与 `system_health.primary_reason == kRefereeOffline` 表达
- 当前仍保留的后续收口点：
  - `RefereeConstraintView` 仍携带部分预算类数值，不是“纯布尔语义视图”
  - 若后续要继续收口，可能需要同步演进 `FireCommand / MotionCommand` schema

## 2026-04-22 Batch 7 Execution Notes

- `Batch 7` 当前已完成最小 bridge 健康摘要折叠层：
  - 新增 `CANBridgeHealthView`
  - 当前只把 `CANBridgeState` 折叠成：
    - `online`
    - `degraded`
- `SystemHealth` 当前已补：
  - `can_bridge_degraded`
- 当前 `degraded` 口径保持保守：
  - 仅表达“bridge 在线，但已经出现协议错误累计”
  - 不表达计数值、不表达时间戳、不表达序号
- 当前明确继续保留在私有状态、**没有** 升级进 `SystemHealth` 的 bridge 字段有：
  - `tx_frame_count`
  - `rx_frame_count`
  - `crc_error_count`
  - `last_tx_time_ms`
  - `last_rx_time_ms`
  - `last_activity_time_ms`
  - `last_tx_seq`
  - `last_rx_seq`
- `DecisionRole` 当前仍只消费 `SystemHealth` 的稳定健康摘要，没有直接依赖 `CANBridgeState`
- 当前剩余收口点：
  - `can_bridge_degraded` 现在还是 first-pass 规则，后续若要更精确地区分单向失活 / sustained CRC anomaly，需要在 bridge 模块内先做阈值化折叠

## 2026-04-22 Batch 8 Execution Notes

- `Batch 8` 当前已把 `MasterMachine / Input / Health` 的上层协同关系从“隐式假设”推进成“显式策略接口”：
  - 新增 `MasterMachineCoordinationView`
  - 通过 `MasterMachineIngressMode` 冻结当前支持的模式
- 当前明确冻结的语义为：
  - `kManualOverrideOnly`
  - 默认 `master_machine_required = false`
  - `master_machine` 只能经由 `InputRole -> OperatorInputSnapshot` 影响控制输入
- `InputRole` 当前不再自己散写“是否允许 host override”的逻辑，而是统一经由 `MasterMachineCoordinationView`
- `TelemetryRole` 当前不再硬编码 `master_machine_required = false`，而是经由同一视图得出当前策略下的结果
- 当前仍保留的后续收口点：
  - `kAssistReserved` 还没有实际实现，只是保留明确扩展位
  - 若后续真的要支持 assist/强依赖模式，需要先明确它进入 `InputRole` 还是 `DecisionRole` 的边界，再扩该视图与配置

## 2026-04-22 Role Execution Semantics Follow-up

- `ChassisRole`
  - 当前已补 `follow gimbal` 模式下的速度投影基础接线：
    - 新增对 `gimbal_state` 的订阅
    - 当 `motion_mode == kFollowGimbal` 时，使用云台相对 yaw 做速度向量投影
  - 当前仍未完成的后续价值主要是：
    - donor 更完整的状态估计
    - 更完整的 `spin / follow / independent` 细粒度切换策略
- `GimbalRole`
  - 当前已补 `pitch_motor` 真实执行链：
    - `AppRuntime` 已实例化 `pitch_motor`
    - `GimbalRole` 现在同时控制 `yaw_motor / pitch_motor`
    - `pitch_deg` 不再只是命令占位，而是由 BMI088 外反馈估计驱动
  - 当前 pitch 估计采用保守的互补滤波 first-pass，不等价于 donor INS 全量姿态链
- `ShootRole`
  - 当前已补：
    - 弹速离散映射：`15 / 18 / 30 m/s`
    - `heat = shooter_heat_limit - remaining_heat`
    - 保守的卡弹检测与短时反转恢复
  - 当前 jam 逻辑仍是 first-pass：
    - 只基于“拨盘长时间低速且未到位”
    - 后续若有更完整的编码器/裁判/弹丸反馈链，还可以继续增强
- `super_cap`
  - 当前建议进入**下一轮 backlog**
  - 但优先级排在当前 3 条执行链之后：
    1. `GimbalRole` pitch 已闭合
    2. `ShootRole` 状态机已 first-pass
    3. `ChassisRole` donor 剩余底盘语义继续吸收
    4. 再进入 `super_cap`
  - 原因是：
    - `super_cap` 更适合以“模块私有状态 + Telemetry/Decision 语义折叠”方式进入
    - 它不会阻塞当前 3 条执行链闭环

## 2026-04-22 Batch 9 Synthesis

- Batch 9 当前已正式把 donor 剩余内容分成三类：
  - 继续迁移
  - 只迁义
  - 明确放弃
- 已冻结的核心结论是：
  - donor `application/*` 不再按文件迁移，而只继续提炼 `Role` 所需的控制语义
  - donor `message_center / daemon / standard_cmd / unicomm` 从现在开始视为明确放弃实现迁移
  - donor `referee / master_machine / can_comm` 只保留协议与约束层继续吸收
  - donor `super_cap` 与“确有硬件需求时”的非 DJI 通用电机模块可以保留在后续 backlog
- Batch 9 之后，主迁移链路正式收敛为：
  - 通信模块
  - Role 编排
  - 健康聚合
  这三条线

## 2026-04-22 ChassisRole Deeper Absorption

- `ChassisRole` 当前已进一步把 donor 底盘执行语义收口到 `ChassisControlHelpers`：
  - `stop`
  - `follow gimbal`
  - `spin`
  这 3 类模式不再是散落在 `Role` 主体里的隐式分支
- 当前 `follow gimbal` 的最小真实语义是：
  - 使用 `gimbal_state.yaw_deg` 作为跟随偏角
  - 使用 donor 同源的二次项思路生成底盘角速度
  - 再与现有车体系速度投影和轮速解算统一组合
- 这意味着 donor `application/chassis/chassis.c` 中最关键的“跟随云台偏角 + 正运动学解算”已经在当前仓库形成明确落点：
  - `ChassisControlHelpers`
  - `ChassisRole`
- 当前仍保留的底盘后续收口点：
  - 更完整的速度/姿态估计
  - 更丰富的 `spin / follow / independent` 切换策略
  - 若后续接入 `super_cap / referee` 的更细功率策略，再继续增强输出限幅

## 2026-04-22 ShootRole Deeper Absorption

- 这轮 `ShootRole` 已继续深化到“可解释的 first-pass 状态机”：
  - `single / burst` 的语义 helper 已冻结：
    - shot count
    - cadence
    - dead time
  - `ShootRole` 当前通过 `next_single_step_allowed_ms_` 保证 single/burst 在按住不放时不会重复触发
- jam recovery 期间当前对外语义已更明确：
  - `loader_mode = kReverse`
  - `loader_active = true`
  - `jammed = true`
  - `fire_enable = false`
- 这意味着当前 `ShootRole` 已不再只是“有热量摘要和反转恢复”，而是进入了 donor `shoot.c` 中“不应期 + 拨盘恢复状态机”的第一轮可解释实现
- 当前仍保留的后续收口点：
  - single/burst cadence 仍是 first-pass 常量，不等价于最终实机节奏
  - jam 判断仍只基于“低速 + 未到位 / 连续拨盘”保守逻辑
  - 若后续有更完整的编码器/弹丸反馈链，可以继续提高 jam 判定精度

## 2026-04-22 ChassisState Estimation Enhancement

- 这轮 `ChassisRole` 的状态估计已从“硬切回命令值”升级为统一 estimator 驱动：
  - 新增 `ChassisStateEstimator`
  - 当前 helper 已覆盖：
    - 轮速反解为机体系 `vx / vy / wz`
    - gyro `yaw` 积分与包角
    - 轮反馈失效时的平滑退化
- 这意味着当前 `ChassisState` 中：
  - `estimated_vx_mps / estimated_vy_mps`
  - `feedback_wz_radps`
  已经不再在 wheel invalid 时直接突变到命令值
- 当前保守冻结的边界仍然是：
  - `wheel_feedback_valid` 仍要求 4 轮全有效
  - 无 gyro 且无轮反馈时，`wz` 仍只是退化估计，不应视为真实角速度
  - 退化参数 `5.0 Hz / 2.5 Hz` 仍是启发式常量，尚未做板上回放调参

## 2026-04-22 super_cap Reminder Gate

- 根据用户最新约束，进入 `super_cap` 之前必须先提醒用户补充事项。
- 当前结论是：
  - `super_cap` 仍在下一轮 backlog 中
  - 但**尚未开始任何实现**
  - 下一轮只有在用户补充：
    - 目标协议/模块来源
    - 硬件接线与 CAN ID
    - 想接入 `Telemetry / Decision / SystemHealth` 的哪一层
  之后，才会正式进入 `Fan in / Fan out` 规划

## 2026-04-22 super_cap Fan in / Fan out Planning

- 用户已经明确确认：
  - 协议源固定为 `Chassis/modules/super_cap`
  - 物理链路固定 `CAN1`
  - 当前阶段只把它作为简单通信模块
  - 当前阶段只要求“先把数据读出来”，暂不接入控制链路
- 因此 `super_cap` 当前阶段的正确落点已冻结为：
  - `Modules/SuperCap`：协议接收、状态发布、最小安全发送接口
  - `TelemetryRole / SystemHealth`：只接最小可观测摘要，优先 `supercap_online`
- 当前阶段明确不做：
  - 不迁 donor `power_controller`
  - 不把 `cap_energy / chassis_power / chassis_power_limit` 直接接进 `DecisionRole / ChassisRole`
  - 不扩 `MotionCommand / ChassisState / FireCommand` 契约
- 当前阶段完成后，`super_cap` 才算从 backlog 进入真正可执行状态，但仍只是“可观测模块”，不是控制链路输入

## 2026-04-22 super_cap First-pass Implementation

- `super_cap` 当前已完成 first-pass 实现：
  - `Modules/SuperCap/SuperCap.hpp`
  - `App/Runtime/SuperCapProtocol_compile_test.cpp`
  - `App/Runtime/SuperCapHealthView_compile_test.cpp`
  - `AppRuntime` 已实例化 `SuperCap`
  - `TelemetryRole` 已订阅 `SuperCapState`
- 当前已经落地的事实是：
  - donor `super_cap` 协议已被适配为本仓库的模块私有状态
  - 物理链路固定 `CAN1`
  - `SystemHealth` 只接入 `supercap_online`
- 当前仍明确**没有**做的事：
  - 没有把 `chassis_power / chassis_power_limit / cap_energy` 接进 `DecisionRole`
  - 没有把 `super_cap` 接进 `ChassisRole` 的功率控制
  - 没有迁 donor `power_controller`
- 当前剩余收口点：
  - 需要后续做 `CAN1 + 0x051 / 0x061` 的板上实测
  - 如果后续要把 `super_cap` 变成健康摘要中的更强语义，才考虑 `supercap_degraded`
  - 如果后续要让它进入控制链，必须单独立项

## 2026-04-28 referee / master_machine / can_comm Controller Rebind

- 当前真实代码已经切到 `Controller` 体系，旧计划中的 `InputRole / DecisionRole / TelemetryRole` 名称只能作为历史记录，不应继续指导新实现。
- `RefereeConstraintView` 已被 `DecisionController` 使用来裁剪 `FireCommand.fire_enable`，裁判 fire gate 不再只依赖 `ShootController` 的二次兜底。
- `MasterMachineState` 中已有 `vision_target_*` 字段，本轮将其作为白名单控制的 aim lane 输入：
  - 只有 `MasterMachineInputField::kVisionTarget` 允许且目标有效时才启用；
  - 启用后 `OperatorInputSnapshot.track_target = true`；
  - `DecisionController` 将其映射为 `AimModeType::kAutoTrack`。
- `OperatorInputSnapshot` 新增 `control_source`，使 `DT7 / VT13 / MasterMachine` 分别能表达为 `Remote / KeyboardMouse / Host`。
- `CANBridge` 本轮保持模块通用能力边界，只增加状态只读访问口；没有把 CAN payload 绑定到任何 App 业务命令。

## 2026-04-28 master_machine status feedback loop

- 新增 `App/Cmd/MasterMachineBridgeController` 作为 App 层桥接控制器，职责是把稳定状态折叠成 `MasterMachine::VisionStatusSummary` 并调用 `SendVisionStatus()`。
- blocking 风险 `MasterMachineConfig.direction = kRxOnly` 没有通过修改全局默认解决，而是在 `AppRuntime` 局部 `MakeMasterMachineConfig()` 中提升为 `kBidirectional`，从而保留 RX 并允许最小 TX 回传闭环。
- 为避免跨线程直接读 `master_machine_.State()` 带来的竞态，控制器最终订阅了 `master_machine_.StateTopic()`，而不是长期持有模块内部状态引用。
- 当前回传字段采用保守映射：
  - `enemy_color = 0`
  - `work_mode` <- `RobotMode`
  - `target_type` <- `MasterMachineState.vision_target_type`（仅当在线且目标有效）
  - `target_locked` <- `online && vision_target_valid && gimbal_state.ready && aim_mode == kAutoTrack`
  - `bullet_speed_mps` <- `ShootState.bullet_speed_mps`（仅 `shoot_state.ready`）
  - `robot_yaw_deg` <- `InsState.yaw_deg`（需 `online && gyro_online`）
  - `robot_pitch_deg / roll_deg` <- `InsState`（需 `online && accl_online`）
- 当前回传路径新增了 50ms 节流，避免按 `OnMonitor()` 每拍都写 UART。
