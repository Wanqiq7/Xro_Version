# rmpp-master 多输入源 / 遥控链路调研（fan-in / fan-out）

## 1. 调研目标

本调研只关注 `rmpp-master` 中与“多输入源汇聚、遥控链路、命令扇出”相关的部分，不讨论其完整驱动层与兵种业务实现。

目标是回答 3 个问题：

1. `rmpp-master` 如何处理多输入源 fan-in？
2. 它如何把这些输入 fan-out 到底盘 / 云台 / 发射机构？
3. 其中哪些语义适合迁移到当前 `Xro_template`，哪些必须拒绝平移？

---

## 2. rmpp 的输入链路总览

从代码结构看，`rmpp-master` 的输入链路分成三层：

```text
原始设备输入
-> RC / Mavlink
-> Robot 内部的 rc / client / software 三类控制通道
-> Chassis / Gimbal / Shooter
```

对应代码落点：

- 原始遥控器适配：
  - `rmpp/lib/rc/FSi6X.*`
  - `rmpp/lib/rc/VT13.*`
- 第一层输入汇聚：
  - `rmpp/lib/rc/RC.*`
- 第二层命令编排与扇出：
  - `rmpp/lib/robot/Robot.*`
- 第三层“软件输入”特化：
  - `rmpp/lib/robot/Sentry.*`
- 外部自动化 / 上位机输入：
  - `rmpp/lib/mavlink/Mavlink.*`

这意味着 `rmpp` 的“多输入源”不是单点仲裁器，而是：

- 先做设备级 fan-in
- 再做按执行域拆开的多通道叠加 / 优先级处理

---

## 3. rmpp 的 Fan-in 结构

## 3.1 Device Fan-in：`RC`

`RC` 是 `rmpp` 的第一层输入汇聚器。

它做的事情不是“在遥控器 A / B 之间二选一”，而是：

- 同时轮询 `VT13` 与 `FSi6X`
- 把两个设备的输出归一化到统一字段：
  - `is_enable`
  - `is_rub`
  - `is_shoot`
  - `is_auto_aim`
  - `is_fn`
  - `is_calib`
  - `x / y / r / yaw / pitch / shoot`
  - `mouse`
  - `key`
- 对模拟量采取“先清零，再叠加”的策略
- 对功能位采取“按来源置位”的策略

也就是说，`RC` 的本质不是“source switch”，而是“同类设备 fan-in + 统一语义归一化”。

这一点很重要，因为它说明：

- 输入归一化可以发生在设备层
- 但这里归一化后的输出仍然是“操作者意图”，不是底盘/云台/射击的最终命令

## 3.2 External Assist Fan-in：`Mavlink`

`Mavlink` 在 `rmpp` 中不是通用总线，而是一个外部辅助输入源容器。

它承接的典型输入包括：

- `vision`
  - `is_detect`
  - `yaw`
  - `pitch`
  - `is_fire`
  - `robot_id`
  - `wr`
  - `distance`
- `insta360`
- `chassis_speed`
- `current_position`

这些输入不会直接驱动执行器，而是先挂在 `device.mavlink.*` 下，再由 `Robot::handleRC()`、`handleClient()` 或 `Sentry::*` 消费。

所以它是：

- 外部辅助输入 fan-in
- 但不是最终决策出口

## 3.3 Intent Fan-in：`Robot`

`Robot` 才是 `rmpp` 真正的“输入语义再编排层”。

它没有把所有输入揉成一个 struct，而是拆成按执行域划分的三组通道：

- 底盘：
  - `vx.rc / vx.client / vx.software`
  - `vy.rc / vy.client / vy.software`
  - `wr.rc / wr.client / wr.software`
- 云台：
  - `gimbal_mode.rc / client / software`
  - `wyaw.rc / client / software`
  - `wpitch.rc / client / software`
  - `yaw.rc / client / software`
  - `pitch.rc / client / software`
- 发射机构：
  - `is_rub.rc / client / software`
  - `is_shoot.rc / client / software`

这相当于把输入来源按“控制域”拆成多个 lane。

