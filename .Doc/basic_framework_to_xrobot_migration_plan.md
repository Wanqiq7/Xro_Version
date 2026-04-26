# basic_framework 到 XRobot/LibXR 的迁移决策表与移植方案

## 1. 文档目标

本文档用于回答两个问题：

1. `basic_framework` 的哪些部分应该直接被 XRobot/LibXR 官方能力替换。
2. 在当前 `F:\Xrobot\Xro_template` 板级模板中，如何以最小冲突方式搭建迁移骨架，并逐步迁入原有业务架构。

本文档基于以下信息综合整理：

- 当前仓库代码结构与入口链
- `basic_framework-master` 的原始分层与任务组织
- `xrobot-agent-context.md` 中引用的 XRobot 官方文档
- 前序研究结论：当前模板边界、旧框架边界、官方中间件能力、官方工作流与调试/通信约束

## 2. 总体结论

迁移时应遵循一句话原则：

**迁移 `basic_framework` 的业务架构模式，不迁移它的旧底座实现。**

更具体地说：

- 该保留的是：
  - `cmd -> chassis/gimbal/shoot` 这类业务分责
  - 命令流、模式切换、急停、容错、跨模块协同
  - 领域 Topic、领域状态机、协议语义
- 不该保留的是：
  - `RobotInit + RobotTask + OSTaskInit` 这一套旧式总入口/总调度中心
  - `message_center` 的自制消息总线实现
  - `daemon` 的旧式全局数组轮询 watchdog 实现
  - 直接暴露 HAL 句柄到模块层的 BSP 实现方式

## 3. 当前模板的既有边界

在当前仓库里，以下边界已经由 XRobot/LibXR 模板占住：

### 3.1 板级入口与依赖注入

