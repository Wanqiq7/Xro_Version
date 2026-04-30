[根目录](../CLAUDE.md) > **Modules**

# Modules -- 设备驱动模块集合

## 变更记录 (Changelog)

| 日期 | 操作 | 说明 |
|------|------|------|
| 2026-04-30 | 更新 | 新增 INS/RLS/SMC 三个模块；LibXR C++20 基线升级完成，RuntimeStringView 命名构造方案引入 |
| 2026-04-23 | 新建 | 首次扫描 |

---

## 模块职责

`Modules/` 目录包含所有硬件设备的驱动模块和独立算法模块。每个模块是一个独立的 header-only C++ 类（部分继承 `LibXR::Application`），通过 LibXR 的 `HardwareContainer` 按名称查找外设，通过 `Topic` 系统发布私有状态。

模块由 `modules.yaml` 声明包管理来源，由 `sources.yaml` 指定模块仓库索引。部分模块通过 xrobot-org 官方仓库以独立 git repo 形式管理（如 BlinkLED、BMI088），其余模块直接以本地目录形式存在。

---

## 模块清单

### 电机驱动

| 模块 | 文件 | 描述 | 硬件需求 |
|------|------|------|----------|
| **DJIMotor** | `DJIMotor/DJIMotor.hpp` | DJI M2006/M3508/GM6020 电机通用驱动。含分组 CAN 发帧、反馈解码、三级 PID 闭环（电流/速度/角度）| CAN |
| **DMMotor** | `DMMotor/DMMotor.hpp` | 达妙 MIT 协议电机驱动。含 MIT 五元组打包、使能/停止/清错命令 | CAN |

### 传感器

| 模块 | 文件 | 描述 | 硬件需求 |
|------|------|------|----------|
| **BMI088** | `BMI088/BMI088.hpp` | 博世 BMI088 6轴 IMU 驱动。含加速度计/陀螺仪采集、温度补偿，发布 `bmi088_gyro` 和 `bmi088_accl` Topic | SPI + GPIO(CS/INT) + PWM(加热) + ramfs + database |
| **INS** | `INS/INS.hpp` | 基于 BMI088 Topic 的 QuaternionEKF 姿态估计模块。订阅 `bmi088_gyro`/`bmi088_accl`，发布 `ins_state` 到 App 域 | 无直接硬件（依赖 BMI088 Topic） |

### 通信/输入

| 模块 | 文件 | 描述 | 硬件需求 |
|------|------|------|----------|
| **DT7** | `DT7/DT7.hpp` | DJI DT7/DBUS 遥控器驱动。解析 18 字节 DBUS 帧，发布 `DT7State` | UART |
| **VT13** | `VT13/VT13.hpp` | rmpp VT13 协议遥控器驱动。含 CRC 校验、模式解析 | UART |
| **MasterMachine** | `MasterMachine/MasterMachine.hpp` | 上位机 UART 桥接骨架。解析上位机命令帧，发布 `MasterMachineState`，超时安全回退 | UART |
| **Referee** | `Referee/Referee.hpp` | RoboMaster 裁判系统 UART 接入骨架。发布 `RefereeState`（比赛状态/功率/热量/射频），离线回退保守默认 | UART |
| **CANBridge** | `CANBridge/CANBridge.hpp` | CAN 桥接模块。收发 heartbeat 遥测帧、白名单校验、CRC8 校验，发布 `CANBridgeState` | CAN |
| **SuperCap** | `SuperCap/SuperCap.hpp` | 超级电容通信模块。donor 协议收发，发布 `SuperCapState`（功率/容量/状态） | CAN |

### 数据共享

| 模块 | 文件 | 描述 | 硬件需求 |
|------|------|------|----------|
| **SharedTopic** | `SharedTopic/SharedTopic.hpp` | 多 Topic UART 共享服务端。统一打包多个 Topic 数据通过串口传输 | UART + ramfs |
| **SharedTopicClient** | `SharedTopicClient/SharedTopicClient.hpp` | 多 Topic UART 共享客户端。订阅多个 Topic 并通过串口转发 | UART |

### 指示/反馈

| 模块 | 文件 | 描述 | 硬件需求 |
|------|------|------|----------|
| **BlinkLED** | `BlinkLED/BlinkLED.hpp` | LED 闪烁模块。使用 LibXR Timer 定时翻转 GPIO | GPIO(led) |
| **BuzzerAlarm** | `BuzzerAlarm/BuzzerAlarm.hpp` | 无源蜂鸣器报警模块 | PWM(pwm_buzzer) |

