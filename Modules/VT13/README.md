# VT13

rmpp VT13 遥控器模块，解析 VT13 串口协议并发布设备级私有状态。

## Required Hardware

- `VT13::Config::uart_name` 指向的 UART 设备

## Constructor Arguments

- `VT13::Config`
  - `uart_name`: UART 硬件别名，默认 `uart_vt13`
  - `task_stack_depth`: 接收线程栈深度，默认 `2048`
  - `state_topic_name`: 状态 Topic 名称，默认 `vt13_state`
  - `thread_name`: 接收线程名称
  - `offline_timeout_ms`: 离线超时时间
  - `joystick_deadband`: 摇杆死区

## Template Arguments

None

## Depends

None

## Published Topics

- `vt13_state`
  - Type: `VT13State`
  - Scope: 模块私有 Topic

## Notes

该模块不处理 VT13 所接 UART 的 CubeMX 配置收口；若无运行错误，串口配置按当前工程策略保持不动。
