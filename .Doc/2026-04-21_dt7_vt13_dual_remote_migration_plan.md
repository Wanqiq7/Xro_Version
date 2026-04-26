# DT7 / VT13 双遥控协议迁移计划

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不打散当前 `InputRole -> DecisionRole -> Roles` 控制链的前提下，把现有 DJI DBUS 遥控模块重命名为 `DT7.hpp`，并在同目录下新增 `VT13.hpp`，形成 `DT7State / VT13State` 两套并存的 typed private input module。

**Architecture:** 本次迁移只处理“协议适配层 + 人工输入入口”，不扩展为整条控制链重构。`DT7` 与 `VT13` 都作为独立协议模块存在，各自产出私有 typed topic；`InputRole` 后续只决定如何消费这些状态，而不要求它们先共用一个状态结构。

**Tech Stack:** STM32CubeMX, FreeRTOS, LibXR, XRobot, UART-based protocol parsing, CRC16 verification, header-only embedded C++

---

## Scope

本计划只覆盖：

- `RemoteControl.hpp -> DT7.hpp` 重命名与兼容收口
- `VT13` 协议模块移植
- `DT7State / VT13State` 并存设计
- `InputRole` 的最小接入预留
- 板级 UART / alias / 验证清单规划

本计划不覆盖：

- `DecisionRole` / `MotionCommand` / `AimCommand` / `FireCommand` 重构
- `OperatorInputSnapshot` 大规模扩展
- automation / host assist / vision lane 重构
- 多遥控器混控策略升级为字段级授权或求和

## Recommended Order

1. 先完成 `RemoteControl.hpp -> DT7.hpp` 的重命名和兼容引用收口。
2. 再单独移植 `VT13.hpp`，让它作为独立协议模块可编译存在。
3. 然后在 `InputRole` 层决定第一版接入方式，但不强迫 `DT7State / VT13State` 合并。
4. 最后处理板级 alias、验证命令和文档清理。

原因：

- 先把“旧模块身份”从 `RemoteControl` 明确为 `DT7`，可以避免后续 `VT13` 接入时语义混乱。
- `VT13State` 与 `DT7State` 分开后，`InputRole` 的接入策略就可以围绕“两个协议模块并存”设计，而不是先被一个错误的统一状态结构绑死。

## File Map

### Existing files to modify

- `Modules/RemoteControl/RemoteControl.hpp`
- `App/Roles/InputRole.hpp`
- `App/Roles/InputRole.cpp`
- `AGENTS.md`
- `findings.md`
- `progress.md`
- `task_plan.md`

### New files to create

- `Modules/RemoteControl/DT7.hpp`
- `Modules/RemoteControl/VT13.hpp`

### Files that may need follow-up changes later

- `User/app_main.cpp`
- `App/Runtime/AppRuntime.cpp`
- `App/Runtime/AppRuntime.hpp`

---

## Task 1: Rename the Existing DJI DBUS Module to `DT7.hpp`

**Files:**
- Create: `Modules/RemoteControl/DT7.hpp`
- Modify: `App/Roles/InputRole.hpp`
- Modify: `App/Roles/InputRole.cpp`
- Reference: `Modules/RemoteControl/RemoteControl.hpp`

- [ ] **Step 1: 复制现有 `RemoteControl` 内容到 `DT7.hpp`**

要求：

- 保持现有协议解析、断联检测、实例级 topic 配置能力不变
- 仅做命名层收口：
  - `RemoteControl` -> `DT7`
  - `RemoteControlState` -> `DT7State`

- [ ] **Step 2: 更新 `InputRole` 引用**

要求：

- 当前 `InputRole` 改为直接依赖 `DT7.hpp`
- 保持现有 primary / secondary 结构不变
- 不扩 `OperatorInputSnapshot`

- [ ] **Step 3: 暂不删除旧 `RemoteControl.hpp`，直到整个批次收口**

原因：

- 便于分阶段验证
- 避免一次性大范围 include breakage

- [ ] **Step 4: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- `DT7.hpp` 替代引用后可通过编译
- 现有主链行为不变

**Acceptance criteria:**

- 项目中对现有 DJI DBUS 遥控模块的语义命名已收口为 `DT7`
- `DT7State` 与未来 `VT13State` 不再处于同名冲突风险

## Task 2: Port `VT13.hpp` as a Separate Protocol Module

**Files:**
- Create: `Modules/RemoteControl/VT13.hpp`
- Reference: `D:\\RoboMaster\\HeroCode\\Code\\rmpp-master\\rmpp-master\\rmpp\\lib\\rc\\VT13.hpp`
- Reference: `D:\\RoboMaster\\HeroCode\\Code\\rmpp-master\\rmpp-master\\rmpp\\lib\\rc\\VT13.cpp`
- Reference: `Modules/MasterMachine/MasterMachine.hpp`