### 算法工具（无硬件依赖）

| 模块 | 文件 | 描述 | 依赖 |
|------|------|------|------|
| **RLS** | `RLS/RLS.hpp` | 2 维递推最小二乘（Recursive Least Squares）估计器。纯标量实现，零外部依赖。用于在线辨识底盘功率损耗系数 k1、k2 | 无 |
| **SMC** | `SMC/SmcController.hpp` | 滑模控制器。提供 5 种控制模式：`kExponent`（指数趋近律）、`kPower`（幂次趋近律）、`kTfsmc`（快速终端滑模）、`kVelSmc`（速度滑模）、`kEismc`（误差积分滑模）。纯算法工具类，不依赖任何外设 | `<cmath>` |

---

## 新增模块详情

### INS -- 姿态估计模块

**文件**: `INS/INS.hpp`（应用层）、`INS/QuaternionInsEstimator.hpp`（算法层）

**架构**:
- `INS` 类继承 `LibXR::Application`，拥有独立 1ms 实时线程执行采样消费与姿态更新
- `OnMonitor()` 只负责发布 state 快照（受互斥锁保护），不承担姿态积分主循环
- 内部使用 `InsAlgorithm::QuaternionInsEstimator`，这是一个纯算法类，不与 LibXR/FreeRTOS 绑定

**初始化流程**:
1. 采集 100 次加速度样本求均值
2. 按加速度方向到重力方向的轴角公式构造初始四元数（yaw 初值 = 0）

**安装误差修正**:
- gyro / accel 在进入 EKF 前经过安装误差矩阵修正
- 支持三轴 gyro scale 和 yaw/pitch/roll offset（通过 `Config` 配置）

**EKF 设计**:
- 6 状态 QuaternionEKF：`q0/q1/q2/q3 + gyro_bias_x/y`
- z 轴陀螺零偏不由纯 IMU 观测（仅 x/y 偏置可观）
- 加速度测量噪声可配置（默认 1e6），卡方检验判收敛
- 自适应增益缩放：根据卡方值和收敛态动态调节 EKF 增益

**输出**: `InsState` Topic（在 `App/Topics/InsState.hpp` 中定义）包含：
- yaw/pitch/roll 欧拉角与角速率
- 累计 yaw 总角度（支持 >360 度跨圈）
- 四元数、陀螺零偏估计
- 运动加速度（body/nav/world 三坐标系）

### RLS -- 递推最小二乘估计器

**文件**: `RLS/RLS.hpp`

**模型**: `P_loss = k1 * |omega| + k2 * tau^2 + k3`

- k1：转速损耗系数 (W/(rad/s))
- k2：力矩平方损耗系数 (W/(Nm^2))
- k3：由外部预置的待机固定损耗

**特点**:
- 纯标量实现：所有矩阵运算（P 更新、增益向量 K 计算）展开为 float 四则运算
- 显式遗忘因子 lambda（默认 0.9999），对旧数据指数衰减
- 参数估计结果强制 >= 1e-5，满足物理意义
- 零外部依赖（不依赖 CMSIS-DSP、Eigen 等矩阵库）

**接口**: `SetParams()` 手动初始化，`Update()` 每次迭代递推，`Reset()` 完全重置，`GetParams()` 读取估计结果

### SMC -- 滑模控制器

**文件**: `SMC/SmcController.hpp`

**5 种控制模式**:

| 模式 | 滑模面 | 推荐场景 |
|------|--------|----------|
| `kExponent` | s = c*e + de | Yaw 轴位置控制（首选） |
| `kPower` | s = c*e + de（幂次趋近） | Yaw 轴位置控制（替代方案） |
| `kTfsmc` | s = beta*e^(q/p) + de | 快速终端滑模，收敛速度更快 |
| `kVelSmc` | s = de + c*int(de) | 速度环控制 |
| `kEismc` | s = c1*e + de + c2*int(e) | Pitch 轴位置控制（首选，误差积分） |

