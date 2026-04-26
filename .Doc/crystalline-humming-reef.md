# App/ 层精简重构方案

## 背景

App/ 当前 58 个源文件、4686 行、7 个子目录，对比 basic_framework 的 application/（10 文件、1469 行、4 目录）明显臃肿。根因：RobotController God Class、Role 空壳层、过度工程的健康系统、Bridge 虚函数抽象。

用户决策：
1. 类级重构 + 目录重组（两步都做）
2. 健康/降级系统大幅裁剪（~700 行 → ~50 行）
3. 去掉 Bridge 虚函数抽象（内联到 GimbalController）

---

## 目标目录结构

```
App/
  Assembly/           # 不变（2 文件）
  Robot/              # 精简（5 文件：AppRuntime, RuntimeContext, ControllerBase）
  Cmd/                # 新建（5 文件：输入 + 决策 + 急停）
  Chassis/            # 新建（底盘控制器 + helpers）
  Gimbal/             # 新建（云台控制器，Bridge 逻辑内联）
  Shoot/              # 新建（发射控制器 + helpers）
  Topics/             # 精简（7→6 文件：删 SystemHealth）
  Config/             # 精简（6→4 文件：删 HealthPolicy/DegradedMode）
  Dev/CompileTests/   # 更新路径，删健康相关测试
```

预估结果：58 文件 → ~30 文件（48% 减少），4686 行 → ~2500 行（47% 减少）

---

## 任务职责与 Fan-out 执行边界

### 当前真实耦合图（作为拆分基线）

1. 当前运行链不是 `RobotController::OnMonitor()` 单点驱动，而是 `InputRole -> DecisionRole -> GimbalRole -> ChassisRole -> ShootRole -> TelemetryRole` 六段包装层串行触发。
2. `RobotController` 当前同时承担三类职责：
   - 输入融合：订阅 `DT7 / VT13 / MasterMachine` 并写 `RuntimeContext::OperatorInput()`
   - 决策生成：从 `OperatorInputSnapshot + RefereeState + SystemHealth` 生成 `RobotMode / MotionCommand / AimCommand / FireCommand`
   - 健康闭环：从 `RobotMode + 各 State + Referee/MasterMachine/CANBridge/SuperCap` 生成 `SystemHealth`
3. `GimbalController` 当前负责“观测整合 + 闭环编排 + GimbalState 发布”；`Yaw/Pitch bridge` 只是其私有执行器适配层，没有独立 Topic 契约和跨角色复用价值。
4. `App/CMakeLists.txt` 目前只收集 `Robot/Subsystems/Runtime/Roles/Topics/Dev/CompileTests` 六类目录；目录迁移到 `Cmd/Chassis/Gimbal/Shoot` 后必须同步更新 glob 和 include path。

### 最终执行决议

1. `RefereeConstraintView` 保留，不再参与健康聚合，只作为 `ChassisController / ShootController` 的本地执行器约束视图。
2. `YawAxisBridge / PitchAxisBridge / YawDJIMotorBridge / PitchDMMotorBridge` 未直接物理删除，而是降级为迁移兼容包装；真实控制语义已经内联进 `GimbalController`。
3. `DegradedModeConfig.hpp` 最终删除；“全局降级策略”不再保留为独立配置面。

### 主控先冻结的 4 个接口面

在任何 worker 开始改代码前，以下接口由主控统一解释并在最终合流时收口：

1. `OperatorInputSnapshot` 的最小契约
   - 输入层专有语义留在这里，不扩散到 Topic。
   - 为支撑“无输入即 Safe”的最小急停闭环，本轮允许新增 `has_active_source`，但不在执行器控制器里重复解释。
2. `RobotMode / MotionCommand / FireCommand` 的责任边界
   - `DecisionController` 只负责模式与命令意图，不再携带健康降级细节。
   - 底盘功率限制、发射热量/弹速门控下沉到 `ChassisController / ShootController`。
3. `AppRuntime` 的唯一执行链
   - 最终执行顺序固定为：`Input -> Decision -> Gimbal -> Chassis -> Shoot`。
   - 任何 `auto_register=true` 的切换、Role 删除、成员声明顺序调整，都由主控最终收口。
4. `CMake + CompileTests` 的迁移时机
   - 目录和测试清理只在主控最终合流时落地，避免中途把旧实现和新实现同时纳入目标。

