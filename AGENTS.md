# Repository Guidelines

## Project Structure & Module Organization
本仓库是基于 STM32CubeMX、LibXR 与 XRobot 的板级模板。`Core/`、`Drivers/`、`startup_stm32f407xx.s` 和 `STM32F407XX_FLASH.ld` 主要承载 HAL、CMSIS 与启动文件；`User/` 放板级入口与运行配置，例如 `app_main.cpp`、`xrobot_main.hpp`、`libxr_config.yaml`；`Modules/` 保存可插拔功能模块，每个模块通常位于 `Modules/<ModuleName>/` 并包含同名 `CMakeLists.txt` 与头文件；`Middlewares/Third_Party/LibXR` 是 Git 子模块，除升级依赖外不要直接修改；`build/` 为本地构建产物，不应提交。

## Build, Test, and Development Commands
使用 CMake 预设进行本地构建：

```powershell
cmake --preset Debug
cmake --build --preset Debug
cmake --preset Release
git submodule update --init --recursive
```

`Debug` 预设默认走 `Ninja + gcc-arm-none-eabi`，输出目录为 `build/<Preset>/`。若重新生成 CubeMX 工程配置，可按 `README.md` 使用 `xr_cubemx_cfg -d . --xrobot`。修改 `.ioc` 或 `Core/` 生成代码后，优先确认 `User/app_main.cpp` 和 `User/xrobot_main.hpp` 的接线仍然有效。

## Coding Style & Naming Conventions
保持与现有 C/C++ 代码一致：缩进使用 2 个空格，类型、模块与类名使用 `PascalCase`，函数和局部变量使用英文标识符，成员变量以 `_` 结尾，例如 `timer_handle_`。模块文件命名遵循 `Modules/BlinkLED/BlinkLED.hpp` 形式；CubeMX 生成文件保留原命名。注释与文档请使用中文，避免修改 `/* User Code Begin/End */` 标记外的自动生成区域。仅在模块清单或必须保持格式的片段周围使用 `clang-format off/on`。

## Architecture Overview
运行入口在 `User/app_main.cpp`：这里负责初始化 HAL 句柄封装、构造 `LibXR::HardwareContainer`，然后把硬件集合交给 `User/xrobot_main.hpp` 中的 `XRobotMain(...)`。后者通过 `ApplicationManager` 注册模块实例，例如 `BlinkLED`。新增功能时，优先做成独立模块并通过 `Modules/modules.yaml` 管理；不要把长期业务逻辑直接堆进 `app_main()`。

## Testing Guidelines
当前仓库未集成独立单元测试框架，也没有 `ctest` 目标。提交前至少完成一次 `cmake --build --preset Debug`，确保固件可编译；涉及板级逻辑时，再在目标板验证 `app_main()` 启动链、外设初始化和模块注册。若新增主机侧测试，请放入 `tests/` 并在 CMake 中显式注册。

## Commit & Pull Request Guidelines
当前分支尚无本地提交历史，建议从第一批提交开始采用简洁的祈使句或 Conventional Commits，例如 `feat: add bmi088 startup self-check`、`fix: correct spi1 buffer size`。Pull Request 需要说明改动目的、受影响板级资源、是否改动 `.ioc`/生成代码、验证方式与结果；涉及引脚映射、时钟或外设行为变更时，请附上关键日志、配置片段或截图。

## Configuration & Safety Tips
不要提交 `build/**`、临时缓存或工具生成的中间文件。修改 `Modules/modules.yaml` 或 `User/*.yaml` 时，确保模块名、硬件别名与 `FindOrExit(...)` 中的查找键保持一致。升级 `LibXR` 前先同步子模块，并单独审查第三方代码变更。

对于通过 `Modules/modules.yaml` 以 `xrobot-org/` 命名空间引入的 XRobot 官方模块，默认按 vendor / 第三方代码处理：

