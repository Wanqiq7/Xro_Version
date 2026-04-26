# basic_framework -> XRobot 未来迁移路线图（2026-04-22 重排版）

## 1. 文档目的

本文档用于替换旧的 checklist/阶段卡片式路线图，只保留当前仍然有效的 4 类信息：

- 当前真实代码基线
- 已冻结的架构边界与迁移禁令
- 后续仍值得推进的主线
- 暂缓项与 backlog

本文档不是历史记录回放，也不是单次实现任务单。凡是已经被当前代码基线覆盖、或已经证明会误导后续实现的旧阶段内容，均视为失效并删除。

---

## 2. 当前冻结基线

### 2.1 唯一运行链

后续所有迁移都必须继续服从这条唯一运行链：

```text
FreeRTOS defaultTask
-> app_main()
-> XRobotMain(peripherals)
-> ApplicationManager::MonitorAll()
```

明确禁止：

- 再引入第二套 `RobotInit/RobotTask/OSTaskInit`
- 再引入第二个无限主循环
- 再引入第二个调度中心或 donor `message_center`

### 2.2 当前 App 基线

当前 `App/` 已固定 6 个角色：

- `InputRole`
- `DecisionRole`
- `ChassisRole`
- `GimbalRole`
- `ShootRole`
- `TelemetryRole`

当前 `App/Topics` 已固定 8 个公共 Topic：

- `robot_mode`
- `motion_command`
- `aim_command`
- `fire_command`
- `chassis_state`
- `gimbal_state`
- `shoot_state`
- `system_health`

### 2.3 当前已落地的真实链路

以下内容已经从“规划”进入“当前基线”，后续不能再按未完成项重复规划：

- `DT7 -> InputRole` 第一轮真实输入链已完成
- `VT13` 已独立成模块并完成板级启用，当前经 `USART2 -> uart_vt13` 接入
- `BMI088 -> GimbalRole / ChassisRole` 第一轮真实观测链已完成
- `DJIMotor` 通用能力已归一化到 `Modules/`，并由 `GimbalRole / ChassisRole / ShootRole` 回绑消费
- `Referee` 已完成两轮收口：
  - `RawStateCache -> DeriveStableState`
  - `RefereeConstraintView`
- `CANBridge` 已完成第一轮最小真实协议闭环：
  - heartbeat frame
  - 在线口径收口
  - CRC-8 校验升级
  - `CANBridgeHealthView`
- `MasterMachine` 已完成第一轮协同视图收口：
  - `MasterMachineCoordinationView`
  - 当前明确冻结为 `manual override only`
- `ChassisRole` 已吸收 donor 的 `stop / follow gimbal / spin` 语义，并已引入 `ChassisStateEstimator`
- `GimbalRole` 已补上 `pitch_motor` 真实执行链
- `ShootRole` 已补上 first-pass 的热量摘要、弹速映射、保守卡弹恢复，以及 single/burst dead time 语义

### 2.4 当前 `super_cap` 基线

`super_cap` 当前已经完成第一轮“只可观测、不入控制链”的最小接入，现阶段把它视为**已实现但暂缓继续扩展**：

- 协议源来自 `D:\RoboMaster\HeroCode\Code\Chassis\modules\super_cap`
- 当前作为简单通信模块存在于 `Modules/SuperCap`
- 当前只做状态观测，不把 `cap_energy / power_limit / chassis_power` 接入 `DecisionRole / ChassisRole`
- 当前只把 `supercap_online` 纳入 `system_health`
- 模块侧已改为通过配置注入 `can_name`，不在模块内部硬编码板级 CAN 选择

后续若没有新的明确需求，`super_cap` 不再占用当前主线优先级。

---

## 3. 当前冻结约束

### 3.1 分层边界

继续严格遵守：

```text
User -> App -> Modules -> LibXR
```

明确要求：

- `User/` 只负责板级入口、硬件别名注入和把控制权交给 `App`
- `App/` 只负责角色化业务编排、模式管理和跨角色协调
- `Modules/` 只负责协议适配、执行器/传感器封装、桥接器和可复用能力
- 禁止把整车级模式判断、跨角色控制策略重新下沉到 `Modules/`

### 3.2 donor 迁移禁令

继续明确禁止：

- 原样搬运 donor `.c/.h` 目录
- 迁移 donor `message_center`
- 迁移 donor `daemon`
- 把 donor 协议结构体直接当业务对象使用
- 在 `Role` 里直接处理长期存在的底层发帧细节
- 再次引入“伪装成模块的 APP 控制器”

### 3.3 输入链约束

当前人工输入优先级继续冻结为：

```text
DT7 primary > DT7 secondary > VT13 > none
-> 再允许 master_machine 做最终覆盖
```

当前继续明确不进入主链的高级语义：

