from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CAN_BRIDGE = ROOT / "Modules" / "CANBridge" / "CANBridge.hpp"
BRIDGE_TOPIC = ROOT / "App" / "Topics" / "ChassisBoardBridge.hpp"
APP_RUNTIME = ROOT / "App" / "Robot" / "AppRuntime.cpp"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_can_bridge_supports_can_comm_sized_fragmented_payloads() -> None:
    source = read(CAN_BRIDGE)

    require(
        "kMaxPayloadBytes = 60u" in source,
        "CANBridge 需要支持 donor can_comm 等价的 60 字节 payload 上限",
    )
    require(
        "PayloadTransferState" in source,
        "CANBridge 需要显式维护多帧 payload 接收状态",
    )
    require(
        "AppendPayloadRxFrame" in source,
        "CANBridge RX 需要把连续 CAN 报文重组为完整 payload",
    )
    require(
        "PublishPayload" in source and "payload_topic_" in source,
        "CANBridge 收到完整 payload 后应发布模块私有 payload Topic",
    )
    require(
        "len > CANBridgeProtocol::kMaxPayloadBytes" in source,
        "SendPayload 需要按 60 字节上限拒绝过长业务数据",
    )


def test_can_bridge_payload_topic_keeps_business_out_of_module() -> None:
    source = read(CAN_BRIDGE)

    for forbidden in (
        "MotionCommand",
        "RobotMode",
        "Chassis_Ctrl_Cmd_s",
        "Chassis_Upload_Data_s",
        "CHASSIS_FOLLOW_GIMBAL_YAW",
    ):
        require(
            forbidden not in source,
            f"CANBridge 不应理解 App 或 donor 业务类型: {forbidden}",
        )

    require(
        'kPayloadTopicName = "can_bridge_payload"' in source,
        "CANBridge 应把通用 payload 作为模块私有 Topic 暴露给 App 适配层",
    )


def test_chassis_board_bridge_contract_uses_explicit_width_fields() -> None:
    require(
        BRIDGE_TOPIC.exists(),
        "需要新增 App/Topics/ChassisBoardBridge.hpp 承接旧双板底盘数据语义",
    )
    source = read(BRIDGE_TOPIC)

    for token in (
        "struct ChassisBoardCommand",
        "struct ChassisBoardFeedback",
        "std::uint8_t mode",
        "std::uint16_t chassis_speed_buffer",
        "float vx_mps",
        "float vy_mps",
        "float wz_radps",
        "float gimbal_relative_yaw_deg",
        "std::uint16_t remaining_heat",
        "float bullet_speed_mps",
        "std::uint8_t enemy_color",
        "BuildChassisBoardCommand",
        "BuildChassisBoardFeedback",
    ):
        require(token in source, f"双板底盘桥契约缺少字段或映射函数: {token}")

    require(
        "Chassis_Ctrl_Cmd_s" not in source and "Chassis_Upload_Data_s" not in source,
        "当前契约不能直接复用 donor packed struct 名称或 ABI",
    )


def test_runtime_keeps_existing_shared_topic_mirror() -> None:
    source = read(APP_RUNTIME)

    for topic_name in (
        "AppConfig::kRobotModeTopicName",
        "AppConfig::kChassisStateTopicName",
        "AppConfig::kGimbalStateTopicName",
        "AppConfig::kShootStateTopicName",
        "AppConfig::kInsStateTopicName",
    ):
        require(topic_name in source, f"SharedTopicClient 既有镜像 Topic 不应被移除: {topic_name}")


def main() -> None:
    test_can_bridge_supports_can_comm_sized_fragmented_payloads()
    test_can_bridge_payload_topic_keeps_business_out_of_module()
    test_chassis_board_bridge_contract_uses_explicit_width_fields()
    test_runtime_keeps_existing_shared_topic_mirror()


if __name__ == "__main__":
    main()
