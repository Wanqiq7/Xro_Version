# Pitch 在线 / 使能 / 反馈 / ready 板上验证收口

## 1. 任务边界

本说明只收口 Pitch 真实链路的板上验证前置条件、可执行步骤、断点口径与当前阻塞点。

- 不宣称板上已经通过。
- 不复写业务代码结论。
- 只基于本轮实际执行命令与仓库现状给出判断。

## 2. 本轮实际证据

### 2.1 实际执行命令

```powershell
cmake --preset Debug
cmake --build --preset Debug
git status --short
Get-Command cmake,ninja,arm-none-eabi-gcc,arm-none-eabi-g++,arm-none-eabi-gdb,JLinkGDBServerCL,JLink.exe,openocd
Get-ChildItem 'build/Debug' -File | Where-Object { $_.Extension -in '.elf','.bin','.hex','.map' } | Select-Object FullName,Extension,Length,LastWriteTime | Format-List
Get-PnpDevice -PresentOnly | Where-Object { $_.FriendlyName -match 'J-Link|ST-Link|CMSIS-DAP|DAPLink' -or $_.InstanceId -match 'VID_1366|VID_0483' } | Select-Object Status,Class,FriendlyName,InstanceId | Format-List
powershell -ExecutionPolicy Bypass -File .\.Doc\start_pitch_jlink_gdbserver.ps1
```

### 2.2 命令结论

1. `cmake --preset Debug`
   - 失败。
   - 关键报错：`ninja: error: failed recompaction: Permission denied`
   - 含义：当前 Debug 重新生成 `build.ninja` 时存在工作目录权限/占用问题，配置阶段不能稳定重入。

2. `cmake --build --preset Debug`
   - 失败。
   - 关键报错文件：`App/Runtime/PitchAxisBridge_compile_test.cpp`
   - 关键报错内容：
     - `PitchAxisBridgeInput` 不存在 `measured_rate`
     - `PitchAxisBridgeState` 不存在 `measured_rate`
     - 若干 `static_assert` 失效
   - 含义：当前仓库**无法得到本轮新鲜可下载固件**，因此不能宣称完成本轮板上验证。

3. 调试工具探测
   - `cmake / ninja / arm-none-eabi-gcc / arm-none-eabi-g++ / arm-none-eabi-gdb` 可用。
   - `Get-Command` 未命中 `JLinkGDBServerCL`、`JLink.exe`、`openocd`。
   - 但标准安装目录中实际存在：
     - `C:\Program Files\SEGGER\JLink\JLink.exe`
     - `C:\Program Files\SEGGER\JLink\JLinkGDBServerCL.exe`
   - 含义：主机具备 SEGGER 可执行文件，但**未进 PATH**，且仓库内没有现成板级启动脚本。

4. 探针在线探测
   - `Get-PnpDevice` 未返回 J-Link / ST-Link / CMSIS-DAP 当前在线设备。
   - 含义：本轮环境中**没有拿到“探针已连接”的新鲜证据**。

5. J-Link 启动脚本实跑
   - `.Doc/start_pitch_jlink_gdbserver.ps1` 已能正常启动 `JLinkGDBServerCL.exe`
   - 实际结果：`Connecting to J-Link failed. Connected correctly?`
   - 含义：当前阻塞点已经收敛为**探针/连线/上电状态**，而不是脚本本身不可执行。

6. 历史构建产物
   - `build/Debug/basic_framework.elf`
   - `LastWriteTime = 2026/4/22 23:21:42`
   - 含义：仓库中存在历史 ELF，但在当前构建失败前提下，它只能视为**陈旧产物**，不能作为本轮下载与验证依据。

## 3. 当前结论

当前状态：**BLOCKED**

原因不是单点，而是 3 个前置条件同时未闭合：

1. **新鲜固件不可得**
   - `cmake --build --preset Debug` 当前失败，阻塞下载。

2. **调试启动资产不完整**
   - 仓库内未发现项目级 `J-Link` / `OpenOCD` / `.gdb` / `flash` 脚本。
   - 本文已补一份 GDB 脚本与一份 J-Link 启动脚本，供后续直接复用。

3. **没有真机在线证据**
   - 当前未检测到在线调试探针，因此不能声称已经进入板上断点或闭环调试。

## 4. Pitch 验证口径

### 4.1 在线 (online)

口径来源：

