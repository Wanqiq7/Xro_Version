# DJIMotor

DJI M2006/M3508/GM6020 电机通用能力模块，负责 CAN 反馈解码、分组发帧和基础闭环接口。

## Required Hardware

- `can1` 或 `can2`

## Constructor Arguments

- `DJIMotorGroupConfig`
  - `can_name`: CAN 硬件别名，默认 `can1`
  - `command_frame_id`: 分组控制帧 ID，默认 `0x200`
- `DJIMotorConfig`
  - `name`: 电机实例名称
  - `motor_type`: 电机类型
  - `feedback_id`: 电机反馈帧 ID
  - `command_slot`: 分组控制帧中的 0-3 槽位
  - `direction`: 电机方向
  - `reduction_ratio`: 减速比
  - `default_outer_loop`: 默认闭环模式
  - `pid`: 电流、速度、角度 PID 参数

## Template Arguments

None

## Depends

None

## Published Topics

None

## Notes

该模块只提供通用电机实例、反馈解码、分组发帧和基础闭环能力。云台、底盘、发射机构的具体业务编排应放在 `App/` 控制器中。
