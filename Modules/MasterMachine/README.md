# MasterMachine

上位机视觉桥接模块，负责通过一个或多个 `LibXR::UART` 链路接收视觉控制帧，并向视觉端发送 donor 风格状态帧。当前模块只发布 `master_machine_state` 私有 Topic，不直接承担 App 业务决策。

## Required Hardware

- `App::Config::MasterMachineConfig::links[].device_name` 指向的 UART 设备。
- 默认配置同时尝试绑定：
  - `uart_master_machine`
  - `vcp_master_machine`

## Configuration

- `direction`: 桥接方向，控制 RX/TX 是否启用。
- `links`: 物理链路列表，每条链路可独立配置 `enable_rx / enable_tx`。
- `ingress_mode`: 上位机输入接管模式。
- `inbound_whitelist`: 允许进入 App 层的字段白名单。
- `offline_timeout_ms`: 任一 RX 链路最后一次合法帧后的全局离线超时。
- `task_stack_depth`: 每条 RX 线程栈深度。

多链路语义：

- RX：每条启用 RX 的链路各有一个接收线程；合法帧采用 last-writer-wins 更新同一个模块状态。
- TX：发送状态时广播到所有启用 TX 且已绑定成功的链路；任一链路发送成功即返回 `OK`。
- Online：当前为全局 online 状态，任一链路收到合法帧即刷新在线时间。

## Wire Format

字节序均为 little-endian，角度在 wire format 中使用弧度，进入 App typed 状态后转换为角度。

### RX: Vision -> Robot

- `cmd_id`: `0x0001`
- `data_length`: `10`
- payload: `flags(uint16) + pitch(float) + yaw(float)`
- frame size: `18`

帧布局：

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 1 | SOF `0xA5` |
| 1 | 2 | `data_length` |
| 3 | 1 | CRC8 over bytes `[0..2]` |
| 4 | 2 | `cmd_id` |
| 6 | 2 | `flags` |
| 8 | 4 | `pitch_rad` |
| 12 | 4 | `yaw_rad` |
| 16 | 2 | CRC16 over bytes `[0..15]` |

RX flags 当前冻结布局：

| Bit range | Field |
| --- | --- |
| `1:0` | `fire_mode` |
| `3:2` | `target_state` |
| `7:4` | `target_type` |
| `8` | `manual_override` 本地扩展 |
| `9` | `request_fire_enable` 本地扩展 |
| `15:10` | reserved |

`AUTO_FIRE` 只表示 `fire_mode`，进入 App 后映射为连续射；`request_fire_enable` 是独立边沿位，进入 App 后上升沿映射为单发请求。

### TX: Robot -> Vision

- `cmd_id`: `0x0002`
- `data_length`: `14`
- payload: `flags(uint16) + yaw(float) + pitch(float) + roll(float)`
- frame size: `22`

TX flags 当前冻结布局：

| Bit range | Field |
| --- | --- |
| `1:0` | `enemy_color` |
| `3:2` | `work_mode` |
| `7:4` | reserved |
| `15:8` | `bullet_speed` |

`work_mode` wire enum 已包含 `Aim / SmallBuff / BigBuff`。当前 App 层尚无 small buff / big buff 业务入口，因此 `MasterMachineBridgeController` 会保守发送 `Aim`；底层 wire enum 已可编码这两种模式。

## Published Topics

- `master_machine_state`
  - Type: `MasterMachineState`
  - Scope: 模块私有 Topic

## Boundary

协议对象不会升级为 App 公共 Topic；`InputController` 只消费白名单允许的 typed 语义，并把稳定意图写入 `OperatorInputSnapshot`。
