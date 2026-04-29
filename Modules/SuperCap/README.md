# SuperCap

超级电容 CAN 通信模块，负责 donor super_cap 协议的最小收发和状态发布。

## Required Hardware

- `can1` 或配置中的 `can_name`

## Constructor Arguments

- `SuperCap::Config`
  - `can_name`: CAN 硬件别名，默认 `can1`
  - `offline_timeout_ms`: 离线超时时间
  - `state_topic_name`: 状态 Topic 名称，默认 `super_cap_state`

## Template Arguments

None

## Depends

None

## Published Topics

- `super_cap_state`
  - Type: `SuperCapState`
  - Scope: 模块私有 Topic

## Notes

该模块只承载超级电容通信与状态观测，不承担整车功率策略编排。
