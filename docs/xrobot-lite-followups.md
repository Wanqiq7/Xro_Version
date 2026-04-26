# XRobot-Lite 后续待办清单

## 1. 仍未处理的已知问题

### 1.1 BMI088 warning

文件：

- [BMI088.hpp](/F:/Xrobot/Xro_template/Modules/BMI088/BMI088.hpp)

问题：

- `GetGyroLSB()`
- `GetAcclLSB()`

仍有编译 warning：

- `control reaches end of non-void function`

建议：

- 作为一个独立的小修复批次处理
- 不要和主架构改动混在一起

### 1.2 `AppConfig.hpp` 中仍保留旧命令 Topic 名称常量

文件：

- [AppConfig.hpp](/F:/Xrobot/Xro_template/App/Config/AppConfig.hpp)

现状：

- `kMotionCommandTopicName`
- `kAimCommandTopicName`
- `kFireCommandTopicName`

这些常量目前已经不再对应公共 Topic，但类型仍然存在。

建议：

- 若未来不会恢复公共命令 Topic，可在后续清理批次删除或改名为“内部状态键”
- 若希望保留兼容迁移语义，则可暂时保留，但应明确注明“非公共 Topic”

## 2. 包装层未来是否继续保留

文件夹：

- [Roles](/F:/Xrobot/Xro_template/App/Roles)

现状：

- 6 个 `Role` 已经只剩调度包装职责
- 真正业务逻辑都在 `RobotController` 和 3 个 subsystem controller 内

当前保留原因：

- 依赖 `ApplicationManager` 的注册顺序语义
- 可以避免 controller 自注册导致双执行或执行顺序漂移

后续可选方向：

### 方向 A：继续保留包装层

优点：

- 运行时顺序清晰
- 风险低
- 当前实现已经稳定

缺点：

- 目录里仍有一层包装

### 方向 B：删除包装层，让 controller 自注册

前提：

- 必须重新设计和验证 `ApplicationManager` 的执行顺序
- 最好给 `AppRuntime` 增加显式的调度顺序测试或替代调度模型

建议：

- 当前不建议立刻删除包装层
- 若删除，应作为单独批次处理，并带板上验证

## 3. 调度顺序应补充自动化验证

当前事实：

- `ApplicationManager` 使用 `LockFreeList` 头插注册
- 当前通过 `AppRuntime` 成员声明顺序的逆序来实现目标执行顺序

风险：

- 这是“结构性约定”，不是显式单元测试保护

建议新增：

- 一个 compile-only 或 host-side 顺序约束测试
- 至少明确断言 `AppRuntime` 中包装层声明顺序

目标：

- 防止未来调整成员顺序时无意改变实际调度顺序

## 4. 是否需要把 `Roles/` 改名为 `Stages/` 或 `Entrypoints/`

现状：

- `Roles/` 名字还带有历史含义
- 现在它们其实更像“注册入口包装层”

可选优化：

- 改名为 `Stages/`
- 改名为 `Entrypoints/`
- 保持 `Roles/` 不动，但在文档中明确当前语义

建议：

- 当前先不改名
- 因为这会带来一轮纯重命名噪音
- 如果未来继续整理文档和目录，可单独做命名批次

## 5. 是否需要把 `ControllerBase` 和包装层进一步解耦

现状：

- 包装层也继承 `ControllerBase`
- 这样已经统一了基类和注册行为

潜在问题：

- 名字上“ControllerBase” 同时服务于真实 controller 和包装层

建议：

- 当前可接受
- 如果未来要进一步提纯概念，可以考虑：
  - `RegisteredStageBase`
  - `ControllerBase`

但这属于命名优化，不属于功能风险

## 6. 是否继续压缩 `Topics/`

当前公共 Topic 已经收缩到 5 个：

- `robot_mode`
- `chassis_state`
- `gimbal_state`
- `shoot_state`
- `system_health`

建议：

- 暂时不要再继续压缩
- 这 5 个已经是当前对外桥接与系统闭环的最小稳定集合

## 7. 是否补充架构层文档入口

建议：

- 在 [README.md](/F:/Xrobot/Xro_template/README.md) 增加一小节，指向：
  - [xrobot-lite-architecture.md](/F:/Xrobot/Xro_template/docs/xrobot-lite-architecture.md)
  - [xrobot-lite-followups.md](/F:/Xrobot/Xro_template/docs/xrobot-lite-followups.md)

这样后续新会话或新成员进入仓库时，不需要重新口头解释当前架构状态。

## 8. 当前建议的后续优先级

按优先级建议如下：

1. 修复 BMI088 warning
2. 给调度顺序补一条自动化约束
3. 视需要决定是否保留 `Roles/` 包装层长期存在
4. 再考虑命名优化（如 `Roles/` 是否改名）
5. 最后才考虑进一步压缩 Topic 或继续美化目录

## 9. 当前不建议做的事

- 不建议现在删除 `Roles/` 包装层
- 不建议现在改 `ApplicationManager` 调度模型
- 不建议现在再削减公共 Topic
- 不建议把业务重新下沉回 `Modules/`
- 不建议把 `App/` 重新压回 donor 式单体大文件
