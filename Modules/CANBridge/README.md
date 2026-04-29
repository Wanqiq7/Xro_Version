# CANBridge

CAN 桥接链路观测模块，负责最小 heartbeat 遥测帧收发、白名单校验和链路状态发布。

## Required Hardware

- `App::Config::CANBridgeConfig::can_name` 指向的 CAN 设备

## Constructor Arguments

- `App::Config::CANBridgeConfig`
  - `can_name`: CAN 硬件别名
  - `direction`: 桥接方向
  - `tx_frame_id`: 发送帧 ID 白名单
  - `rx_frame_id`: 接收帧 ID 白名单
  - `tx_period_ms`: heartbeat 发送周期
  - `offline_timeout_ms`: 离线超时时间

## Template Arguments

None

## Depends

None

## Published Topics

- `can_bridge_state`
  - Type: `CANBridgeState`
  - Scope: 模块私有 Topic

## Notes

该模块不向业务 Topic 写入命令，也不承担业务旁路控制，只提供桥接链路状态观测。