- 禁止为了当前机器人业务逻辑直接修改这些官方模块源码。
- 优先在 `App/` 层、配置层或本地适配模块中实现机器人特定行为。
- 若确实发现官方模块存在通用能力缺口或可上游的 bug，必须先明确说明理由，再以“最小、通用、可回馈上游”的方式修改。
- 若只是为了临时 bring-up、协议兼容或单机型特判，禁止落到官方模块源码中。

## XRobot-Specific Notes
当前模板已完成 LibXR 对 FreeRTOS 的系统层接入：`cmake/LibXR.CMake` 已设置 `LIBXR_SYSTEM FreeRTOS`，`Core/Src/freertos.c` 的 `StartDefaultTask()` 已调用 `app_main()`，`User/app_main.cpp` 已初始化 `STM32TimerTimebase` 并执行 `PlatformInit(...)`。新增业务功能前通常不需要先修改 `Middlewares/Third_Party/LibXR/system/FreeRTOS/`；只有在 `FreeRTOSConfig.h` 的 tick、内存分配策略或线程/同步语义与 LibXR 假设不一致时，才应回头调整这一层。

`SharedTopicClient` 与 `SharedTopic` 是一对基于 UART 的 Topic 同步桥：前者订阅并打包本地 Topic 后发往串口，后者从串口接收并解析 Topic 数据，再分发回本地 Topic。两者都已作为模块来源列在 `Modules/modules.yaml` 中；当前 `AppRuntime` 已实例化 `SharedTopicClient` 用于转发 `robot_mode/chassis_state/gimbal_state/shoot_state`，`SharedTopic` 服务端仍不是当前主链路的一部分。

如果要接入自定义模块仓库，请把自建 `index.yaml` 追加到 `Modules/sources.yaml` 的 `sources:` 列表中，再按 `namespace/ModuleName` 的形式把模块写入 `Modules/modules.yaml`。使用 `xrobot_create_mod` 创建模块时，常用参数包括 `--desc`、`--hw`、`--constructor`、`--template` 与 `--depends`；其中 `--depends` 用于声明模块级依赖关系，不等同于硬件依赖，也不会替代代码中的 `#include` 和实例化顺序。

XRobot 的上层架构不必局限于当前的 `BSP -> Module` 组织方式，完全可以扩展为 `BSP(LibXR) -> Module -> APP`。但当前 CMake 仅自动收录 `User/*.cpp`，因此如果新增 `App/` 或 `APP/` 目录，必须同步修改 `cmake/LibXR.CMake`，为新目录补充 include 路径和 `target_sources(...)`，否则文件夹存在但源文件不会参与编译。

## Current Migration Status
当前仓库已经不再是“只有 `User/xrobot_main.hpp` 直接实例化几个模块”的模板状态，而是进入了 `App/` 编排层落地阶段。请把下面这些事实当成后续工作的当前基线：

