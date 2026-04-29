# DT7

DJI DT7/DBUS 遥控器模块，解析 18 字节 DBUS 帧并发布最小输入状态。

## Required Hardware

- `DT7::Config::uart_name` 指向的 UART 设备

## Constructor Arguments

- `DT7::Config`
  - `uart_name`: UART 硬件别名，默认 `usart3`
  - `task_stack_depth`: 接收线程栈深度，默认 `2048`
  - `state_topic_name`: 状态 Topic 名称，默认 `remote_control_state`
  - `thread_name`: 接收线程名称

## Template Arguments

None

## Depends

None

## Published Topics

- `remote_control_state`
  - Type: `DT7State`
  - Scope: 模块私有 Topic

## Notes

该模块保持 DT7 协议的设备级语义，输入源选择和模式映射由 `App/Cmd/InputController` 完成。