这里的三个来源含义非常明确：

- `rc`：遥控器摇杆 / 拨杆意图
- `client`：键鼠 / 客户端交互意图
- `software`：自动化 / 兵种状态机 / 外部辅助策略意图

这是 `rmpp` 对多输入源最值得借鉴的地方。

---

## 4. rmpp 的 Fan-out 结构

## 4.1 Chassis Fan-out

`Robot::handleChassis()` 的核心模式是：

- 先把 `vx / vy / wr` 三个来源相加
- 再统一 clamp
- 最后下发到 `device.chassis`

也就是：

```text
rc motion + client motion + software motion
-> clamp
-> chassis.SetSpeed(...)
```

这属于“同维度命令叠加后扇出”。

## 4.2 Gimbal Fan-out

`Robot::handleGimbal()` 的模式更复杂：

- 若 `rc` 请求 angle mode，则 `rc` 优先
- 否则若 `client` 请求 angle mode，则 `client` 优先
- 否则若 `software` 请求 angle mode，则 `software` 优先
- 若都不是 angle mode，则速度量 `wyaw / wpitch` 做叠加后扇出

也就是说，云台不是简单求和，而是：

- 模式优先级先决
- 速度模式才叠加

这说明“多输入源仲裁”必须支持按域、按模式的不同规则。

## 4.3 Shooter Fan-out

`Robot::handleShooter()` 的模式是：

- `rub` 使用布尔 OR
- `shoot` 则区分：
  - 若 `is_shoot.rc`，用遥控链路并允许 `pitch` 摇杆复用弹频
  - 若 `client` 或 `software`，则走固定弹频

这说明：

- 射击域的仲裁规则和底盘 / 云台完全不同
- 发射域非常依赖来源解释，而不是只看一个 `bool`

---

## 5. rmpp 的关键优点

从当前迁移目标看，`rmpp` 有 4 个值得吸收的优点：

1. **设备层先归一化**

`VT13`、`FSi6X` 先各自做协议解析和断联检测，再由 `RC` 做统一语义汇聚。

2. **按控制域拆 lane**

不是一份“大输入状态”直接覆盖整机，而是分成：

- motion lane
- aim lane
- fire lane

3. **允许不同域采用不同仲裁规则**

- chassis：求和 + clamp
- gimbal：模式优先级 + 速度叠加
- shooter：布尔 OR + 来源特化

4. **为 future automation 留了稳定位置**

`software` 不是临时特判，而是结构上与 `rc / client` 并列的一层。

---

## 6. rmpp 中不能平移到当前仓库的部分

尽管 `rmpp` 的输入链路很完整，但不能直接搬进 `Xro_template`。

必须拒绝平移的点有：

## 6.1 不能把 `Robot` 变成新的超级编排器

当前仓库已经固定为：

```text
InputRole -> DecisionRole -> ChassisRole / GimbalRole / ShootRole
```

因此不能把 `rmpp::Robot` 那种：

- 输入汇聚
- 模式判断
- 命令叠加
- 执行器直达

重新做成一个中央 God Object。

## 6.2 不能在 InputRole 中塞入执行域业务逻辑

`rmpp` 的 `handleRC / handleClient` 已经夹带了较多业务语义，例如：

- 自瞄右键 + 左键强制开火
- `z/c` 调弹速/弹频
- `r` 一键掉头
- `pitch` 摇杆复用射频

这些在当前架构里不能直接塞进 `InputRole`。

原因是：

- `InputRole` 应只负责意图 fan-in
- `DecisionRole` 才负责高层业务命令
- `ShootRole / GimbalRole / ChassisRole` 才负责执行域编排

## 6.3 不能照搬 `rc / client / software` 的成员布局

`rmpp` 在 `Robot` 内直接持有大量 `*.rc / *.client / *.software` 字段。

当前仓库更适合做成：

- `source-specific draft`
- `domain-specific arbitration result`
- 最终汇总到 `OperatorInputSnapshot`

而不是把 `Robot` 的内部状态布局原样复制。

## 6.4 不能让外部辅助输入直接成为执行器控制入口

