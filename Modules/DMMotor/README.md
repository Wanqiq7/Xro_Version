# DMMotor

达妙 MIT 协议电机通用能力模块，负责 MIT 五元组打包、基础控制命令、反馈解析和可选级联闭环。

## Required Hardware

- `can1` 或 `can2`

## Constructor Arguments

- `DMMotorConfig`
  - `name`: 电机实例名称
  - `can_name`: CAN 硬件别名，默认 `can1`
  - `tx_id`: 控制帧 ID
  - `feedback_id`: 反馈帧 ID
  - `direction`: 电机方向
  - `feedback_timeout_ms`: 反馈超时时间
  - `default_outer_loop`: 默认闭环模式
  - `mit_limit`: MIT 协议限幅参数
  - `pid`: 速度、角度 PID 参数

## Template Arguments

None

## Depends

None

## Published Topics

None

## Notes

`ZeroPosition()` 属于校准/维修辅助能力，常规业务控制不应在 `App/` 层直接调用。
