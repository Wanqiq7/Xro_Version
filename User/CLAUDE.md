[根目录](../CLAUDE.md) > **User**

# User -- 用户板级配置与入口桥接

## 变更记录 (Changelog)

| 日期 | 操作 | 说明 |
|------|------|------|
| 2026-04-23 | 新建 | 首次扫描 |

---

## 模块职责

`User/` 目录是连接 STM32CubeMX 生成代码与 XRobot 应用层的桥接层，负责：
1. 将 HAL 外设句柄包装为 LibXR 抽象对象
2. 构建 `HardwareContainer` 外设注册表（按名称查找）
3. 定义 Flash 分区映射
4. 提供 YAML 配置文件供 LibXR 代码生成工具使用

---

## 关键文件

### app_main.h / app_main.cpp -- 入口桥接

`app_main()` 是 C linkage 函数，由 `freertos.c` 中的默认任务调用。在 `app_main.cpp` 中：

1. 创建 LibXR Timebase (`STM32TimerTimebase`, TIM14)
2. 调用 `PlatformInit(2, 1024)` 初始化 LibXR 平台（2 个软件定时器，1024 栈深度）
3. 创建电源管理器、Flash、Database、RamFS
4. **逐一包装 HAL 外设为 LibXR 对象**:
   - GPIO: KEY, CS1_ACCEL, CS1_GYRO, INT_ACC, INT_GYRO, INT_MAG, MAG_RST
   - ADC: adc1 (温度传感器, 内部参考)
   - PWM: TIM1_CH1~4, TIM10_CH1, TIM4_CH3, TIM5_CH1~3
   - DAC: dac_out2
   - SPI: spi1 (IMU), spi2
   - UART: usart1 (上位机), usart2 (VT13 遥控), usart3, usart6 (裁判系统)
   - I2C: i2c2, i2c3
   - CAN: can1, can2 (also aliased as can_bridge)
5. 组装 `HardwareContainer` 并为外设分配名称/别名
6. 调用 `XRobotMain(peripherals)` 进入应用层

### 外设名称别名映射

| LibXR 名称 | 别名 | HAL 句柄 | 用途 |
|-----------|------|----------|------|
| `spi1` | `spi_bmi088` | hspi1 | BMI088 IMU |
| `CS1_ACCEL` | `bmi088_accl_cs` | -- | IMU 加速度计片选 |
| `CS1_GYRO` | `bmi088_gyro_cs` | -- | IMU 陀螺仪片选 |
| `INT_GYRO` | `bmi088_gyro_int` | -- | IMU 陀螺仪中断 |
| `pwm_tim10_ch1` | `pwm_bmi088_heat` | htim10 | IMU 加热 |
| `usart1` | `uart_master_machine` | huart1 | 上位机通信 |
| `usart2` | `uart_vt13` | huart2 | VT13 遥控器 |
| `usart6` | `uart_referee` | huart6 | 裁判系统 |
| `can2` | `can_bridge` | hcan2 | CAN 桥接 |

### xrobot_main.hpp -- 应用入口

单行委托到 `App::RunXRobotAssembly(hw)`。

### flash_map.hpp -- Flash 分区

STM32F407IGH6 的 12 个 Flash 扇区映射（4x16KB + 1x64KB + 7x128KB = 1MB）。

### xrobot.yaml -- 模块配置

```yaml
global_settings:
  monitor_sleep_ms: 1    # MonitorAll 循环间隔
modules:
  - name: BlinkLED
    constructor_args:
      blink_cycle: 250   # LED 闪烁周期 (ms)
```

### libxr_config.yaml -- LibXR 硬件配置

定义 SPI/I2C/USART/ADC/CAN/USB/DAC/Terminal/FlashLayout 等全部外设参数，供 LibXR 代码生成工具读取，自动生成 `app_main.cpp` 中的初始化代码。

---

## 相关文件清单

```
User/
  app_main.h              # C linkage 声明
  app_main.cpp            # 外设包装 + HardwareContainer 组装
  xrobot_main.hpp         # 委托到 App::RunXRobotAssembly
  flash_map.hpp           # Flash 扇区映射
  xrobot.yaml             # 模块运行时配置
  libxr_config.yaml       # LibXR 硬件配置 (代码生成用)
```
