# basic_framework 到 XRobot 的迁移执行蓝图

## 1. 文档目的

本文档用于收口以下 6 个已完成的规划任务：

1. 运行时骨架定界
2. `App/` 目录与构建接入设计
3. 角色与 Topic 契约定义
4. donor 旧能力迁移映射
5. 骨架最小闭环验收定义
6. 实施批次与执行顺序冻结

本文档的目标不是讨论所有可能方案，而是冻结当前版本的迁移基线，作为后续实现阶段的唯一输入。

## 2. 已冻结的全局前提

### 2.1 唯一运行链

当前与后续都必须保持唯一运行链：

```text
FreeRTOS defaultTask
-> app_main()
-> XRobotMain(peripherals)
-> ApplicationManager::MonitorAll()
```

明确禁止：

- 新增第二套 `RobotInit/RobotTask/OSTaskInit`
- 新增第二个无限主循环
- 新增第二套总调度中心

### 2.2 单向依赖方向

后续实现只允许沿以下方向依赖：

```text
User
-> App/Runtime
-> App/Roles + App/Topics
-> Modules
-> LibXR
```

明确禁止：

- `Modules -> User`
- `App -> app_main`
- `App` 直接持有 HAL 句柄
- `User/` 长期承载业务编排

### 2.3 当前构建硬约束

当前工程只有一个可执行目标 `basic_framework`，并且 `cmake/LibXR.CMake` 只自动收录 `User/*.cpp`。

这意味着：

- `App/` 目录现在不会自动参与编译
- `App/` 必须显式接入现有目标
- 不能通过新增第二个目标或旁路入口来“曲线接入”

## 3. 运行时骨架冻结结果

### 3.1 目标层级图

```text
CubeMX / STM32Cube 生成层
└─ FreeRTOS defaultTask
   └─ User/app_main.cpp
      ├─ PlatformInit / Timebase / Power
      ├─ HAL -> LibXR 驱动对象封装
      ├─ HardwareContainer 命名与注入
      └─ User/xrobot_main.hpp
         └─ AppRuntime 装配入口
            ├─ App/Runtime
            ├─ App/Roles
            ├─ App/Topics
            ├─ App/Config
            └─ Modules/
```

### 3.2 边界冻结

`User/app_main.cpp` 只做：

- 平台初始化
- 硬件对象封装
- `HardwareContainer` 注入
- 转交给 `XRobotMain(...)`

`User/xrobot_main.hpp` 只做：

- 薄装配
- 运行时转交

不做：

- 长期业务状态机
- 复杂 Topic 路由
- 新的总控中心

## 4. App 目录与构建接入冻结结果

### 4.1 建议目录树

```text
App/
  CMakeLists.txt
  Runtime/
    AppRuntime.hpp
    AppRuntime.cpp
    RuntimeContext.hpp
  Roles/
    InputRole.hpp
    DecisionRole.hpp
    ChassisRole.hpp
    GimbalRole.hpp
    ShootRole.hpp
    TelemetryRole.hpp
  Topics/
    RobotMode.hpp
    MotionCommand.hpp
    AimCommand.hpp
    FireCommand.hpp
    ChassisState.hpp
    GimbalState.hpp
    ShootState.hpp
    SystemHealth.hpp
  Config/
    AppConfig.hpp
  Integration/
    XRobotAssembly.hpp
```

### 4.2 构建接入策略

推荐方案：

1. 在 `cmake/LibXR.CMake` 中新增 `XROBOT_APP_DIR`
2. 使用 `add_subdirectory(App)` 显式接入
3. `App/CMakeLists.txt` 不创建新目标
4. 通过 `target_sources(${CMAKE_PROJECT_NAME} ...)` 与 `target_include_directories(...)` 回挂到现有目标

明确禁止：

- 继续扩大 `User/*.cpp` 的 GLOB 作为长期方案
- 把 `App/` 伪装成 `Modules/` 条目
- 把 `App/` 代码重新塞回 `User/`

## 5. 角色与 Topic 契约冻结结果

### 5.1 第一版角色集合

第一版固定为 6 个角色：

