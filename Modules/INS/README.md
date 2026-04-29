# INS

`INS` 是基于 BMI088 `bmi088_gyro` / `bmi088_accl` Topic 的姿态观测模块，
对外发布 App 域 `ins_state`。

当前实现按 donor `ins_task.c/.h` 的核心语义收口，但保留 LibXR/XRobot 的
模块边界：

- BMI088 仍负责 SPI 采样、温控、驱动层静态陀螺零偏和原始 Topic 发布。
- INS 内部使用独立 1ms 实时线程执行采样消费与姿态更新，`OnMonitor()` 只负责
  发布/监测状态，不承担姿态积分主循环。
- 初始四元数使用 100 次加速度样本均值，并按加速度方向到重力方向的轴角公式
  构造，yaw 初值为 0。
- gyro / accel 在进入 EKF 前经过 donor 风格安装误差矩阵修正，支持三轴 gyro
  scale 与 yaw/pitch/roll offset。
- 姿态融合使用固定尺寸 6 状态 QuaternionEKF：`q0/q1/q2/q3 + gyro_bias_x/y`，
  z 轴陀螺零偏不由纯 IMU 观测。
- 运动加速度先扣除估计重力，再按 donor `AccelLPF = 0.0085` 的
  `dt / (AccelLPF + dt)` 公式低通。
- 输出角速度沿用 donor `INS.Gyro` 语义：来自 BMI088 gyro 经安装误差修正后的
  当前角速度，不回写 EKF 估计的 x/y bias。

与 donor 不同的是，本模块不恢复全局 `INS_Task()`、全局 `QEKF_INS` 或旧 C
动态 `kalman_filter.c` 对象模型；算法语义落在当前 `Modules/INS` 内，并通过
`InsState` Topic 提供给 App 控制器。