- `Modules/DMMotor/DMMotor.hpp`
  - `DMMotor::UpdateFeedback()`
  - `DMMotor::UpdateOnlineStatus()`
- `App/Integration/PitchDMMotorBridge.cpp`
  - `PitchDMMotorBridge::SyncFeedbackState()`
- `App/Roles/GimbalRole.cpp`
  - `GimbalRole::UpdateGimbalState()`

成功判据：

- `DMMotorFeedback.initialized == true`
- `DMMotorFeedback.online == true`
- `PitchAxisBridgeState.feedback_online == true`
- `gimbal_state_.online == true`

失败判据：

- `feedback_.initialized` 长期为 `false`
- 或 `feedback_.last_feedback_time_ms` 不更新
- 或 `UpdateOnlineStatus()` 判定超时后 `feedback_.online == false`

### 4.2 使能 (enable)

口径来源：

- `App/Roles/GimbalRole.cpp`
  - `GimbalRole::IsRobotReady()`
  - `GimbalRole::ApplyPitchMotorControl()`
- `App/Integration/PitchDMMotorBridge.cpp`
  - `PitchDMMotorBridge::Update()`
- `Modules/DMMotor/DMMotor.hpp`
  - `DMMotor::Enable()`
  - `DMMotor::Stop()`

成功判据：

- `robot_ready == true`
- `PitchAxisControlGate.allow_enable == true`
- `PitchDMMotorBridge::Update()` 不走 `kDisabled`
- 无反馈前会执行 `motor_.Enable()`
- `command_.control == DMMotorControlCommand::kEnable` 至少出现一次

失败判据：

- `robot_mode_.is_enabled == false`
- 或 `robot_mode_.is_emergency_stop == true`
- 或 `robot_mode_.robot_mode == RobotModeType::kSafe`
- 导致 `allow_enable == false`，桥接状态停留在 `kDisabled`

### 4.3 反馈 (feedback)

口径来源：

- `DMMotor::UpdateFeedback()`
- `PitchDMMotorBridge::SyncFeedbackState()`
- `GimbalRole::UpdateGimbalState()`

成功判据：

- `feedback_.angle / total_angle / velocity / torque` 有实际更新
- `feedback_.last_feedback_time_ms` 单调更新
- `PitchAxisBridgeState.feedback_initialized == true`
- `PitchAxisBridgeState.measured_position` 与 `relative_pitch_deg_` 有合理变化

失败判据：

- CAN 回包断点不命中
- `feedback_` 始终默认值
- `feedback_initialized` 始终为 `false`

### 4.4 ready

最终口径固定在 `GimbalRole::UpdateGimbalState()`：

```cpp
gimbal_state_.ready = robot_ready &&
                      yaw_feedback.online &&
                      pitch_bridge_state.feedback_online &&
                      pitch_bridge_state.feedback_initialized &&
                      yaw_state.enabled &&
                      pitch_tracking &&
                      !yaw_state.safe_stopped &&
                      has_gyro_sample_;
```

Pitch 相关成功判据：

- `pitch_bridge_state.feedback_online == true`
- `pitch_bridge_state.feedback_initialized == true`
- `pitch_bridge_state.status == PitchBridgeStatus::kTracking`

注意：

- `ready` 不是纯 Pitch 单点口径，它还依赖 `yaw` 在线、`yaw_state.enabled`、`BMI088` 有陀螺仪样本等。
- 因此 Pitch 已经在线/有反馈，也可能因为 yaw 或 IMU 条件不满足而 `ready == false`。

## 5. 推荐最小断点链

结合 AGENTS.md 现有板级调试经验，推荐断点顺序如下：

1. `StartDefaultTask`
   - 目标：确认 FreeRTOS 默认任务确实启动

2. `app_main`
   - 目标：确认板级入口已进入

3. `App::AppRuntime::AppRuntime`
   - 目标：确认 Pitch 执行链对象已经构造
   - 重点观察：
     - `pitch_motor_`
     - `pitch_motor_bridge_`

4. `App::PitchDMMotorBridge::Update`
   - 目标：确认 GimbalRole 已经把 Pitch 指令喂入桥接
   - 重点观察：
     - `input.target_position`
     - `input.measured_position`
     - `state_.feedback_online`
     - `state_.feedback_initialized`

5. `DMMotor::Enable`
   - 目标：确认 Pitch 进入使能路径
   - 重点观察：
     - `command_.control`

