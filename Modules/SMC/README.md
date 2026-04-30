# SMC (Sliding Mode Control)

滑模控制器工具类，提供 5 种滑模模式的位置/速度控制算法。

## 模块简介 / Module Description

SMC 是一个纯算法工具类模块，不包含任何硬件依赖。它实现了多种滑模控制（Sliding Mode Control）策略，适用于 RoboMaster 机器人云台/底盘的闭环控制。模块以 header-only 形式提供，直接 include 即可使用。

## 控制模式 / Control Modes

| 模式 | 说明 | 推荐场景 |
|------|------|----------|
| `kExponent` | 指数趋近律 + 线性滑模面 | Yaw 轴位置控制（首选） |
| `kPower` | 幂次趋近律 + 线性滑模面 | Yaw 轴位置控制（替代方案） |
| `kTfsmc` | 快速终端滑模（Terminal Fast SMC） | 非线性滑模面，收敛速度更快 |
| `kVelSmc` | 速度滑模控制 | 速度环控制 |
| `kEismc` | 误差积分滑模（Error Integral SMC） | Pitch 轴位置控制（首选） |

## 硬件需求 / Required Hardware

- 无硬件需求。纯算法模块，不依赖任何外设。

## API 简要 / API Overview

```cpp
// 构造控制器（示例）
Module::SMC<Mode> controller(kp, ki, kd, ...);

// 更新控制量
float output = controller.Update(target, current, dt);
```

具体 API 以 `SMC.hpp` 源码为准。

## 依赖 / Depends

- 无依赖（除 LibXR 基础框架外）。

## 命名空间 / Namespace

- `Module`
