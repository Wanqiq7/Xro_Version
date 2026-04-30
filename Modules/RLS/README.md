# RLS

2维递推最小二乘（Recursive Least Squares）算法模块，用于在线估计机器人底盘功率模型的未知参数。

## 算法简介

RLS 是一种自适应滤波算法，通过递推方式最小化带遗忘因子的加权误差平方和。

本模块采用的模型：

```
P_loss = k1 * |omega| + k2 * tau^2 + k3
```

- k1：转速损耗系数 (W / (rad/s))
- k2：力矩平方损耗系数 (W / (Nm^2))
- k3：待机固定损耗 (W)，由外部预置

估计器对 k1、k2 进行在线辨识，实测损耗作为观测量输入。

特点：

- **纯标量实现**：所有矩阵运算（P 的更新、增益向量 K 的计算）展开为 float 四则运算，零外部依赖
- **遗忘因子 lambda**：对旧数据指数衰减，适应时变系统
- **正值约束**：参数估计结果强制 >= 1e-5，满足物理意义

## 接口说明

```cpp
Module::RLS::RlsEstimator rls(/* RlsEstimator::Config{delta=1e-5, lambda=0.9999} */);

// 手动设置初始参数（可选）
rls.SetParams(0.22f, 1.2f);

// 每次迭代更新
auto state = rls.Update(omega_sum,   // 四个电机 |omega| 之和 (rad/s)
                         tau_sq_sum, // 四个电机 tau^2 之和 (Nm^2)
                         measured_loss, // 实测功率损耗 (W)
                         dt_s);     // 时间步长（保留参数）

// 读取估计结果
float k1 = state.k1;
float k2 = state.k2;

// 重置
rls.Reset();
```

## 参考来源

- HKUST ENTERPRIZE RM2024 PowerModule（原始 RLS 实现，依赖 CMSIS-DSP）
- [Recursive Least Squares - Wikipedia](https://en.wikipedia.org/wiki/Recursive_least_squares_filter)

## Constructor Arguments

- `RlsEstimator::Config`
  - `delta`: 协方差矩阵 P 初始对角值，默认 `1e-5`
  - `lambda`: 遗忘因子，默认 `0.9999`

## Template Arguments

None

## Depends

None（纯标量运算，无外部依赖）

## Published Topics

None（纯算法模块，不发布 Topic）