### Worker 任务卡（可直接下发）

#### Worker-A：`gimbal-bridge-inline`

- **目标**：把 `Yaw/Pitch bridge` 内联进 `GimbalController`，删除公开桥接抽象的必要性。
- **负责内容**：
  - 收口 `GimbalController` 构造接口，使其面向 `DJIMotor& yaw_motor, DMMotor& pitch_motor`
  - 内联 `YawDJIMotorBridge` 的单轴执行器适配逻辑
  - 内联 `PitchDMMotorBridge` 的速率估计、P+D 速度命令和 MIT 输出逻辑
  - 删除或改写桥接专属 compile tests
- **不负责内容**：
  - 不改 `RuntimeContext`
  - 不改 `RobotMode` 与健康系统
  - 不改 `InputRole / DecisionRole / TelemetryRole / RobotController`
  - 不改 `AppRuntime` 最终接线
- **文件所有权**：
  - `App/Subsystems/GimbalController.hpp/.cpp`
  - `App/Subsystems/YawAxisBridge.hpp`
  - `App/Subsystems/YawDJIMotorBridge.hpp/.cpp`
  - `App/Subsystems/PitchAxisBridge.hpp`
  - `App/Subsystems/PitchDMMotorBridge.hpp/.cpp`
  - `App/Subsystems/AxisBridgeCommon.hpp`
  - `App/Dev/CompileTests/GimbalRoleAxisBridge_compile_test.cpp`
  - `App/Dev/CompileTests/YawAxisBridge_compile_test.cpp`
  - `App/Dev/CompileTests/PitchAxisBridge_compile_test.cpp`
- **交付边界**：
  - `GimbalState.online/ready` 判定口径保持不变
  - `robot_ready=false` 时 yaw/pitch 仍走 stop/disable 路径
  - 不把调度、健康、Topic 语义重新抽回 Gimbal 层

#### Worker-B：`health-contract-trim`

- **目标**：删除健康/降级系统的独立契约，为后续决策最小闭环让路。
- **负责内容**：
  - 收缩 `RobotMode`，移除 `is_degraded / degraded_mode` 等健康降级字段
  - 删除 `SystemHealth / HealthAggregator / DegradedPolicy` 相关定义与健康视图
  - 删除健康系统专属 compile tests
  - 清理 `system_health` 相关配置常量与桥接条目
- **不负责内容**：
  - 不改 `RobotController`、`DecisionController` 的最终安全逻辑
  - 不改 `RuntimeContext` 和 `AppRuntime` 的最终接线
  - 不改底盘/云台/发射控制器
- **文件所有权**：
  - `App/Topics/RobotMode.hpp`
  - `App/Topics/SystemHealth.hpp`
  - `App/Runtime/HealthAggregator.hpp`
  - `App/Runtime/DegradedPolicy.hpp`
  - `App/Subsystems/CANBridgeHealthView.hpp`
  - `App/Subsystems/MasterMachineCoordinationView.hpp`
  - `App/Subsystems/RefereeConstraintView.hpp`
  - `App/Config/HealthPolicyConfig.hpp`
  - `App/Config/AppConfig.hpp`
  - `App/Config/BridgeConfig.hpp`
  - `App/Dev/CompileTests/CANBridgeHealthView_compile_test.cpp`
  - `App/Dev/CompileTests/HealthPolicy_compile_test.cpp`
  - `App/Dev/CompileTests/MasterMachineCoordinationView_compile_test.cpp`
  - `App/Dev/CompileTests/RefereeConstraintView_compile_test.cpp`
  - `App/Dev/CompileTests/SuperCapHealthView_compile_test.cpp`
- **交付边界**：
  - 不再保留 `SystemHealth` Topic 契约
  - 不把“全局健康”语义偷渡回 `RobotMode`
  - 裁判限制作为局部执行器约束保留，不再经健康聚合折返

#### Worker-C：`input-controller-extract`

- **目标**：从 `RobotController::UpdateInput()` 中提取 `Cmd/InputController`，冻结输入层职责。
- **负责内容**：
  - 新建 `Cmd/InputController.hpp/.cpp`
  - 搬运 `DT7 / VT13 / MasterMachine` 订阅、输入源优先级选择、输入映射、上位机覆盖逻辑
  - 视需要扩展 `OperatorInputSnapshot` 以显式表达 `has_active_source`
  - 保持 `InputMappingHelpers` 只做输入语义映射，不掺入决策与执行器逻辑