`rmpp` 中 `vision / current_position / chassis_speed` 会被 `Robot` 或 `Sentry` 直接消费。

当前仓库必须先经过：

- `Module private typed topic`
- `InputRole` 或未来 automation input adapter
- `DecisionRole`

才能进入公共命令链。

---

## 7. 对当前仓库的直接启发

## 7.1 当前 InputRole 的问题

当前 `InputRole` 只有两种处理方式：

- `master_machine` 满足条件时，整份快照直接切到 host
- 否则整份快照直接来自 remote

这是“整源切换”，不是“按控制域 fan-in”。

它的问题是：

- 无法解释当前是谁在控制哪一个域
- 无法表达“remote 控模式 + host 给速度目标”的混合输入
- 无法为 future automation 留 lane
- `DecisionRole` 只能从最终结果倒推来源，语义很弱

## 7.2 当前仓库最应该借鉴 rmpp 的点

当前仓库不该照搬 `rmpp::Robot`，但应该借鉴它的“lane 思维”：

- motion lane
- aim lane
- fire lane
- mode/safety lane

每个 lane 都有：

- 来源
- 是否有效
- 是否允许覆盖
- 回退原因

然后再由 `InputRole` 统一汇聚。

---

## 8. 细化后的升级方案

下面给出面向当前仓库的 `InputRole` 与整条遥控链路升级方案。

## Phase B1: 建立输入源与仲裁语义字典

建议新增一组稳定语义：

- `InputSourceKind`
  - `kNone`
  - `kRemoteStick`
  - `kRemoteMouseKeyboard`
  - `kHost`
  - `kAutomation`
- `InputArbitrationReason`
  - `kNominal`
  - `kRemoteOffline`
  - `kRemoteSafeSwitch`
  - `kHostManualOverride`
  - `kAutomationAssist`
  - `kForcedSafeByHealth`
- `InputDomainMask`
  - `kInputDomainMode`
  - `kInputDomainMotion`
  - `kInputDomainAim`
  - `kInputDomainFire`

这些语义不一定都要公开成公共 Topic，但至少要在 `InputRole` 内部与 `OperatorInputSnapshot` 中稳定存在。

## Phase B2: 引入 source-specific draft，而不是直接写最终快照

建议把 `InputRole` 内部输入处理分成三步：

1. `RemoteControlState -> RemoteInputDraft`
2. `MasterMachineState -> HostInputDraft`
3. `FutureAutomationState -> AutomationInputDraft`

每个 draft 只表达来源自己的原始意图，不做跨来源覆盖。

例如：

- `RemoteInputDraft`
  - safe / manual / friction / fire
  - stick motion
  - mouse aim
  - keyboard hotkeys
- `HostInputDraft`
  - manual_override
  - fire_enable
  - target_vx / vy / yaw / pitch
- `AutomationInputDraft`
  - track_target
  - auto aim pose
  - fire window

## Phase B3: InputRole 内部改为 domain-aware arbitration

不要再做：

```text
if (use_master_machine) {
  whole snapshot = host;
} else {
  whole snapshot = remote;
}
```

应改为：

- 先决安全层：
  - health force-safe
  - remote safe switch
  - remote offline
- 再做 domain 级仲裁：
  - mode/safety domain
  - motion domain
  - aim domain
  - fire domain

第一版建议优先级：

- `safe fallback` > `remote emergency/safe` > `host manual override` > `remote manual` > `automation assist`

注意：

- 这里的“优先级”不应强制整源覆盖
- 而应允许“按域选择来源”

## Phase B4: 扩展 OperatorInputSnapshot，让它能解释来源

当前 `OperatorInputSnapshot` 只保留了值，没有来源解释。

建议至少补：

- `active_control_source`
- `active_motion_source`
- `active_aim_source`
- `active_fire_source`
- `arbitration_reason`
- `source_online_mask`
- `source_override_mask`

这样 `DecisionRole` 才能正确设置：

- `RobotMode.control_source`
- future telemetry / health 对 control link 的解释

## Phase B5: 重新定义遥控链路边界

