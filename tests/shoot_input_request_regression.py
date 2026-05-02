from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_fire_command_carries_discrete_shot_request_sequence() -> None:
    operator_input = read("App/Cmd/OperatorInputSnapshot.hpp")
    fire_command = read("App/Topics/FireCommand.hpp")
    decision = read("App/Cmd/DecisionController.cpp")

    require(
        "shot_request_seq" in operator_input,
        "OperatorInputSnapshot 需要携带离散射击请求序号",
    )
    require(
        "shot_request_seq" in fire_command,
        "FireCommand 需要把离散射击请求序号传给 ShootController",
    )
    require(
        "fire_command.fire_enable ? input.shot_request_seq : 0U" in decision,
        "裁判或安全门禁火时不能把离散射击请求序号交给 ShootController 排队",
    )


def test_fire_command_carries_referee_fire_gate_override() -> None:
    operator_input = read("App/Cmd/OperatorInputSnapshot.hpp")
    fire_command = read("App/Topics/FireCommand.hpp")
    decision = read("App/Cmd/DecisionController.cpp")
    shoot_controller = read("App/Shoot/ShootController.cpp")

    require(
        "bool ignore_referee_fire_gate = false;" in operator_input,
        "OperatorInputSnapshot 需要默认关闭裁判发射门控豁免",
    )
    require(
        "bool ignore_referee_fire_gate = false;" in fire_command,
        "FireCommand 需要默认关闭裁判发射门控豁免",
    )
    require(
        "fire_command.ignore_referee_fire_gate = input.ignore_referee_fire_gate;"
        in decision,
        "DecisionController 需要把输入侧裁判发射门控豁免传递给 FireCommand",
    )
    require(
        "constraints.referee_allows_fire || input.ignore_referee_fire_gate"
        in decision,
        "DecisionController 的发射门控应允许人工遥控豁免裁判发射许可",
    )
    require(
        "constraints.referee_online && !input.ignore_referee_fire_gate"
        in decision,
        "裁判在线限频不应裁剪已明确豁免的人工遥控强制拨弹",
    )
    require(
        "fire_command.fire_enable = input.fire_enabled && input.friction_enabled &&\n"
        "                             fire_gate_open;"
        in decision,
        "DecisionController 的 fire_enable 应基于放行后的 fire_gate_open",
    )
    require(
        "fire_command_.referee_allows_fire || fire_command_.ignore_referee_fire_gate"
        in shoot_controller,
        "ShootController 最终门控应保留 FireCommand 中的裁判门控豁免",
    )
    require(
        "fire_command_.fire_enable && robot_mode_.fire_enable &&\n"
        "      fire_gate_open"
        in shoot_controller,
        "ShootController 的 fire_enabled 不应再次只依赖真实裁判许可",
    )
    require(
        "state_fire_command.referee_allows_fire = referee_constraints.referee_allows_fire;"
        in shoot_controller,
        "ShootController 发布状态估算时仍应记录真实裁判许可",
    )


def test_remote_mapper_generates_shot_request_on_trigger_rising_edge() -> None:
    mapper = read("App/Cmd/RemoteInputMapper.hpp")

    require(
        "fire_pressed" in mapper,
        "InputLatchState 需要记录上一周期发射触发状态",
    )
    require(
        "shot_request_seq" in mapper and "++latch.shot_request_seq" in mapper,
        "输入映射需要在鼠标左键或 VT13 trigger 上升沿递增 shot_request_seq",
    )
    require(
        "UpdateFireTriggerEdge" in mapper,
        "发射触发边沿检测应封装成清晰的 helper",
    )


def test_shoot_controller_queues_single_like_modes_by_request_sequence() -> None:
    shoot_controller_hpp = read("App/Shoot/ShootController.hpp")
    shoot_controller_cpp = read("App/Shoot/ShootController.cpp")

    require(
        "last_shot_request_seq_" in shoot_controller_hpp,
        "ShootController 需要记住上一条离散射击请求序号",
    )
    require(
        "ConsumeShotRequest" in shoot_controller_hpp
        and "ConsumeShotRequest" in shoot_controller_cpp,
        "ShootController 应通过 ConsumeShotRequest 消费离散请求",
    )
    require(
        "fire_command_.shot_request_seq != last_shot_request_seq_" in shoot_controller_cpp,
        "单发/三连发需要按请求序号变化排队，而不是按模式电平反复或只触发一次",
    )
    require(
        "fire_command_.shot_request_seq == 0U" in shoot_controller_cpp,
        "ShootController 需要把 0 序号视为无有效离散射击请求",
    )
    require(
        "QueueSingleShots(ResolveRequestedSingleLikeShotCount" in shoot_controller_cpp,
        "序号变化后应根据 Single/Burst 模式排队 1 发或 3 发",
    )


def test_master_machine_host_override_maps_fire_semantics() -> None:
    input_controller_hpp = read("App/Cmd/InputController.hpp")
    input_controller_cpp = read("App/Cmd/InputController.cpp")

    require(
        "master_machine_fire_request_latched_" in input_controller_hpp,
        "InputController 需要记住上一周期 host request_fire_enable 电平",
    )
    require(
        "master_machine_shot_request_seq_" in input_controller_hpp,
        "InputController 需要维护 host 单发请求序号",
    )
    require(
        "LoaderModeType::kContinuous" in input_controller_cpp
        and "auto_fire_requested" in input_controller_cpp,
        "AUTO_FIRE 必须映射为连续射",
    )
    require(
        "fire_request_rising_edge" in input_controller_cpp
        and "LoaderModeType::kSingle" in input_controller_cpp
        and "++master_machine_shot_request_seq_" in input_controller_cpp,
        "request_fire_enable 上升沿必须映射为单发并递增 shot_request_seq",
    )
    require(
        "else if (fire_edge_requested)" in input_controller_cpp,
        "request_fire_enable 高电平期间应保持 kSingle，避免单发状态机被下一周期清空",
    )
    require(
        "const bool fire_field_allowed" in input_controller_cpp
        and "fire_field_allowed &&" in input_controller_cpp,
        "host fire 相关语义必须统一受 kFireEnable 白名单门控",
    )


def main() -> None:
    test_fire_command_carries_discrete_shot_request_sequence()
    test_fire_command_carries_referee_fire_gate_override()
    test_remote_mapper_generates_shot_request_on_trigger_rising_edge()
    test_shoot_controller_queues_single_like_modes_by_request_sequence()
    test_master_machine_host_override_maps_fire_semantics()


if __name__ == "__main__":
    main()