- 当前唯一运行链已经冻结为：`FreeRTOS defaultTask -> app_main() -> XRobotMain(peripherals) -> ApplicationManager::MonitorAll()`。
- 当前已明确禁止再引入第二套 `RobotInit/RobotTask/OSTaskInit`、第二个无限主循环或第二套总调度中心。
- 当前已正式接入 `App/` 目录，`App/CMakeLists.txt` 通过 `cmake/LibXR.CMake` 挂入现有单目标 `basic_framework`；禁止再把长期业务代码塞回 `User/`。
- 当前 `App/` 层真实架构是 Controller 体系，不是旧的 `Roles` 目录体系；当前已落地的 App 控制器为 `InputController`、`DecisionController`、`GimbalController`、`ChassisController`、`ShootController`。
- 当前 `App/Topics` 已实际创建 4 个公共 Topic：`robot_mode`、`chassis_state`、`gimbal_state`、`shoot_state`。
- 当前 `MotionCommand`、`AimCommand`、`FireCommand` 是 `AppRuntime` 内部共享命令对象，不是当前已经落地的 `LibXR::Topic`；后续只有在跨控制器长期稳定交换并明确需要发布/订阅语义时，才升级为公共 Topic。
- 当前第一批“骨架冻结批”已经完成：`App/Robot`、`App/Cmd`、`App/Chassis`、`App/Gimbal`、`App/Shoot`、`App/Topics`、`App/Config` 已存在，并能通过 `cmake --build --preset Debug`。
- 当前命令闭环基线为：`InputController -> DecisionController -> GimbalController / ShootController / ChassisController`，由 `AppRuntime` 共享命令对象承接意图，不再按旧 `Role` 命名描述。
- 当前没有 `TelemetryRole` 已落地事实，也没有 `system_health` 公共 Topic 已落地事实；在线监测、失联回调和健康汇总仍属于后续 `referee/master_machine/can_comm` 等链路推进时需要重新设计的 App 层能力。
- 当前真实 `remote` 第一轮接入已经完成：当前 DJI DBUS 遥控协议模块已收口为 `Modules/RemoteControl/DT7.hpp`，`InputController` 不再使用板载 `KEY`，而是异步订阅模块内部的强类型 Topic `remote_control_state`，并将其映射到 `OperatorInputSnapshot`。
- 当前真实 `imu` 第一轮接入已经完成：`User/app_main.cpp` 已补齐 `BMI088` 所需的 `spi_bmi088 / bmi088_accl_cs / bmi088_gyro_cs / bmi088_gyro_int / pwm_bmi088_heat / ramfs / database`，`AppRuntime` 已实例化 `BMI088`，`GimbalController` 和 `ChassisController` 已开始消费 `bmi088_gyro / bmi088_accl`。
- 当前曾尝试以 `GM6020GimbalMotor` 与 `MecanumChassisDrive` 这类伪模块直接承载云台/底盘控制策略，但这已被确认为错误迁移路线：它们把本应属于 App 控制器的业务编排、模式判断、运动学/控制策略下沉进了 `Modules/`，并已在本轮 `DJIMotor` 归一化完成后正式删除。
- 当前关于 `motor` 的正确迁移路线已经修正为：**优先完整移植 donor 的 `DJIMotor` 通用能力到 `Modules/`，再由 `GimbalController / ChassisController / ShootController` 在 `App/` 层完成具体编排。**
- 当前真实链路的后续正确顺序已修正为：`remote -> imu -> DJIMotor module -> gimbal/shoot/chassis controller orchestration -> referee/master_machine/can_comm`。
- 当前 `cmake --build --preset Debug` 已再次验证通过；但 `Modules/BMI088/BMI088.hpp` 仍存在既有 warning：`GetGyroLSB()` 和 `GetAcclLSB()` 有 “control reaches end of non-void function”，后续如触达该文件应顺手修复。
- 当前关于 `VT13` 串口设置的处理策略补充如下：
  - 若当前系统**没有出现运行错误、启动断言、数据接收异常或回调缺失**，则默认**不要主动处理** `VT13` 所接串口（当前为 `USART2`）的配置细节。
  - 尤其不要因为 `.ioc`、UART Mode、DMA/NVIC 或 CubeMX 生成细节“看起来不够理想”就单独发起一次与业务主线无关的收口工作。
  - 只有当问题表现为 `VT13` 真正无法运行、导致启动失败、收不到数据、频繁离线或出现明确运行时错误时，才重新把 `VT13` 串口设置拉回排查主线。
  - 换言之：对 `VT13` 串口设置采取“**无运行错误则忽略**”策略，优先保障主迁移链路继续向前推进。

## Current C++ Architecture Constraints
在继续把占位镜像逻辑替换成真实执行器/传感器前，必须遵守以下统一约束。后续代码如与这些约束冲突，应优先改方案，不要硬写实现。

