# basic_framework 板级目录说明

本目录是独立的 CubeMX / LibXR / XRobot 板级工程根目录。

当前已准备好的文件：

- `basic_framework.ioc`：CubeMX 工程配置
- `.config.yaml`：由 `xr_parse_ioc` 从 `.ioc` 解析出的中间配置
- `User/app_main.cpp`：LibXR / XRobot 自动生成的板级入口
- `User/app_main.h`：`app_main()` 声明
- `User/libxr_config.yaml`：LibXR 配置
- `User/flash_map.hpp`：Flash 布局头文件

推荐接入方式：

1. 用 CubeMX 打开 `basic_framework.ioc`
2. 在本目录生成 CMake 工程
3. 在 FreeRTOS 默认任务入口中调用 `app_main()`

典型接线位置：

- `freertos.c` 中的 `StartDefaultTask()`

典型接线代码：

```c
#include "app_main.h"

void StartDefaultTask(void *argument)
{
  app_main();
}
```

如果后续本目录已经生成出完整 CubeMX CMake 工程，建议再执行一次：

```powershell
xr_cubemx_cfg -d F:\Xrobot\board\basic_framework --xrobot
```

这样可以让官方工具进一步补齐 LibXR / XRobot 所需的工程配置。

## 当前串口 / 别名基线

当前仓库里已经稳定接入的 UART 角色如下：

- `USART1` -> `uart_master_machine`
  说明：`MasterMachine`
  波特率：`921600`
  DMA：`USART1_RX -> DMA2_Stream5`，`USART1_TX -> DMA2_Stream7`
- `USART2` -> `uart_vt13`
  说明：`VT13`
  波特率：`921600`
  DMA：`USART2_RX -> DMA1_Stream5`
- `USART3`
  说明：`DT7`
  波特率：`100000`，`9E1`
  DMA：`USART3_RX -> DMA1_Stream1`
- `USART6` -> `uart_referee`
  说明：`Referee`
  DMA：`USART6_RX -> DMA2_Stream2`，`USART6_TX -> DMA2_Stream6`

这些硬件别名的事实源位于 [app_main.cpp](F:/Xrobot/Xro_template/User/app_main.cpp)。

## 当前 bring-up 收口注意事项

- `USART2` 与 `USART3` 在 [usart.c](F:/Xrobot/Xro_template/Core/Src/usart.c) 中仍保留 USER CODE 区的 `RX only` 收口。
  原因：LibXR 的 `STM32UART` 在 UART 处于 `TX` 模式但没有 `TX DMA` 时，可能触发启动期断言。
- `basic_framework.ioc` 本轮已补回 `USART2` 的最小存在配置，但仍未做一次正式的“CubeMX 再生成 -> 全量编译 -> 上板回归”闭环验证。
- 每次修改 `.ioc` 后，至少要重新核对：
  - 唯一启动链仍是 `defaultTask -> app_main() -> XRobotMain() -> ApplicationManager::MonitorAll()`
  - `USART2 / USART3` 的最终 HAL Mode 与 DMA 链接仍满足 LibXR 假设
  - `uart_vt13 / uart_master_machine / uart_referee` 等别名仍可在 `HardwareContainer` 中被查到