6. `DMMotor::UpdateFeedback`
   - 目标：确认 CAN 回包进入反馈解析
   - 重点观察：
     - `feedback_.initialized`
     - `feedback_.online`
     - `feedback_.angle`
     - `feedback_.velocity`
     - `feedback_.last_feedback_time_ms`

7. `DMMotor::OnMonitor`
   - 目标：确认周期任务持续发送控制命令
   - 重点观察：
     - `has_mit_command_`

8. `App::GimbalRole::UpdateGimbalState`
   - 目标：确认最终 `online / ready` 汇总口径
   - 重点观察：
     - `pitch_bridge_state.status`
     - `pitch_bridge_state.feedback_online`
     - `pitch_bridge_state.feedback_initialized`
     - `gimbal_state_.online`
     - `gimbal_state_.ready`

## 6. 最小可执行板上步骤

### Step 1: 先修通构建

必须先让以下命令拿到 **exit 0**：

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

当前已知直接阻塞：

- `App/Runtime/PitchAxisBridge_compile_test.cpp` 与 `App/Integration/PitchAxisBridge.hpp` 字段定义不一致。

### Step 2: 启动 J-Link GDB Server

可直接运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\.Doc\start_pitch_jlink_gdbserver.ps1
```

若脚本失败，至少要手工确保：

- 设备型号：`STM32F407IGHx`
- 接口：`SWD`
- GDB 端口：`2331`

### Step 3: 用 GDB 挂载并打断点

```powershell
arm-none-eabi-gdb -x .\.Doc\pitch_online_enable_feedback_ready.gdb
```

### Step 4: 按口径逐项验收

建议按下面顺序记录：

1. 是否命中 `StartDefaultTask / app_main / AppRuntime::AppRuntime`
2. `PitchDMMotorBridge::Update` 是否被周期命中
3. `DMMotor::Enable` 是否出现
4. `DMMotor::UpdateFeedback` 是否出现
5. `feedback_.initialized / online` 是否为真
6. `pitch_bridge_state.status` 是否进入 `kTracking`
7. `gimbal_state_.online / ready` 最终值是什么
8. 若 `ready == false`，是哪一个布尔前提没满足

## 7. 板上记录模板

建议把每次真机结果按下表回填：

| 项目 | 观察点 | 成功标准 | 本轮结果 |
| --- | --- | --- | --- |
| 启动链 | `StartDefaultTask -> app_main -> AppRuntime::AppRuntime` | 三者均命中 | 未执行 |
| Pitch 在线 | `feedback_.online` / `feedback_.initialized` | 均为 `true` | 未执行 |
| Pitch 使能 | `DMMotor::Enable` / `command_.control` | 命中且为 `kEnable` | 未执行 |
| Pitch 反馈 | `DMMotor::UpdateFeedback` | 周期命中且角度/速度更新 | 未执行 |
| Pitch ready 口径 | `pitch_bridge_state.status` | `kTracking` | 未执行 |
| Gimbal ready | `gimbal_state_.ready` | 结合 yaw/imu 条件成立 | 未执行 |

## 8. 当前阻塞点清单

### 阻塞点 1: 当前 Debug 构建失败

位置：

- `App/Runtime/PitchAxisBridge_compile_test.cpp`
- `App/Integration/PitchAxisBridge.hpp`

现象：

- 编译测试仍访问 `measured_rate / commanded_velocity`，而当前桥接结构体已删除这些字段。

影响：

- 无法产出本轮新鲜 ELF，板上下载与验证不成立。

### 阻塞点 2: 仓库原生调试脚本缺失

现象：

- 未发现项目级 `.gdb`、`flash`、`OpenOCD`、`J-Link` 启动脚本。

影响：

- 即使构建修好，后续板上 bring-up 仍缺少统一入口。

### 阻塞点 3: 没有探针在线证据

现象：

- 当前 `Get-PnpDevice` 没拿到在线 J-Link / ST-Link / CMSIS-DAP。

影响：

- 不能声称已经进入真机断点。

## 9. 给主控的结论

当前**不能**进入可信的板上闭环调试。

必须先完成以下两件事，才值得继续真机：

1. 修通 `cmake --build --preset Debug`
2. 确认调试探针真实在线，并按本文脚本进入断点链

一旦这两个条件满足，Pitch 的板上验证路径已经具备最小可执行性，可以按本文断点表进入闭环调试。