- **不负责内容**：
  - 不生成 `RobotMode / MotionCommand / AimCommand / FireCommand`
  - 不改健康系统删除
  - 不删 `RobotController`、不改 `AppRuntime`
  - 不做目录/CMake 最终迁移
- **文件所有权**：
  - `App/Cmd/OperatorInputSnapshot.hpp`
  - `App/Cmd/InputMappingHelpers.hpp`
  - `App/Cmd/InputController.hpp/.cpp`
- **交付边界**：
  - 无输入时仍回到安全快照
  - 主/副控/VT13 优先级不变
  - `MasterMachine override` 继续留在输入层，不上浮为模式决策

### 主控集成任务（由 synthesizer 完成）

以下改动不下发给 worker，由主控在收齐结果后统一合并并完成最终验证：

1. `DecisionController + EmergencyHandler`
   - 从 `RobotController::ComputeCommands()` 提取最小决策闭环
   - 保留“无活动输入或显式 safe 请求 -> Safe 模式 + 零命令”的最小安全语义
2. `AppRuntime` 收口
   - 删除 `RobotController` 和全部 `Roles/*`
   - 切换为 `InputController / DecisionController / GimbalController / ChassisController / ShootController` 自注册链
   - 统一成员声明顺序，确保 `Input -> Decision -> Gimbal -> Chassis -> Shoot`
3. `RuntimeContext` 收口
   - 删除 `SystemHealthTopic()`
   - 保留 `RobotMode/ChassisState/GimbalState/ShootState` Topics 和共享命令状态
4. `目录/CMake/CompileTests` 最终迁移
   - 移动 `Runtime/*Helpers` 与 `Subsystems/*Controller` 到 `Cmd/Chassis/Gimbal/Shoot`
   - 更新 `App/CMakeLists.txt`
   - 删除 `Roles/*`、`RobotController*`、剩余旧架构 compile tests
   - 处理 `SubsystemControllers_compile_test.cpp`、`Phase2BRobotWrappers_compile_test.cpp`、`RobotController*_compile_test.cpp`

### 并行与串行约束

- **可并行**：
  - Worker-A `gimbal-bridge-inline`
  - Worker-B `health-contract-trim`
  - Worker-C `input-controller-extract`
- **半并行**：
  - 主控可在 Worker-B 契约稳定后编写 `DecisionController`
- **必须串行收口**：
  - `AppRuntime` 最终接线
  - `auto_register` 开关切换
  - `Roles/*`、`RobotController*` 删除
  - `App/CMakeLists.txt` 最终源文件与 include 目录更新
  - compile tests 的最终删改与重命名

---

## 删除清单（24 个文件）

### 整个 Roles/ 目录（12 文件）
- `Roles/InputRole.hpp/.cpp` — 替换为 Cmd/InputController
- `Roles/DecisionRole.hpp/.cpp` — 替换为 Cmd/DecisionController
- `Roles/GimbalRole.hpp/.cpp` — GimbalController 自注册
- `Roles/ChassisRole.hpp/.cpp` — ChassisController 自注册
- `Roles/ShootRole.hpp/.cpp` — ShootController 自注册
- `Roles/TelemetryRole.hpp/.cpp` — 删除，无替代

### 健康系统（7 个文件/部分）
- `Runtime/HealthAggregator.hpp` (302 行)
- `Runtime/DegradedPolicy.hpp` (54 行)
- `Config/HealthPolicyConfig.hpp` (32 行)
- `Topics/SystemHealth.hpp` (78 行)
- `Subsystems/CANBridgeHealthView.hpp` (25 行)
- `Subsystems/MasterMachineCoordinationView.hpp` (40 行)
- `Subsystems/RefereeConstraintView.hpp` — 保留，作为执行器本地约束视图

### Bridge 抽象（公开接口删除，兼容包装保留）
- `Subsystems/YawAxisBridge.hpp` — 改为兼容包装，不再承载公开控制接口
- `Subsystems/YawDJIMotorBridge.hpp/.cpp` — 改为透传兼容层
- `Subsystems/PitchAxisBridge.hpp` — 改为兼容包装，不再承载公开控制接口
- `Subsystems/PitchDMMotorBridge.hpp/.cpp` — 改为透传兼容层
- `Subsystems/AxisBridgeCommon.hpp` — 保留为兼容状态摘要定义

