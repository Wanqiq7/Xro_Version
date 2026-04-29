[根目录](../CLAUDE.md) > **Modules**

# Modules -- 设备驱动模块集合

## 变更记录 (Changelog)

| 日期 | 操作 | 说明 |
|------|------|------|
| 2026-04-23 | 新建 | 首次扫描 |

---

## 模块职责

`Modules/` 目录包含所有硬件设备的驱动模块。每个模块是一个独立的 header-only C++ 类（继承 `LibXR::Application`），通过 LibXR 的 `HardwareContainer` 按名称查找外设，通过 `Topic` 系统发布私有状态。

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
| **BMI088** | `BMI088/BMI088.hpp` | 博世 BMI088 6轴 IMU 驱动。含加速度计/陀螺仪采集、温度补偿 | SPI + GPIO(CS/INT) + PWM(加热) + ramfs + database |

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

**设计约束**: 模块私有状态结构体 **不进入** `App/Topics/` 公共契约。App 层通过订阅模块 Topic，在 View/Helper 中将其折叠为稳定的公共语义。

---

## 关键依赖与配置

- 所有模块依赖 LibXR 的 `app_framework.hpp`（Application/ApplicationManager）
- 电机模块依赖 LibXR 的 `can.hpp` + `pid.hpp`
- IMU 模块依赖 LibXR 的 `spi.hpp` + `gpio.hpp` + `pwm.hpp` + `transform.hpp`
- 通信模块依赖 LibXR 的 `uart.hpp` + `semaphore.hpp` + `thread.hpp`
- 部分模块引用 `App/Config/` 中的配置结构体（如 Referee -> RefereeConfig, CANBridge -> BridgeConfig）

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

其余模块（DJIMotor, DMMotor, DT7, VT13, Referee, MasterMachine, CANBridge, SuperCap）为本地项目内模块，已按 XRobot 官方模块骨架补齐 `README.md`、`CMakeLists.txt` 与模块级 workflow。

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
```