建议把整条遥控链路分成 4 层：

### Layer 1: Device Adapter

- `RemoteControl`
- `MasterMachine`
- future `AutomationInput`

职责：

- 协议解析
- 在线检测
- 私有 typed topic 发布

### Layer 2: Source Draft Builder

位于 `InputRole` 内部或 `App/Runtime/InputDraft*`

职责：

- 把模块私有状态转成统一 operator intent draft

### Layer 3: Domain Arbitrator

位于 `InputRole`

职责：

- 按 domain 做优先级与回退选择

### Layer 4: Decision Fan-out

位于 `DecisionRole`

职责：

- 把稳定输入快照转换为：
  - `robot_mode`
  - `motion_command`
  - `aim_command`
  - `fire_command`

---

## 9. 推荐实施顺序

结合当前仓库状态，建议下一轮按下面顺序推进：

1. 先补 `InputPolicyConfig.hpp`
   - 冻结来源字典、domain 字典、仲裁原因字典
2. 扩 `OperatorInputSnapshot.hpp`
   - 增加来源解释字段
3. 在 `InputRole.cpp` 内引入 source draft + domain arbitration
   - 先只覆盖 `remote + master_machine`
4. 再让 `DecisionRole` 使用新的来源解释
   - 修正 `RobotMode.control_source`
5. 最后视需要把 `SystemHealth` 与 `TelemetryRole` 接到新的 `source_online_mask`

---

## 10. 最终结论

对当前仓库最有价值的，不是 `rmpp` 的兵种业务代码，而是它对多输入源的两个结构性认知：

1. **输入不应该只按“来源”理解，而应该按“控制域”理解。**
2. **自动化输入必须从结构上占一个 lane，而不是将来临时插特判。**

因此，当前 `InputRole` 的正确升级方向不是：

- 从 `remote vs host` 二选一变成三选一

而是：

- 从“整份快照切换”升级为“按控制域做 fan-in 的仲裁器”。

这也是当前仓库在不破坏 `InputRole -> DecisionRole -> Roles` 边界下，最适合吸收 `rmpp-master` 架构价值的方式。

---

## 11. N Researchers -> Synthesizer 综合结论

这一节不是单次代码阅读结果，而是把 3 条独立研究主线综合起来后的最终判断：

- Researcher A：设备级输入 fan-in
- Researcher B：`Robot` 级按域 fan-in / fan-out
- Researcher C：automation / host assist / 外部辅助输入

综合后，可以把 `rmpp-master` 的输入链路抽象成 3 个正交维度：

1. **设备输入层（device input）**
2. **意图来源层（intent source）**
3. **控制域层（control domain）**

### 11.1 设备输入层

这一层只回答：

- 设备是否在线
- 原始输入是否有效
- 归一化后的稳定快照是什么

对应 `rmpp` 中：

- `FSi6X`
- `VT13`
- `Mavlink`

这一层最重要的共识是：

- 应只做协议解析、断联检测、超时复位、死区归一化、稳定字段缓存
- 不应在这里提前解释“是否开火模式 / 是否自动瞄准模式 / 是否导航”

### 11.2 意图来源层

这一层回答：

- 这份输入代表“谁的意图”？

从 `rmpp` 的综合结果看，来源不应该只按“设备名”建模，而应该按“意图归属”建模：

- `human direct`
  - 遥控器摇杆 / 鼠标 / 键盘 / 上位端直接操作
- `host assist`
  - 外部主机给出的辅助事实或高层建议
- `automation`
  - 由板载状态机 / 导航 / 自瞄策略推导出的非人工意图

这里是当前仓库此前最容易混淆的地方：

- `master_machine` 不必然等于 `automation`
- `vision` 不必然等于 `InputRole` 的直接人类输入
- `host assist` 可以是事实源，也可以是人类直接输入桥

### 11.3 控制域层

这一层回答：

- 这份输入在控制链里到底影响哪一个域？

从 `rmpp` 的研究看，至少应拆成：

- `mode/safety`
- `motion`
- `aim mode`
- `aim speed`
- `aim angle`
- `fire enable`
- `fire modulation`