- [ ] **Step 1: 明确 `VT13State` 的最小稳定字段**

建议至少保留：

- `online`
- `x / y / pitch / yaw / wheel`
- `mode`
- `pause / trigger / fn / photo`
- `mouse`
- `key`

- [ ] **Step 2: 迁移协议解析逻辑，但改成当前仓库风格**

要求：

- 使用 LibXR / STM32UART 风格，而不是 rmpp 的 `BSP::UART6::RegisterCallback`
- CRC16 校验改复用当前仓库已有 `utils/crc.hpp`
- 断联检测逻辑与 `DT7` 保持同级职责

- [ ] **Step 3: 保持 `VT13` 为独立 typed private topic 发布者**

要求：

- 不与 `DT7State` 强行统一
- 不在模块内做业务动作解释
- 不在模块内做多遥控器仲裁

- [ ] **Step 4: Debug 构建校验**

Run: `cmake --build --preset Debug`

Expected:

- `VT13.hpp` 可独立编译存在
- 不要求本批次就接上真实 UART

**Acceptance criteria:**

- 当前仓库里已经有与 `DT7` 并存的 `VT13` 协议模块
- `VT13State` 完整保留 VT13 的设备级语义，而不是被压扁成 DT7 语义子集

## Task 3: Freeze the First-pass Consumption Strategy in `InputRole`

**Files:**
- Modify: `App/Roles/InputRole.hpp`
- Modify: `App/Roles/InputRole.cpp`
- Reference: `Modules/RemoteControl/DT7.hpp`
- Reference: `Modules/RemoteControl/VT13.hpp`
- Reference: `App/Runtime/OperatorInputSnapshot.hpp`

- [ ] **Step 1: 明确第一版只消费 `DT7State`**

原因：

- 现有控制链已经围绕 DT7 语义映射
- `VT13State` 比 `DT7State` 丰富得多，不应仓促压平

- [ ] **Step 2: 在 `InputRole` 内为 `VT13` 预留插入位置**

要求：

- 可以先不实例化 `VT13`
- 但结构设计上应允许未来新增：
  - `vt13_primary_`
  - `vt13_secondary_`
  - 对应 subscriber / cache

- [ ] **Step 3: 不扩 `OperatorInputSnapshot`**

要求：

- 当前批次不把 `VT13` 键鼠语义往下游扩散
- 当前批次不修改 `DecisionRole`

- [ ] **Step 4: 明确后续消费策略**

建议分两阶段：

- Phase 1:
  - 让 `VT13` 仅完成模块化存在与基础在线验证
- Phase 2:
  - 再决定 `VT13State -> OperatorInputSnapshot` 的映射规则

**Acceptance criteria:**

- 当前计划不会把 `VT13` 移植直接扩大成整条控制链改造
- `InputRole` 的接入策略已被明确冻结为“先模块并存，后消费映射”

## Task 4: Board-level Alias and Hardware Readiness Plan

**Files:**
- Reference: `User/app_main.cpp`
- Reference: `Core/Src/usart.c`
- Reference: `User/libxr_config.yaml`

- [ ] **Step 1: 确认 `VT13` 实际接入的 UART**

需要回答：

- 使用哪一路 UART
- 当前是否已被占用
- 是否有 RX DMA / 中断资源

- [ ] **Step 2: 板级别名规划**

建议新增一个稳定别名，例如：

- `uart_vt13`

而不是直接复用 `usartX`

- [ ] **Step 3: 明确启用顺序**

建议：

- 先让 `VT13` 模块可编译
- 再在板级注册 `uart_vt13`
- 最后再在 `InputRole` 或测试路径中实例化

**Acceptance criteria:**

- `VT13` 模块移植不会被“当前没有空闲 UART”卡死
- 板级启用与协议模块迁移解耦

## Task 5: Cleanup and Documentation Alignment

**Files:**
- Modify: `AGENTS.md`
- Modify: `findings.md`
- Modify: `progress.md`
- Modify: `task_plan.md`
- Modify: `.Doc/2026-04-21_dt7_vt13_dual_remote_migration_plan.md`

- [ ] **Step 1: 文档从 `RemoteControl` 语义切到 `DT7`**

要求：

- 把“当前 remote 模块”明确记为 `DT7`
- 不再让 `RemoteControl` 继续承担协议名与模块名双重含义

- [ ] **Step 2: 记录 `VT13State` 与 `DT7State` 分开设计的原因**

至少写清：

- 协议差异
- 键鼠语义保留
- 避免错误统一状态结构

- [ ] **Step 3: 记录第一版刻意不做的内容**

至少包括：

- 不做 `VT13 -> OperatorInputSnapshot` 全量映射
- 不做 `DecisionRole` 改造
- 不做整条控制链重构

**Acceptance criteria:**

- 规划、实现、文档三者边界一致
- 后续 agent 不会误把本任务扩展成“输入系统总重构”
