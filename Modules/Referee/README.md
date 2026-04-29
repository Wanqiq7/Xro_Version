# Referee

RoboMaster 裁判系统 UART 接入模块，解析裁判系统帧并发布模块私有状态。

## Required Hardware

- `App::Config::RefereeConfig::uart_name` 指向的 UART 设备

## Constructor Arguments

- `App::Config::RefereeConfig`
  - `enabled`: 是否启用串口接收线程
  - `uart_name`: UART 硬件别名
  - `offline_timeout_ms`: 离线超时时间
  - `task_stack_depth`: 接收线程栈深度
  - 其余字段用于离线或缺帧时的保守默认值与限幅

## Template Arguments

None

## Depends

None

## Published Topics

- `referee_state`
  - Type: `RefereeState`
  - Scope: 模块私有 Topic

## Notes

`RefereeState` 不进入 `App/Topics/` 公共契约。App 层需要通过视图或控制器把裁判状态折叠为稳定业务语义。