注意这里已经比上一版调研更细：

- `aim` 至少要拆成 `mode + speed + angle`
- `fire` 至少要拆成 `enable + modulation`

因为 `rmpp` 明确证明：

- 云台角度模式与速度模式不能用同一套仲裁律
- 射击“是否开火”和“如何调弹频”也不是同一层问题

---

## 12. 对当前仓库的进一步细化方案

基于三路研究，当前仓库的遥控链路优化方案应再细化为下面这套结构。

## 12.1 目标结构

未来输入链路建议固定为：

```text
Device Adapters
-> Source Drafts
-> Domain Arbitrator (InputRole)
-> Stable Operator Snapshot
-> DecisionRole
-> Command Topics
```

### Layer A: Device Adapters

现有或未来模块：

- `RemoteControl`
- `MasterMachine`
- future `KeyboardMouse`
- future `VisionAssist`
- future `NavigationAssist`

职责固定为：

- 协议解析
- 在线性检测
- 超时安全回退
- 私有 typed topic 发布

### Layer B: Source Drafts

这一层建议不要直接并入模块，而是作为 `InputRole` 内部的中间语义。

建议至少拆成：

- `HumanInputDraft`
- `HostAssistDraft`
- `AutomationInputDraft`

其中：

- `HumanInputDraft`
  - 表达遥控器 / 键鼠 / 上位端直接人工输入
- `HostAssistDraft`
  - 表达外部主机给出的“建议性”或“批准式”输入
- `AutomationInputDraft`
  - 表达板载自动状态机或算法输出

这样可以避免把 `master_machine`、`vision`、`automation` 全部糊成一个 `host`。

### Layer C: Domain Arbitrator

这是 `InputRole` 的真正升级目标。

它不再负责“整份快照切换”，而负责：

- 为每个 domain 选择来源
- 记录仲裁原因
- 保留来源解释
- 输出稳定的 `OperatorInputSnapshot`

### Layer D: Stable Operator Snapshot

这一层仍然是 `DecisionRole` 唯一消费的输入，但它不能只保留裸值。

它至少应表达：

- 最终值
- 每个 domain 的 active source
- 仲裁原因
- 来源在线性摘要
- 是否来自 assist / automation

---

## 12.2 建议新增的数据结构

## A. 输入来源枚举

建议新增：

- `InputSourceKind`
  - `kNone`
  - `kRemoteStick`
  - `kRemoteMouseKeyboard`
  - `kHostDirect`
  - `kHostAssist`
  - `kAutomation`

相比上一版，关键变化是把 `host` 拆成：

- `HostDirect`
- `HostAssist`

原因是：

- 直接操作者输入
- 外部辅助建议

在仲裁上不应该同权。

## B. 控制域枚举

建议新增：

- `InputControlDomain`
  - `kModeSafety`
  - `kMotion`
  - `kAimMode`
  - `kAimSpeed`
  - `kAimAngle`
  - `kFireEnable`
  - `kFireModulation`

## C. 仲裁原因枚举

建议新增：

- `InputArbitrationReason`
  - `kNominal`
  - `kRemoteOffline`
  - `kRemoteSafeSwitch`
  - `kHostOverride`
  - `kAssistAccepted`
  - `kAssistRejected`
  - `kAutomationActive`
  - `kForcedSafeByHealth`
  - `kNoValidSource`

这里比上一版增加了：

- `kAssistAccepted`
- `kAssistRejected`
- `kNoValidSource`

因为 `rmpp` 证明“辅助输入”和“自动输入”不是同一种结构位置。

---

## 12.3 OperatorInputSnapshot 应如何升级

当前 `OperatorInputSnapshot` 最大的问题不是字段少，而是“无法解释来源”。

建议下一版至少增加：

- `active_control_source`
- `active_motion_source`
- `active_aim_mode_source`
- `active_aim_value_source`
- `active_fire_source`
- `motion_arbitration_reason`
- `aim_arbitration_reason`
- `fire_arbitration_reason`
- `source_online_mask`
- `source_accept_mask`

