[根目录](../CLAUDE.md) > **Core**

# Core -- STM32CubeMX 生成的外设初始化层

## 变更记录 (Changelog)

| 日期 | 操作 | 说明 |
|------|------|------|
| 2026-04-23 | 新建 | 首次扫描 |

---

## 模块职责

`Core/` 目录包含由 STM32CubeMX 自动生成的代码，负责：
1. MCU 系统初始化（时钟树、中断向量）
2. 外设初始化（GPIO、ADC、CAN、DAC、DMA、I2C、RNG、RTC、SPI、TIM、USART）
3. FreeRTOS 入口（默认任务创建，桥接到 `app_main()`）
4. HAL 中断回调

**重要**: 此目录大部分文件由 CubeMX 重新生成时会被覆盖，用户自定义代码必须写在 `USER CODE BEGIN/END` 标记之间。

---

## 入口与启动

```
main.c::main()
  -> HAL_Init()
  -> SystemClock_Config()          // HSI PLL -> 168MHz SYSCLK
  -> MX_*_Init()                   // 全部外设初始化
  -> osKernelInitialize()          // CMSIS-RTOS V2 初始化
  -> MX_FREERTOS_Init()            // 创建 defaultTask (8KB 栈)
  -> osKernelStart()               // 启动调度器

freertos.c::StartDefaultTask()
  -> app_main()                    // 桥接到 User/app_main.cpp
  -> vTaskDelete(NULL)
```

---

## 关键文件

### 源文件 (Src/)

| 文件 | 职责 |
|------|------|
| `main.c` | 程序入口、时钟配置 (HSI 8MHz -> PLL 168MHz)、外设初始化调用链 |
| `freertos.c` | FreeRTOS 初始化、默认任务 (8KB 栈, Normal 优先级) |
| `gpio.c` | GPIO 初始化 (IMU CS/INT, LED, 按键, 蜂鸣器等) |
| `adc.c` | ADC1 初始化 (温度传感器/内部参考电压) |
| `can.c` | CAN1/CAN2 初始化 |
| `spi.c` | SPI1/SPI2 初始化 (IMU 通信) |
| `tim.c` | TIM1/4/5/8/10 初始化 (PWM 输出) |
| `usart.c` | USART1/2/3/6 初始化 (遥控器/裁判/上位机) |
| `i2c.c` | I2C2/I2C3 初始化 |
| `dac.c` | DAC 初始化 (通道 2) |
| `dma.c` | DMA 控制器初始化 |
| `rng.c` | 随机数生成器初始化 |
| `rtc.c` | RTC 初始化 |
| `crc.c` | CRC 硬件计算单元初始化 |
| `stm32f4xx_it.c` | 中断服务函数 |
| `stm32f4xx_hal_msp.c` | HAL MSP 初始化回调 |
| `stm32f4xx_hal_timebase_tim.c` | HAL 时基 (TIM14) |
| `system_stm32f4xx.c` | SystemInit |
| `sysmem.c` / `syscalls.c` | Newlib 系统调用存根 |

### 头文件 (Inc/)

| 文件 | 关键内容 |
|------|----------|
| `main.h` | GPIO 引脚宏定义 (KEY, CS1_ACCEL, CS1_GYRO, INT_ACC, INT_GYRO, INT_MAG, MAG_RST, LED_R/G/B, BUZZER, SERVO 等) |
| `FreeRTOSConfig.h` | FreeRTOS 内核配置 |
| `stm32f4xx_hal_conf.h` | HAL 模块使能宏 |
| 外设头文件 | 各外设句柄声明和初始化函数原型 |

---

## 外设映射 (GPIO 引脚定义)

| 名称 | 引脚 | 端口 | 用途 |
|------|------|------|------|
| KEY | PA0 | GPIOA | 用户按键 |
| CS1_ACCEL | PA4 | GPIOA | BMI088 加速度计 SPI 片选 |
| CS1_GYRO | PB0 | GPIOB | BMI088 陀螺仪 SPI 片选 |
| INT_ACC | PC4 | GPIOC | BMI088 加速度计中断 (EXTI4) |
| INT_GYRO | PC5 | GPIOC | BMI088 陀螺仪中断 (EXTI9_5) |
| INT_MAG | PG3 | GPIOG | 磁力计中断 (EXTI3) |
| MAG_RST | PG6 | GPIOG | 磁力计复位 |
| LED_R/G/B | PH12/11/10 | GPIOH | RGB LED |
| BUZZER | PD14 | GPIOD | 蜂鸣器 |
| SERVO | PE9 | GPIOE | 舵机 |
| IMU_TEMP | PF6 | GPIOF | IMU 加热 PWM |

---

## 时钟配置

- 振荡器: HSI (内部 RC, 8MHz) + HSE (外部晶振)
- PLL 源: HSI
- PLLM=8, PLLN=168, PLLP=2, PLLQ=7
- SYSCLK: 168 MHz
- AHB: 168 MHz (DIV1)
- APB1: 42 MHz (DIV4)
- APB2: 84 MHz (DIV2)
- HAL 时基: TIM14

---

## 相关文件清单

```
Core/
  Src/
    main.c, freertos.c, gpio.c, adc.c, can.c, crc.c, dac.c, dma.c,
    i2c.c, rng.c, rtc.c, spi.c, tim.c, usart.c,
    stm32f4xx_it.c, stm32f4xx_hal_msp.c, stm32f4xx_hal_timebase_tim.c,
    system_stm32f4xx.c, sysmem.c, syscalls.c
  Inc/
    main.h, FreeRTOSConfig.h, stm32f4xx_hal_conf.h,
    stm32f4xx_it.h, gpio.h, adc.h, can.h, crc.h, dac.h, dma.h,
    i2c.h, rng.h, rtc.h, spi.h, tim.h, usart.h
```