### App / Modules / Topics 边界
- `User/` 只负责板级入口、HAL 到 LibXR 的封装、`HardwareContainer` 注入和向 `App` 转交，不承载长期业务编排。
- `App/` 只负责 Controller 化业务编排、模式管理、跨控制器契约和系统级协调。
- `Modules/` 只负责可复用能力封装，如传感器、执行器、桥接器、协议适配器、通用服务，不承载整车级业务决策。
- 对 `Modules/modules.yaml` 中以 `xrobot-org/` 引入的官方模块，默认视为“只读能力提供者”；除非用户显式批准并且修改具备上游通用性，否则不要直接编辑其源码。
- 若一段逻辑同时理解“输入意图 + 多模块状态 + 机器人模式 + 输出行为”，它应放在 `App/`，不应放进 `Modules/`。
- 若一段逻辑未来可能跨机器人、跨角色复用，它优先设计成 `Modules/`。
- 允许模块发布“模块私有强类型 Topic”，例如 `remote_control_state`，但这类 Topic 不应自动升级成 `App/Topics` 公共契约。只有跨控制器长期稳定交换的数据才进入 `App/Topics/*`。
- `MotionCommand`、`AimCommand`、`FireCommand` 当前是 `AppRuntime` 内共享命令对象，不是公共 Topic；不要在文档或实现中把它们误写成已经落地的 `motion_command / aim_command / fire_command` Topic。
- `AppRuntime` 中模块实例化顺序很重要：感知/输入/执行模块必须先于消费它们的 Controller 构造。后续新增模块时，默认遵守 `context_ -> module_ -> controller_` 的顺序。
- 对 `motor` 的特殊补充约束：
  - `Modules/` 中的电机层只允许提供“电机实例 / 反馈解码 / 分组发帧 / 闭环控制接口 / 配置注入”这类通用能力。
  - 禁止在电机模块中直接订阅 `AimCommand`、`MotionCommand`、`FireCommand`、`RobotMode` 来完成具体业务控制。
  - 禁止在电机模块中直接实现“云台 yaw 控制器”“底盘运动学编排器”“发射策略器”这类 APP 级控制器。
  - `GimbalController / ChassisController / ShootController` 才是 `AimCommand / MotionCommand / FireCommand` 的合法消费者；它们通过通用电机模块接口来驱动执行器。
  - 对具备“重定义执行器参考零点”语义的接口（例如 `DMMotor::ZeroPosition()`），默认视为 bring-up / 校准 / 维修辅助能力，而不是常规业务控制接口。
  - 除非用户明确批准并且当前任务目标就是做校准/基准重建流程，否则 `App/` 层应尽量不要直接调用这类接口；优先在人工 bring-up、专用调试流程或明确隔离的校准入口中使用。

### LibXR 风格约束
- 新的长期运行单元优先写成继承 `LibXR::Application` 的类，把周期逻辑收敛到 `OnMonitor()`，不要退回 `init()/task()/while(1)` 的 C 风格。
- 所有硬件依赖都必须通过 `LibXR::HardwareContainer` 查找，禁止在 `App/` 或新 `Modules/` 中直接暴露 `SPI_HandleTypeDef*`、`UART_HandleTypeDef*`、`CAN_HandleTypeDef*` 等 HAL 类型。
- 发布/订阅状态优先使用 `LibXR::Topic`，事件通知优先使用 `LibXR::Event`，日志优先使用 `XR_LOG_*`；不要重新实现 `message_center`、自制消息总线或自制日志系统。
- 使用 `Topic` 时必须显式思考 `multi_publisher`、`cache`、`check_length` 三个开关，尤其不要让 ISR/回调路径去发布 `multi_publisher=true` 的 Topic。
- 注册到 `ApplicationManager`、`Topic` 回调、`Event::Bind`、驱动回调里的对象必须比注册表活得更久，禁止把短生命周期对象地址绑进去。
- 载荷类型优先使用尺寸稳定、轻量、可平铺的数据结构；不要把 owning 容器、协议打包结构体、HAL 句柄或复杂对象直接塞进 Topic。
- 若使用 `LibXR::Topic::ASyncSubscriber<Data>`，构造后应立即 `StartWaiting()`，`OnMonitor()` 中消费后也应立即重新 `StartWaiting()`，保持订阅处于持续接收状态。
- 若使用 `LibXR::Timebase::GetMicroseconds()` 进行控制周期计算，优先使用 `MicrosecondTimestamp::Duration::ToSecondf()`，不要直接把时间戳对象当裸整数做减法。
- 若使用 `LibXR::CAN`，应显式包含 `can.hpp`，不要假设 `libxr.hpp` 一定已经转出该抽象。