- [`User/app_main.cpp`](F:/Xrobot/Xro_template/User/app_main.cpp#L61) 是当前模板的板级入口。
- 它负责：
  - `PlatformInit(...)`
  - HAL 句柄封装为 `STM32GPIO/STM32SPI/STM32UART/...`
  - 组装 `HardwareContainer`
  - 最后调用 `XRobotMain(peripherals)`

这意味着：**硬件对象创建、板级资源命名、平台初始化已经有唯一入口，不应再引入第二套。**

### 3.2 运行时入口

- [`Core/Src/freertos.c`](F:/Xrobot/Xro_template/Core/Src/freertos.c#L115) 在默认任务中调用 `app_main()`

这意味着：**FreeRTOS 启动链已经接好，不应再迁入 `basic_framework` 的总任务初始化入口。**

### 3.3 模块运行骨架

- [`Middlewares/Third_Party/LibXR/src/middleware/app_framework.hpp`](F:/Xrobot/Xro_template/Middlewares/Third_Party/LibXR/src/middleware/app_framework.hpp#L120)
  已提供：
  - `Application`
  - `ApplicationManager`
  - `HardwareContainer`

- [`User/xrobot_main.hpp`](F:/Xrobot/Xro_template/User/xrobot_main.hpp#L7)
  当前负责：
  - 创建 `ApplicationManager`
  - 实例化模块
  - 调用 `MonitorAll()`

这意味着：**模块生命周期和轮询式调度骨架已经存在。**

## 4. basic_framework 原始架构中真正值得继承的部分

基于 `basic_framework-master` 的 README 与代码，其最值得继承的不是“旧实现”，而是下面这些架构思想：

### 4.1 三层职责

`bsp -> module -> app`

- `bsp`：硬件/外设抽象
- `module`：算法、传感器、执行器、协议适配
- `app`：机器人业务逻辑

### 4.2 命令流解耦

`robot_cmd` 负责把遥控器/视觉/键鼠等输入解释成统一命令，再由 `chassis/gimbal/shoot` 订阅执行。

### 4.3 任务分频思维

高频控制、低频巡检、UI/桥接任务分开考虑，这个“频率分层思维”是对的。

### 4.4 健康检查与降级处理

守护线程背后的思想是值得保留的，但实现方式不应原样迁入。

## 5. 官方 XRobot/LibXR 文档给出的迁移约束

### 5.1 工具链分责必须清楚

官方定义：

- `CodeGenerator`：生成外设初始化代码与工程入口
- `LibXR`：提供运行时、驱动抽象、中间件
- `XRobot`：负责模块源、依赖、实例化与入口生成

参考：

- <https://xrobot-org.github.io/docs/intro>

迁移含义：

- 不能再把“生成层、运行时层、业务层”揉成一个旧式框架。

### 5.2 ISR 只负责交接，线程负责展开

参考：

- <https://xrobot-org.github.io/docs/concept>

迁移含义：

- ISR/HAL callback 不应承载协议解析、控制逻辑、业务决策。

### 5.3 公共接口不能泄露平台类型

参考：

- <https://xrobot-org.github.io/docs/concept>

迁移含义：

- `SPI_HandleTypeDef*`、`UART_HandleTypeDef*` 等不应继续上浮到模块层与业务层。

### 5.4 官方工作流的根目录语义不能破坏

官方更偏向：

- `Modules/`：模块仓库根目录
- `User/`：用户配置与生成入口

参考：

- <https://xrobot-org.github.io/docs/proj_man>

迁移含义：

- 不应把长期业务逻辑堆进 `User/`
- 不应把一次性工程胶水大量塞进 `Modules/`

### 5.5 调试与通信抽象应该后置协议细节

官方在 `debug` 与 `XRUSB` 文档里已经给出：

- SWD 调试抽象
- CDC / GSUSB / CMSIS-DAP v2 等设备类
- 设备身份、接口排序、复合设备约束

参考：

- <https://xrobot-org.github.io/docs/debug>
- <https://xrobot-org.github.io/docs/xrusb>
- <https://xrobot-org.github.io/docs/env_setup>

迁移含义：

- 骨架阶段应预留调试探针、传输抽象、设备身份抽象
- 不应先写死桥接协议、USB 复合接口、遥测格式

## 6. 迁移决策表

下表是本次迁移最核心的决策清单。

### 6.1 可立即替换为官方能力

| 旧能力 / 旧概念 | 官方能力 | 决策 | 说明 |
| --- | --- | --- | --- |
| 板级硬件注册表 | `HardwareContainer` | 直接替换 | 官方已经提供设备注册、别名查找、类型安全接口 |
| 模块轮询器 / 简单 app scheduler | `Application` + `ApplicationManager` | 直接替换 | 可替换旧的模块列表 + 手写轮询 |
| 自制 pub/sub | `Topic` | 直接替换 | 支持同步、异步、队列、回调、域 |
| 自制数据打包/拆包器 | `Topic::PackedData` + `Topic::Server` | 直接替换 | 官方已有统一包格式和解析发布路径 |
| 自制事件通知器 | `Event` | 直接替换 | 适合按键、ISR 触发、状态事件 |
| 自制日志系统 | `Logger` | 直接替换 | 官方日志建立在 Topic 机制上 |
| 线程封装 | `LibXR::Thread` | 直接替换 | 官方已统一不同系统线程语义 |
| 锁/同步封装 | `Mutex` / `Semaphore` | 直接替换 | 尤其 `Semaphore` 兼容 ISR post |
| 软件定时任务器 | `LibXR::Timer` | 基本替换 | 适合周期任务，但不是硬件定时器 ISR 替代品 |
| BSP 统一接口层 | LibXR driver abstraction | 直接替换 | UART/I2C/SPI/CAN/ADC/PWM/Timebase 已有统一抽象 |
| 系统时基包装 | `Timebase` | 直接替换 | 官方要求线程/定时器时间语义收敛到它 |

### 6.2 需要包一层适配

| 旧能力 / 旧概念 | 官方能力 | 决策 | 说明 |
| --- | --- | --- | --- |
| “最新值缓存 + 通知”通道 | `Topic` + `cache` | 适配后替换 | 官方把发布与缓存拆开，需显式设计缓存策略 |
| 中断后丢后台线程的 deferred work | `ASync` | 有条件替换 | 仅适合单实例串行异步，不是线程池 |
| 健康检查 / offline watchdog | `OnMonitor()` + `Timer` + `SystemHealth Topic` | 重建 | 保留思想，不保留旧实现 |
| 跨板 Topic 桥接 | `SharedTopic` / `SharedTopicClient` / XRUSB | 分阶段适配 | 先定义桥接抽象，不先定最终协议 |
| 旧 CAN/UART 通信桥 | `Topic::Server` + 传输抽象 | 适配 | 官方消息层可承接，但链路语义仍需项目定义 |

### 6.3 必须保留自研业务层

| 业务能力 | 决策 | 说明 |
| --- | --- | --- |
| 模式切换 | 保留自研 | 官方不会替你定义机器人行为模式 |
| 急停 / 降级 / 容错 | 保留自研 | 只能利用官方底座实现，不会被官方中间件替代 |
| `robot_cmd` 命令仲裁 | 保留职责，自研重构 | 这是最值得迁的业务核心 |
| `chassis/gimbal/shoot` 业务角色层 | 保留职责，自研重构 | 它们是业务边界，不是底座能力 |
| Topic/Event 的领域命名与 schema | 保留自研 | 官方只给机制，不给领域语义 |
| 协议字段、版本、兼容策略 | 保留自研 | 官方只给打包框架 |
| 启动顺序、依赖检查、故障恢复 | 保留自研 | 需结合具体机器人系统设计 |
| 实时性预算、栈大小、缓冲区预算 | 保留自研 | 官方不替项目做资源预算 |

## 7. 推荐的目标目录结构

建议在当前仓库上逐步演进为：

```text
User/
  app_main.cpp            # 板级入口，HAL -> LibXR 封装
  xrobot_main.hpp         # 入口对接或薄装配层
  xrobot.yaml             # 模块实例与参数配置

Modules/
  modules.yaml            # 仓库依赖清单
  sources.yaml            # 模块源索引
  <ReusableModule>/       # 可复用业务模块 / 驱动适配 / 桥接器 / 算法

App/
  Runtime/                # 顶层业务 orchestrator
  Roles/                  # 角色接口与角色装配
  Topics/                 # 领域 Topic 定义
  Config/                 # 逻辑角色 -> 资源/部署映射
```

### 7.1 各目录职责

#### `User/`

只保留：

- 板级入口
- 生成入口
- XRobot 配置
- 少量稳定接线代码

不应长期承载：

- 复杂状态机
- 长期业务编排
- 大量 Topic 路由
- 复杂容错逻辑

#### `Modules/`

适合放：

- 传感器模块
- 执行器模块
- 算法模块
- 通信/桥接适配模块
- 可复用 service

不适合放：

- 一次性工程 glue code
- 板级专用脚本
- 顶层业务 orchestrator

#### `App/`

适合放：

- `robot_cmd` 思想的业务重建
- `chassis/gimbal/shoot` 的业务角色层
- 模式管理
- 降级与协同逻辑

注意：

- 这不是官方默认目录
- 必须手工接入构建系统
- 不能另起第二套入口与实例化中心

## 8. 推荐的业务骨架

### 8.1 角色层

骨架阶段建议先定义以下角色：

- `InputRole`
- `DecisionRole`
- `ActuatorRole`
- `TelemetryRole`
- `BridgeRole`

### 8.2 领域 Topic

骨架阶段建议先定义：

- `MotionCommand`
- `AimCommand`
- `FireCommand`
- `RobotMode`
- `PoseEstimate`
- `SystemHealth`
- `BoardLinkState`

原则：

- 先定义跨角色稳定契约
- 不要先定义模块内部字段
- 不要先把 PID、寄存器流、原始 DMA 缓冲写进 Topic schema

## 9. 迁移顺序

建议按下面顺序推进：

### 阶段 1：固定边界

- 明确唯一顶层运行时入口
- 禁止引入第二套 `RobotTask/OSTaskInit`
- 明确 `User -> AppRuntime -> Roles/Modules` 的单向依赖

### 阶段 2：建立骨架

- 新建 `App/Runtime`、`App/Roles`、`App/Topics`、`App/Config`
- 补齐 `App/` 的 CMake 接入
- 保持骨架在无业务模块迁移情况下可空跑

### 阶段 3：替换底座能力

- 用官方能力替换：
  - 硬件注册
  - 线程/锁/定时器
  - Topic/Event/Logger
  - 消息打包解析

### 阶段 4：迁移业务壳

- 先迁 `robot_cmd` 职责
- 再迁 `chassis/gimbal/shoot` 的业务角色层
- 暂不急着迁底层具体模块实现

### 阶段 5：迁移具体模块

- 传感器
- 电机/执行器
- 裁判系统
- 桥接器
- 视觉与遥测

## 10. 风险与禁区

### 10.1 明确禁止

- 再造一个 `RobotInit/RobotTask/OSTaskInit` 总调度中心
- 再造一个 `message_center`
- 把长期业务继续堆进 `User/`
- 把 `Modules/` 变成杂物箱
- 把 HAL 句柄继续上浮到模块层
- 先写死板型宏、桥接协议、USB 复合接口布局

### 10.2 高风险误判

#### 把 `ApplicationManager` 当成完整应用框架

实际它只负责：

- 模块注册
- 周期性调用 `OnMonitor()`

它不负责：

- 业务状态机
- 启动依赖图
- 错误传播总线
- 机器人策略层

#### 把 `Topic` 当成旧消息总线换皮

需注意：

- `cache` 不是默认开启
- `multi_publisher=true` 会改变同步语义
- `Event` 与 `Topic` 的使用边界不同

#### 把 `Timer` 当硬实时中断

官方 `Timer` 是软调度，不是硬件 compare/ISR 替代品。

## 11. 骨架阶段最小闭环清单

1. 明确唯一顶层运行时入口
2. 定义 4 到 6 个逻辑角色
3. 定义 5 到 7 个领域 Topic
4. 约定硬件别名、角色名、Topic 名三套命名
5. 建立 `User -> AppRuntime -> Roles/Modules` 的单向依赖
6. 为 `App/` 提供正式构建接入
7. 列出可跨板同步 Topic 白名单
8. 骨架在无业务模块迁移时也能自洽空跑

## 12. 结论

最稳妥的迁移方式不是“把 `basic_framework` 移植到 XRobot”，而是：

**把 `basic_framework` 拆成：**

- 官方底座可替代部分
- 需要适配的运行时语义部分
- 必须保留的业务架构部分

然后再嫁接到当前模板：

- `User/` 保留板级与生成入口
- `Modules/` 承接可复用能力
- `App/` 承接业务编排层
- `LibXR/XRobot` 替代旧底座

这样做的结果是：

- 与官方工作流兼容
- 与当前模板入口链兼容
- 业务层仍然保留 `basic_framework` 的核心价值
- 后续迁移具体模块时不会陷入“双入口、双调度、双消息系统”的结构冲突

## 13. 主要参考

- XRobot 官方总入口：<https://xrobot-org.github.io/docs/intro>
- XRobot 官方设计思想：<https://xrobot-org.github.io/docs/concept>
- 基础编程：<https://xrobot-org.github.io/docs/basic_coding>
- Application 框架：<https://xrobot-org.github.io/docs/basic_coding/middleware/app-framework>
- 消息系统：<https://xrobot-org.github.io/docs/basic_coding/middleware/message>
- Event：<https://xrobot-org.github.io/docs/basic_coding/middleware/event>
- Logger：<https://xrobot-org.github.io/docs/basic_coding/middleware/logger>
- 系统抽象：<https://xrobot-org.github.io/docs/basic_coding/system>
- 驱动抽象：<https://xrobot-org.github.io/docs/basic_coding/driver>
- 项目管理：<https://xrobot-org.github.io/docs/proj_man>
- 环境配置：<https://xrobot-org.github.io/docs/env_setup>
- 调试：<https://xrobot-org.github.io/docs/debug>
- XRUSB：<https://xrobot-org.github.io/docs/xrusb>
- XRobot Onboarding：<https://xrobot-org.github.io/XRobot-Onboarding/>