- `InputRole`
- `DecisionRole`
- `ChassisRole`
- `GimbalRole`
- `ShootRole`
- `TelemetryRole`

### 5.2 第一版 Topic 白名单

第一版固定为 8 个 Topic：

- `robot_mode`
- `motion_command`
- `aim_command`
- `fire_command`
- `chassis_state`
- `gimbal_state`
- `shoot_state`
- `system_health`

### 5.3 Topic 发布/订阅关系

| Topic | 发布者 | 订阅者 |
| --- | --- | --- |
| `robot_mode` | `DecisionRole` | `ChassisRole` `GimbalRole` `ShootRole` `TelemetryRole` |
| `motion_command` | `DecisionRole` | `ChassisRole` `TelemetryRole` |
| `aim_command` | `DecisionRole` | `GimbalRole` `TelemetryRole` |
| `fire_command` | `DecisionRole` | `ShootRole` `TelemetryRole` |
| `chassis_state` | `ChassisRole` | `DecisionRole` `TelemetryRole` |
| `gimbal_state` | `GimbalRole` | `DecisionRole` `TelemetryRole` |
| `shoot_state` | `ShootRole` | `DecisionRole` `TelemetryRole` |
| `system_health` | 各角色按需上报 | `DecisionRole` `TelemetryRole` |

### 5.4 命名规范

- Topic 名：小写蛇形，如 `motion_command`
- 角色名：`PascalCase + Role`
- 字段名：小写蛇形并带单位后缀，如 `vx_mps`、`yaw_deg`
- 命令 Topic：`*_command`
- 状态 Topic：`*_state`

### 5.5 当前禁止进入 Topic schema 的内容

- HAL 句柄
- CAN ID / UART 号 / DMA 缓冲区
- PID、前馈、限幅器内部状态
- 原始编码器值
- 原始 IMU 大块数据
- 遥控器原始摇杆/键鼠扫描值
- 原始视觉协议帧
- 调试字符串与一次性校准参数

## 6. donor 能力迁移映射冻结结果

### 6.1 第一批要迁的能力

按优先级排序：

1. `robot_cmd`
2. `remote`
3. `gimbal`
4. `shoot`
5. `chassis`

迁移原则：

- 先迁业务职责与语义
- 不先迁底层驱动与协议实现
- 不把 donor 的应用层硬编码初始化整体平移

### 6.2 不迁移或后置迁移

明确不迁移：

- `message_center` 旧实现
- `RobotInit / RobotTask / OSTaskInit`
- 旧条件编译式板拓扑组织方式

后置迁移：

- `daemon`
- `can_comm`
- `master_machine`
- `referee`
- `imu`
- `motor`

### 6.3 映射原则

| 旧能力 | 新归宿 |
| --- | --- |
| `robot_cmd` | `InputRole + DecisionRole` |
| `chassis` | `ChassisRole` |
| `gimbal` | `GimbalRole` |
| `shoot` | `ShootRole` |
| `message_center` | `LibXR::Topic/Event` |
| `daemon` | `App/Runtime + system_health` |
| `remote` | 输入适配模块 + `InputRole` |
| `can_comm` | 后续桥接模块 |
| `referee` | 后续裁判系统适配 + `TelemetryRole` |
| `imu` | 后续传感器模块 |
| `motor` | 后续执行器模块 |
| `master_machine` | 后续视觉/上位机适配 |

## 7. 骨架最小闭环验收标准

### 7.1 Definition of Done

骨架阶段完成，至少满足：

1. 运行链唯一且未分叉
2. `App/` 已进入正式构建输入
3. 6 个角色在 `App/` 中有稳定骨架定义
4. 8 个 Topic 白名单集中定义
5. 依赖方向仍保持单向
6. 空角色场景可以通过构建
7. `XRobotMain(...)` 仍是薄装配层
8. `HardwareContainer` 仍是业务侧唯一硬件入口

### 7.2 验收检查维度

- 结构
- 构建
- 依赖
- 契约
- 运行时

### 7.3 建议验证命令