### donor 迁移禁令
- 禁止把 `basic_framework` 的 `.c/.h` 模块按目录原样搬进当前仓库，再套一层 C++ 外壳。
- 禁止迁移 `message_center` 的实现；只允许迁移其“话题拆分思想”，并按当前公共契约审慎落到 `App/Topics/*` 与 `LibXR::Topic`。
- 禁止迁移 `daemon` 的旧实现；只允许迁移“在线监测/失联回调/健康汇总”的语义，后续由 App 控制器或专门健康汇总能力重新设计，不得假定 `system_health` 已经落地。
- 禁止把 `remote`、`master_machine`、`referee` 这类外部输入源直接写成顶层业务模块；它们必须先作为 `Module` 适配，再由 `InputController`、`DecisionController` 或后续明确引入的 App 控制器消费。
- 禁止把 donor 的协议帧结构体直接当业务对象使用；协议对象与领域对象必须分层。
- 禁止让 `DecisionController` 或任意单个 App 控制器长成新的“超级模块”或隐式总调度器。
- 禁止把 donor 的 `DJIMotorInit/DJIMotorSetRef/DJIMotorControl` 全套 C 风格对象模型直接照搬。后续真实电机接入应优先写成 LibXR/XRobot 风格的模块，再由 Controller 编排消费。
- 禁止在 Controller 中直接处理 CAN 发帧细节，除非是当前这种“第一条最小真实闭环”的过渡方案，并且必须在注释里明确说明是临时实现。
- 禁止重新引入当前这种“把 APP 控制器伪装成 Module”的过渡产物。此前的 `GM6020GimbalMotor` 与 `MecanumChassisDrive` 已完成历史清理；后续若再次出现让模块直接理解 `AimCommand / MotionCommand / RobotMode` 的设计，应视为越界并直接拒绝。

### 后续真实接入的建议落点
- `remote`：优先作为 `Module + InputController` 组合接入，而不是直接塞进 `App/` 业务壳。
- `imu`：优先作为 `Module + Topic schema` 组合接入，由 `GimbalController/ChassisController` 消费。
- `motor`：优先完整迁移 donor `DJIMotor` 的“通用电机模块能力”到 `Modules/`，由 `ChassisController/GimbalController/ShootController` 编排使用；不要继续把具体云台/底盘控制器写进 `Modules/`。
- `referee`：优先作为 `Module + Topic schema + App Controller` 组合接入，是否需要独立健康/遥测控制器应以后续真实数据流为准。
- `master_machine`、`can_comm`：优先作为桥接/通信 `Module` 接入，不直接承担业务决策。
- 当前后续实现优先级已经修正为：`DJIMotor module normalization -> gimbal/shoot/chassis controller re-binding -> referee/master_machine/can_comm -> phase 5 cleanup`。原因是电机层分层已经确认走偏，必须先修正通用执行器抽象，再继续上层业务推进。

## Board Debugging Lessons
以下内容基于 2026-04-21 这轮“板上无法读取 BMI088 陀螺仪值”的实机调试结果，总结为后续同类问题的默认排查顺序与已知陷阱。