同时建议把当前快照内部按域分块：

- `mode/safety`
- `motion`
- `aim`
- `fire`

这样 `DecisionRole` 才能根据结构清楚区分：

- `mode` 该不该允许某来源覆盖
- `motion` 是不是可求和
- `aim` 当前是 speed 还是 angle
- `fire` 是否只接受 enable，不接受 modulation

---

## 12.4 InputRole 应如何升级

当前 `InputRole` 的问题是：

```text
if (host override) {
  snapshot = host
} else {
  snapshot = remote
}
```

这会把所有控制域强制绑在一起。

升级后建议分 4 步处理：

### Step 1: Build Drafts

把每个来源先翻译成 draft：

- `BuildHumanDraft(remote_control_state, future_keyboard_mouse_state)`
- `BuildHostDraft(master_machine_state)`
- `BuildAutomationDraft(future_assist_topics)`

### Step 2: Safety Gate

先做前置硬门：

- health 强制 safe
- remote 物理 safe
- 无有效人工来源

### Step 3: Domain Arbitration

按域做不同规则：

- `mode/safety`
  - 强优先级选择
- `motion`
  - 第一版保守使用“单来源选择”，暂不求和
- `aim mode`
  - 强优先级选择
- `aim angle / aim speed`
  - 跟随 `aim mode` 的仲裁结果
- `fire enable`
  - 人工批准优先
- `fire modulation`
  - 仅在来源允许时采纳

### Step 4: Emit Explained Snapshot

写出最终快照，并保留：

- 最终值
- 来源
- 原因
- 哪些来源在线

---

## 12.5 当前仓库第一版实施建议

虽然 `rmpp` 允许 motion 求和，但我不建议当前仓库第一版就照搬。

当前仓库第一版更稳妥的策略应是：

- `mode/safety`：优先级选择
- `motion`：优先级选择
- `aim mode`：优先级选择
- `aim value`：跟随当前模式来源
- `fire enable`：人工批准优先
- `fire modulation`：只在来源明确时生效

原因：

- 当前仓库还没有 `source metadata`
- 还没有 keyboard/mouse 独立模块
- 还没有 automation lane 的实际 producer

若过早引入求和，解释性会先崩。

因此建议分两阶段：

### Phase B-1

- 先做“可解释的单域优先级仲裁”
- 不做跨来源求和
- 把 whole-source switch 拆掉

### Phase B-2

- 等 future keyboard/mouse、vision assist、automation lane 真实接入后
- 再把部分 domain 升级为“可求和贡献”

---

## 12.6 对整条遥控链路的最终建议

综合三路研究后，当前仓库的遥控链路建议固定为：

```text
RemoteControl / MasterMachine / future Input Modules
-> typed private state topics
-> InputRole drafts
-> domain-aware arbitration
-> OperatorInputSnapshot
-> DecisionRole
-> motion/aim/fire command topics
-> Role fan-out
```

其中必须长期坚持的禁令是：

- 禁止在设备模块里直接解释机器人业务模式
- 禁止在 `InputRole` 里长出执行域状态机
- 禁止把 assist / automation 伪装成新的 `remote`
- 禁止重新造一个 `rmpp::Robot` 式中央总控对象

---

## 13. Synthesizer 最终判断

如果只用一句话概括这轮研究的最终结论，就是：

**当前仓库的遥控链路升级，不应从“来源切换”思考，而应从“控制域仲裁”思考。**

进一步说：

- A 研究告诉我们：设备层只应产出稳定输入快照。
- B 研究告诉我们：真正的架构价值在按控制域分别仲裁。
- C 研究告诉我们：automation / assist 必须是结构上的独立来源，而不是 host/remote 的变体。

因此，下一轮实现前最应该冻结的，不是某个具体 `if/else`，而是：

1. 输入来源字典
2. 控制域字典
3. 仲裁原因字典
4. `OperatorInputSnapshot` 的解释能力

只有这 4 件事先冻结，后续 `InputRole` 才不会再次滑回 whole-source switch。

---

## 14. 范围纠偏：当前目标应收缩为“多遥控器接入”