```powershell
git submodule update --init --recursive
cmake --preset Debug
cmake --build --preset Debug
cmake --build --preset Debug --verbose
rg --line-number "XRobotMain|MonitorAll|app_main" User Core cmake
rg --line-number "InputRole|DecisionRole|ChassisRole|GimbalRole|ShootRole|TelemetryRole" App User
rg --line-number "robot_mode|motion_command|aim_command|fire_command|chassis_state|gimbal_state|shoot_state|system_health" App User
rg --line-number "HandleTypeDef" App
```

### 7.4 典型不通过信号

- `App/` 目录存在但没进编译
- 新增第二入口或第二主循环
- `User/` 又开始承载业务编排
- `App/` 出现 HAL 句柄
- 角色名或 Topic 名漂移
- 空角色都无法构建

## 8. 实施波次冻结结果

### 8.1 三个实施波次

#### 第一批：骨架冻结批

目标：

- 冻结唯一运行链
- 冻结 `App/` 单目标接入约束
- 冻结角色集合
- 冻结 Topic 白名单
- 冻结职责边界和不迁移项

退出条件：

- 所有边界都无歧义
- 后续任务可在此基础上并行实现

#### 第二批：命令闭环批

目标：

- 先落 `robot_cmd`
- 接 `remote`
- 接 `gimbal`
- 接 `shoot`

形成：

```text
InputRole -> DecisionRole -> GimbalRole / ShootRole
```

退出条件：

- `robot_mode`
- `aim_command`
- `fire_command`

已形成稳定发布/消费闭环。

#### 第三批：执行与遥测闭环批

目标：

- 接入 `chassis`
- 接入 `TelemetryRole`
- 聚合 `system_health`

形成第一版全闭环。

退出条件：

- 6 个角色全部接入唯一运行链
- 8 个 Topic 都有明确生产/消费归属
- 满足骨架 DoD 与验收标准

### 8.2 执行顺序图

```text
统一前提冻结
-> 第一批：骨架与边界冻结
-> 第二批-A：remote 输入适配
-> 第二批-B：robot_cmd / DecisionRole 命令仲裁
-> 第二批-C：GimbalRole 接 aim_command
-> 第二批-D：ShootRole 接 fire_command
-> 第三批-A：ChassisRole 接 motion_command
-> 第三批-B：TelemetryRole 聚合状态与健康信息
-> 第一版全集成验收
```

### 8.3 并行与串行规则

必须串行：

- 第一批必须先完成
- `DecisionRole` 命令语义冻结先于各执行角色最终接线
- `TelemetryRole` 最终联调必须晚于主要状态源稳定

可以并行：

- `remote` 与 `DecisionRole` 内部编排
- `GimbalRole` 与 `ShootRole`
- 第三批里的 `ChassisRole` 接入与 `TelemetryRole` 聚合框架搭建

## 9. 每批失败点与兜底策略

### 第一批

最容易失败：

- 边界冻结不彻底
- 反复重开第二入口、旧调度、Topic 扩容讨论

兜底：

- 第一批产出作为第一版唯一基线
- 新诉求一律后置，不回流改边界

### 第二批

最容易失败：

- `remote`、`robot_cmd`、`DecisionRole` 职责缠绕

兜底：

- 严格拆成输入解释、命令仲裁、执行消费
- 先冻结最小命令字段和状态机

### 第三批

最容易失败：

- 状态 Topic 口径不一致
- `TelemetryRole` 演变成隐式总调度

兜底：

- 状态 Topic 只表达状态
- `TelemetryRole` 只做汇聚、转发、健康呈现
- 不一致时先收敛最小公共字段

## 10. 下一步真正该做什么

后续实现阶段应严格按下面顺序推进：

1. 建 `App/` 目录与 `App/CMakeLists.txt`
2. 在 `cmake/LibXR.CMake` 中显式接入 `App/`
3. 建空的 `AppRuntime`、6 个角色骨架、8 个 Topic 骨架
4. 先跑通骨架最小闭环构建
5. 再开始第二批命令闭环
6. 最后再接 `chassis` 与 `TelemetryRole`

如果后续要继续拆实现任务，应基于本文档继续按角色边界拆卡，而不要再按 donor 原目录直接平移。*** End Patch