**关键特性**:
- 饱和函数 `Sat()` 替代符号函数，抑制抖振
- 死区保护：位置误差 < dead_zone 时输出 0
- 输出钳位：`u_max` 限制最大输出
- 参数切换时的积分续接（`OutContinuation`）
- `Reset()` / `ClearIntegral()` 状态重置

**编译测试**: 位于 `App/Dev/CompileTests/smc_controller_compile_test.cpp`，覆盖 7 个测试用例（默认构造、参数构造、SetParam、位置控制、速度控制、5 种模式全量、死区、Reset/ClearIntegral）

---

## 对外接口

每个模块通过 `LibXR::Topic` 发布私有状态：

| 模块 | Topic 名称 | 数据类型 | 说明 |
|------|-----------|----------|------|
| DT7 | `remote_control_state` | `DT7State` | 遥控器输入状态 |
| VT13 | `vt13_state` | `VT13State` | VT13 输入状态 |
| MasterMachine | `master_machine_state` | `MasterMachineState` | 上位机命令意图 |
| Referee | `referee_state` | `RefereeState` | 裁判系统比赛/功率数据 |
| CANBridge | `can_bridge_state` | `CANBridgeState` | 桥接链路状态 |
| SuperCap | (内部 Topic) | `SuperCapState` | 超级电容状态 |
| BMI088 | `bmi088_gyro` / `bmi088_accl` | `Vector3f` | IMU 原始数据 |
| INS | `ins_state` | `InsState` | 姿态估计输出（发布到 App 域） |

**设计约束**: 模块私有状态结构体 **不进入** `App/Topics/` 公共契约。App 层通过订阅模块 Topic，在 View/Helper 中将其折叠为稳定的公共语义。`InsState` 是例外，它被设计为跨控制器共享的公共契约，因此定义在 `App/Topics/` 中。

---

## 关键依赖与配置

- 所有模块依赖 LibXR 的 `app_framework.hpp`（Application/ApplicationManager）
- 电机模块依赖 LibXR 的 `can.hpp` + `pid.hpp`
- IMU/BMI088 模块依赖 LibXR 的 `spi.hpp` + `gpio.hpp` + `pwm.hpp` + `transform.hpp`
- INS 模块依赖 LibXR 的 `app_framework.hpp` + `message.hpp` + `mutex.hpp` + `App/Topics/InsState.hpp`
- 通信模块依赖 LibXR 的 `uart.hpp` + `semaphore.hpp` + `thread.hpp`
- 部分模块引用 `App/Config/` 中的配置结构体（如 Referee -> RefereeConfig, CANBridge -> BridgeConfig）
- RLS 和 SMC 模块为纯算法模块，零外部依赖
- 模块接口中的 `const char*` 名称参数可通过 `LibXR::RuntimeStringView` 的隐式转换兼容（详见 App/CLAUDE.md FAQ）

---

## 模块管理

```yaml
# modules.yaml - 通过 xrobot-org 包管理的模块
modules:
  - xrobot-org/BlinkLED
  - xrobot-org/BMI088
  - xrobot-org/SharedTopic
  - xrobot-org/SharedTopicClient
  - xrobot-org/BuzzerAlarm

# sources.yaml - 模块仓库索引
sources:
  - url: https://xrobot-org.github.io/xrobot-modules/index.yaml
    priority: 0
```

其余模块（DJIMotor, DMMotor, DT7, VT13, Referee, MasterMachine, CANBridge, SuperCap, INS, RLS, SMC）为本地项目内模块。

---

## 相关文件清单

```
Modules/
  modules.yaml
  sources.yaml
  BlinkLED/BlinkLED.hpp, CMakeLists.txt
  BMI088/BMI088.hpp, CMakeLists.txt
  SharedTopic/SharedTopic.hpp, CMakeLists.txt
  SharedTopicClient/SharedTopicClient.hpp, CMakeLists.txt
  BuzzerAlarm/BuzzerAlarm.hpp
  DJIMotor/DJIMotor.hpp
  DMMotor/DMMotor.hpp
  DT7/DT7.hpp
  VT13/VT13.hpp
  Referee/Referee.hpp
  MasterMachine/MasterMachine.hpp
  CANBridge/CANBridge.hpp
  SuperCap/SuperCap.hpp
  INS/INS.hpp, QuaternionInsEstimator.hpp, CMakeLists.txt
  RLS/RLS.hpp, CMakeLists.txt
  SMC/SmcController.hpp, CMakeLists.txt
```