- `VT13.mouse`
- `VT13.key`
- `target_wz`
- `track_target`

当前继续明确不做：

- 多来源字段级混控
- 多来源模拟量求和
- 新建 `RemoteMux` 一类总线式输入模块

### 3.4 bring-up 约束

- 若当前没有出现运行错误、启动断言、数据接收异常或回调缺失，则**不要主动处理** `VT13` 所接串口设置细节
- 对 `VT13` 串口设置继续采取“**无运行错误则忽略**”策略
- `xrobot-org/` 命名空间下的官方模块默认按 vendor / 第三方代码处理，非通用问题不得直接改源码

---

## 4. 后续主线重排

`super_cap` 第一轮接入之后，当前更有价值的后续迁移不再是“继续补模块目录”，而是围绕**执行链上板收口、外部约束协同、桥接扩展、最后清理**这 4 条主线推进。

### 主线 A：执行链板上收口与参数回归

目标：

- 把当前 `ChassisRole / GimbalRole / ShootRole` 已吸收的 donor 语义，从“代码可解释”推进到“板上行为稳定”

当前重点：

- `ChassisStateEstimator` 的板上回放与参数整定
- `follow gimbal / spin / stop` 模式切换的真实反馈验证
- `pitch` 真实执行链的上板闭环确认
- `ShootRole` 的热量、弹速、卡弹恢复在真实反馈下的行为确认

明确不做：

- 不在这一主线里扩 `App/Topics`
- 不顺手把 `super_cap` 数值塞进控制链

### 主线 B：`referee / master_machine / decision` 协同深化

目标：

- 继续把外部约束和外部协同压缩成稳定视图，避免把协议细节重新散写进 `DecisionRole`

当前重点：

- 继续完善 `RefereeConstraintView` 的稳定语义
- 明确 `master_machine` 若未来从 `manual override only` 升级，应走哪条 ingress 路径
- 保持 `DecisionRole` 只消费稳定领域语义，不直接理解协议帧

明确不做：

- 不直接把 `master_machine` 升级成新的上位总控中心
- 不在 `DecisionRole` 中长出第二套协议解释逻辑

### 主线 C：`CANBridge` 按真实跨板需求扩业务载荷

目标：

- 当前 `CANBridge` 已具备最小 heartbeat 闭环，后续仅在出现真实跨板业务需求时再扩展业务载荷

当前重点：

- 保持 bridge 协议白名单和健康摘要的边界稳定
- 若后续要加业务 frame，优先先做模块内摘要和协议边界设计，再考虑是否进入公共 Topic

明确不做：

- 不把 bridge 计数器、序号、时间戳直接抬进 `system_health`
- 不让 bridge 演变成新的业务决策中心

### 主线 D：bring-up 清理与再生成回归

目标：

- 把当前“代码已通、板级可跑、文档可交接”进一步推进到“再生成后不易回退”

当前重点：

- `.ioc` 与当前真实接线事实继续对齐
- `USART2 / USART3` 的 RX-only workaround 后续择机收口
- 文档只保留仍有效的主线、约束和 backlog
- 板级回归脚本、断点脚本、联调清单继续沉淀

明确不做：

- 不在清理阶段引入新的架构实验
- 不在清理阶段混入新的 donor 功能迁移

---

## 5. 当前 backlog

以下事项保留在 backlog，不进入当前主线：

### 5.1 `super_cap` 第二轮扩展

只有在用户重新明确授权后，才允许继续推进：

- `CAN1` 板上抓包与实测
- 更细的 `supercap_degraded` / 故障摘要
- 把 `cap_energy / chassis_power_limit / chassis_power` 接入控制链

在那之前，`super_cap` 保持：

- 模块可观测
- 健康可汇总
- 不参与控制

### 5.2 非 DJI 通用电机

仅在真实硬件需求出现时立项，不提前为 donor 完整性而迁移。

### 5.3 `VT13` 高级键鼠语义

仅在明确需要进入主控链时，再单独设计其 ingress 语义与安全边界。

---

## 6. 文档使用规则

后续若继续更新本路线图，应遵守以下规则：

- 只写“当前真实仍成立”的事实
- 已进入代码基线的内容，从未来规划移入当前基线
- 已被证伪或已过期的 checklist/阶段卡片，直接删除，不做历史保留
- 未来任务优先写“主线、边界、验收口径”，不要再回到 donor 目录对目录平移

---

## 7. 当前结论

截至当前阶段，路线图已经从“补骨架”切换为“收口真实执行链 + 稳定外部约束协同 + 保守维护 backlog”。

当前优先级顺序冻结为：

1. 执行链板上收口与参数回归
2. `referee / master_machine / decision` 协同深化
3. `CANBridge` 按真实需求扩业务载荷
4. bring-up 清理与再生成回归
5. `super_cap` 第二轮扩展继续留在 backlog