### 被拆分的原文件
- `Robot/RobotController.hpp/.cpp` — 拆分为 Cmd/InputController + Cmd/DecisionController

---

## 移动清单（10 个文件）

| 原路径 | 新路径 |
|--------|--------|
| `Runtime/InputMappingHelpers.hpp` | `Cmd/InputMappingHelpers.hpp` |
| `Runtime/OperatorInputSnapshot.hpp` | `Cmd/OperatorInputSnapshot.hpp` |
| `Runtime/ChassisControlHelpers.hpp` | `Chassis/ChassisControlHelpers.hpp` |
| `Runtime/ChassisStateEstimator.hpp` | `Chassis/ChassisStateEstimator.hpp` |
| `Runtime/ShootControlHelpers.hpp` | `Shoot/ShootControlHelpers.hpp` |
| `Subsystems/ChassisController.hpp/.cpp` | `Chassis/ChassisController.hpp/.cpp` |
| `Subsystems/GimbalController.hpp/.cpp` | `Gimbal/GimbalController.hpp/.cpp` |
| `Subsystems/ShootController.hpp/.cpp` | `Shoot/ShootController.hpp/.cpp` |

移动后删除空的 `Runtime/` 目录；`Subsystems/` 仅保留兼容包装和局部约束视图。

---

## 新建清单（3 个文件）

### 1. `Cmd/InputController.hpp/.cpp`（~80 行）

从 `RobotController::UpdateInput()` 提取。职责：
- 持有 DT7/VT13/MasterMachine 的 Topic 订阅者
- `SelectActiveInput()` 选择最高优先级输入源
- 调用 `InputMappingHelpers` 映射到 `OperatorInputSnapshot`
- 处理 MasterMachine 覆盖
- 写入 `RuntimeContext::OperatorInput()`
- `auto_register = true`

```cpp
class InputController : public ControllerBase {
 public:
  InputController(RuntimeContext& runtime, LibXR::ApplicationManager& appmgr,
                  DT7& primary, DT7* secondary, VT13& vt13,
                  MasterMachine& master_machine);
  void OnMonitor() override;
 private:
  // 输入相关订阅者和状态（从 RobotController 搬过来）
};
```

### 2. `Cmd/DecisionController.hpp/.cpp`（~120 行）

从 `RobotController::ComputeCommands()` 提取 + 简化急停。职责：
- 读取 `RuntimeContext::OperatorInput()`
- 急停检查：遥控器离线 → `RobotMode::kSafe`，所有命令归零
- `BuildRobotMode()` — 简化版，去掉降级字段
- `BuildMotionCommand()` — 简化版，功率限制下放到 ChassisController
- `BuildAimCommand()` — 不变
- `BuildFireCommand()` — 简化版，热量限制下放到 ShootController
- 发布 RobotMode Topic，写命令到 RuntimeContext
- `auto_register = true`

急停逻辑（替代整个健康系统）：
```cpp
static bool IsControlLinkLost(const OperatorInputSnapshot& input) {
  return !input.has_active_source;
}

static bool IsEmergencyStopRequested(const OperatorInputSnapshot& input) {
  return input.request_safe_mode;
}
```

### 3. `Cmd/EmergencyHandler.hpp`（~30 行）

纯 constexpr 辅助函数，被 DecisionController 调用：
```cpp
namespace App::EmergencyHandler {
  constexpr bool ShouldForceSafe(const OperatorInputSnapshot& input) {
    return !input.has_active_source || input.request_safe_mode;
  }
  constexpr MotionCommand SafeMotionCommand() { return {}; }
  constexpr AimCommand SafeAimCommand() { return {}; }
  constexpr FireCommand SafeFireCommand() { return {}; }
}
```

---

## 关键修改

### `Robot/RuntimeContext.hpp`
- 删除 `system_health_topic_` 成员和访问器
- 删除 `#include "Topics/SystemHealth.hpp"`
- 保留：RobotMode/ChassisState/GimbalState/ShootState Topics + 共享命令状态

### `Topics/RobotMode.hpp`
- 删除 `degraded_mode`、`degraded_source_mask`、`control_action_mask` 字段
- 删除 `DegradedModeType` 枚举、`ControlAction` 常量
- 保留：`mode`(Safe/Manual)、`control_source`、`motion_mode`、`aim_mode`