### 默认排查顺序
- 若现象是“某个传感器/模块无数据”，不要先假设是该模块本身故障，必须先确认启动链是否真的已经走到该模块构造、初始化和运行线程。
- 推荐优先验证 4 个断点：`StartDefaultTask`、`app_main`、`App::AppRuntime::AppRuntime`、目标模块的 `Init/ThreadFunc`。若前级断点未命中，后级外设问题都暂不成立。
- 若系统已经进入 `IdleTask`，不要直接得出“程序正常空转”结论；应继续读取 `defaultTaskHandle`、`pxCurrentTCB`、`uxCurrentNumberOfTasks` 和 `defaultTask` 保存栈，判断是任务退出、阻塞还是上下文损坏。
- 若 `HardFault` 的异常栈帧明显异常，或 `PSP` 区域充满 `0xA5A5A5A5`，优先怀疑任务栈踩踏或上下文损坏，而不是普通外设访问错误。
- 若 `pvPortMalloc` 内出现异常值，不要立刻判断为“单纯堆不够”；应先区分是“剩余堆耗尽”还是“堆元数据已被前面代码踩坏”。

### 本轮已确认的 3 个真实阻塞点
- 第一个阻塞点不是 BMI088，而是 `USART3` 的 HAL 配置与 LibXR 封装假设不一致：`huart3.Init.Mode` 为 `UART_MODE_TX_RX`，但只配置了 RX DMA，导致 `LibXR::STM32UART` 在构造时因 `hdmatx == NULL` 触发致命断言。
- 第二个阻塞点是 FreeRTOS 总堆偏小。当前工程在启动早期存在大量 `pvPortMalloc/new`，包括 `HardwareContainer` alias 注册、`ApplicationManager` 注册、`Topic::Domain/Topic` 构造、`Mutex/Queue` 创建等。`configTOTAL_HEAP_SIZE` 过小时会显著放大启动失败概率。
- 第三个阻塞点是 `defaultTask` 栈不足。即便 `app_main()` 已在 `StartDefaultTask()` 中运行，`app_main` 内的大量局部对象、硬件封装对象和大型 `HardwareContainer` 初始化列表仍可能导致瞬时栈踩踏，从而进一步破坏堆或任务上下文。

### 对当前模板的具体经验
- 当前模板里 `app_main()` 本来就在 `Core/Src/freertos.c` 的 `StartDefaultTask()` 中执行，后续遇到类似问题时不要重复尝试“把 `app_main` 移到 DefaultTask”这种已完成动作。
- 若未来继续在 `app_main()` 中增加局部硬件对象或更大的初始化列表，默认应重新评估 `defaultTask` 栈水位，而不是假设 4KB/8KB 永远足够。
- 对 LibXR 的 `STM32UART` 封装，凡是 HAL `Mode` 开启了 TX，就必须确保 `hdmatx` 已正确链接；若某 UART 实际只用来接收，则 HAL `Mode` 应与之保持一致，避免“配置看似无害、构造即致命断言”的问题。
- 对 “进入 `AppRuntime` 但目标模块仍无数据” 的情况，优先抓取模块线程断点与应用层消费点，例如 `BMI088::ThreadFunc` 和 `GimbalController::OnMonitor`，确认数据是否真正发布并被消费。
- 对 BMI088 这一路，只有在 `AppRuntime`、`BMI088::BMI088`、`BMI088::Init`、`BMI088::ThreadFunc` 都已命中之后，才有资格继续怀疑 SPI 时序、中断映射、寄存器配置或芯片本体。

### 后续板上调试建议
- 若再次出现“上板跑不起来但无明显日志”，优先保留并利用 J-Link + GDB 的阶段性脚本，不要每次从头手工单步。
- 推荐把“启动链断点脚本”“HardFault 栈帧抓取脚本”“defaultTask 保存栈抓取脚本”“GimbalController 采样抓取脚本”作为标准调试资产长期保留。
- 后续若再扩展 `AppRuntime` 或 `User/app_main.cpp`，一旦出现早期异常，应优先重复本节流程，而不是直接修改目标外设驱动。