经过这轮并行研究后，需要对目标做一次明确收缩：

- **当前目标不是重构整条 `遥控 -> 决策 -> 执行` 控制链**
- **当前目标是只解决“多遥控器如何安全接入现有人工输入入口”**

也就是说，当前真正需要回答的是：

```text
多个遥控器
-> 如何合并为一份稳定人工输入
-> 再继续进入现有 InputRole / DecisionRole 主链
```

而不是：

```text
多个输入源
-> 整个控制架构重构
```

这是两件不同规模的事。

---

## 15. 多遥控器接入方案（第一版）

## 15.1 目标边界

第一版只处理：

- 多个“人工遥控设备”同时接入
- 主备切换 / 优先级选择
- 保持现有 `OperatorInputSnapshot` 下游契约基本不变

第一版不处理：

- automation lane
- host assist lane
- vision / navigation 辅助输入重构
- `DecisionRole` / `MotionCommand` / `AimCommand` / `FireCommand` 结构升级

## 15.2 推荐落点

综合三路研究后的明确推荐是：

### 推荐方案：在 `InputRole` 内订阅多个遥控器并合并

原因：

1. **最符合当前边界**

`InputRole` 本来就负责汇聚输入并生成统一输入视图。

2. **最小改动**

下游仍只消费同一个 `OperatorInputSnapshot`，不需要牵动：

- `DecisionRole`
- `ChassisRole`
- `GimbalRole`
- `ShootRole`

3. **不污染模块边界**

`RemoteControl` 继续保持“单协议适配器”角色，不膨胀成输入策略器。

## 15.3 第一版建议实现形态

建议第一版使用：

- `remote_control_primary_`
- `remote_control_secondary_`

由 `InputRole` 持有两套 `RemoteControl` 实例与两套 subscriber。

`InputRole::OnMonitor()` 增加：

1. 拉取两套 `RemoteControlState`
2. 计算每个来源的“是否可用”
3. 选出当前激活来源
4. 仅将激活来源映射为最终 `OperatorInputSnapshot`

这样现有人工输入主链保持为：

```text
RemoteControl A / B
-> InputRole
-> OperatorInputSnapshot
-> DecisionRole
```

## 15.4 第一版合并策略

第一版不建议：

- 布尔 OR
- 模拟量求和
- 按字段级混合来源

第一版应固定为：

### 单设备独占

规则：

- 只允许一个遥控器成为当前激活源
- 激活源离线、超时或进入显式 safe 时，切换到下一候选源
- 若所有源都不可用，回到 safe

原因：

- 安全
- 可解释
- 最小改动
- 最适合当前仍沿 `basic_framework` 思路编写的控制链

## 15.5 第一版应保留的最小解释字段

虽然第一版不建议扩 `OperatorInputSnapshot` 公共语义，但至少在 `InputRole` 内部或调试结构里应保留：

- `active_source_id`
- `selection_reason`
- `source_online`
- `source_last_update_ms`
- `source_requested_safe`
- `active_source_switch_state`

这样后续才能解释：

- 当前控制权在哪台遥控器
- 为什么切走
- 为什么进入 safe

## 15.6 对 `RemoteControl` 的最小必要改动

当前 `RemoteControl` 还不能无改动支持多实例并存，原因是：

- Topic 名固定为 `remote_control_state`

所以第一版只需要做一个很小的模块级调整：

- 允许 `RemoteControl` 在构造时注入实例级 Topic 名

注意：

- 这不是把多遥控器仲裁下沉到模块
- 这只是让多个设备实例能共存

仲裁仍然留在 `InputRole`

## 15.7 明确不建议的方案

### 不建议 A：在 `RemoteControl` 内部做多遥控器合并

因为这会让模块从“协议适配器”膨胀成“输入策略器”。

### 不建议 B：第一版就引入 `RemoteMux/RemoteAggregator` 模块

因为虽然它比 A 干净，但会：

- 新增一个长期模块
- 扩大装配面
- 把当前问题抽象过度

在“只做多遥控器接入”的目标下，第一版性价比不高。