### `Gimbal/GimbalController.hpp/.cpp`
- 删除 `YawAxisBridge&` / `PitchAxisBridge&` 引用
- 构造函数改为接收 `DJIMotor& yaw_motor, DMMotor& pitch_motor`
- 内联 Yaw 控制逻辑（来自 YawDJIMotorBridge.cpp:60 行）：
  - `SetExternalAngleFeedbackDeg` / `SetExternalSpeedFeedbackDegps`
  - 使能/停止/设角度参考
- 内联 Pitch 控制逻辑（来自 PitchDMMotorBridge.cpp:123 行）：
  - 速率估计回退（数值微分，kMaxRateEstimateStepS=0.05）
  - P+D 速度环（kPositionGain=8.0, kRateDampingGain=0.35）
  - MIT 命令生成

### `Chassis/ChassisController.hpp/.cpp`
- 新增 Referee Topic 订阅者
- 直接读取 `referee_state.chassis_power_limit`（离线时用保守默认值）
- 在电机控制阶段应用功率限制
- 改为 `auto_register = true`

### `Shoot/ShootController.hpp/.cpp`
- 新增 Referee Topic 订阅者
- 直接读取热量限制、弹速限制、最大射频
- 内部执行发射门控（热量/弹速）
- 改为 `auto_register = true`

### `Robot/AppRuntime.hpp/.cpp`
- 删除所有 Role 成员（6 个）
- 删除 RobotController
- 新增 InputController + DecisionController
- 所有控制器 `auto_register = true`
- 声明顺序（反向执行序）：

```cpp
// 5. 控制器（后声明先执行）
ShootController shoot_controller_;       // 第 5 执行
ChassisController chassis_controller_;   // 第 4 执行
GimbalController gimbal_controller_;     // 第 3 执行
DecisionController decision_controller_; // 第 2 执行
InputController input_controller_;       // 第 1 执行
```

- GimbalController 构造参数：`yaw_motor_` 和 `pitch_motor_` 直接传入

### `App/CMakeLists.txt`
- 更新源文件路径到新目录结构
- 删除 Roles/ 和旧路径的 .cpp 文件
- 添加 Cmd/InputController.cpp、Cmd/DecisionController.cpp

---

## 执行顺序（Fan-out -> Fan-in）

### Phase 0：主控冻结接口
1. 冻结 `OperatorInputSnapshot`、`RobotMode`、`RuntimeContext` 的最小边界
2. 明确 worker 文件所有权，避免交叉写入

### Phase 1：并行扇出
1. Worker-A 执行 `gimbal-bridge-inline`
2. Worker-B 执行 `health-contract-trim`
3. Worker-C 执行 `input-controller-extract`

### Phase 2：主控合流
1. 基于 Worker-B 的契约结果实现 `DecisionController + EmergencyHandler`
2. 基于 Worker-A 的控制器结果完成 `AppRuntime` 新装配
3. 基于 Worker-C 的输入层结果移除 `RobotController` 输入职责

### Phase 3：结构迁移与清理
1. 迁移 `Runtime/*Helpers` 与 `Subsystems/*Controller` 到新目录
2. 删除 `Roles/*`、`RobotController*`、健康系统残留与 bridge 抽象
3. 更新 `App/CMakeLists.txt`、include 路径和 compile tests
4. 更新 `App/CLAUDE.md` 文档

### Phase 4：最终验证
1. `cmake --preset Debug`
2. `cmake --build --preset Debug`
3. 检查实际执行顺序：`Input -> Decision -> Gimbal -> Chassis -> Shoot`
4. 检查遥控离线时是否进入 Safe 并输出零命令
5. 检查底盘功率限制与发射热量/弹速门控是否由执行器控制器本地消费

---

## 验证计划

1. `cmake --preset Debug && cmake --build --preset Debug` — 编译通过
2. 检查 MonitorAll() 执行顺序：Input → Decision → Gimbal → Chassis → Shoot
3. 验证急停：遥控器离线 → 所有电机零力矩
4. 验证电机控制：各子系统控制器正确驱动电机
5. 验证裁判系统数据：底盘功率限制和发射热量限制通过直接订阅工作
6. 运行编译测试（更新路径后）
