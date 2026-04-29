from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    gimbal_math = ROOT / "App" / "Gimbal" / "GimbalMath.hpp"
    text = gimbal_math.read_text(encoding="utf-8")

    require("RadiansToDegrees" in text, "missing rad-to-deg conversion helper")
    require("DegreesToRadians" in text, "missing deg-to-rad conversion helper")
    require("ComputeRelativeYawDeg" in text, "missing relative yaw helper")
    require("kRadToDeg" in text, "vision wire radians must convert to App degrees")

    gimbal_state = (ROOT / "App" / "Topics" / "GimbalState.hpp").read_text(
        encoding="utf-8"
    )
    require(
        "relative_yaw_deg" in gimbal_state,
        "GimbalState must expose yaw motor relative angle for chassis follow",
    )

    master_machine = (
        ROOT / "Modules" / "MasterMachine" / "MasterMachine.hpp"
    ).read_text(encoding="utf-8")
    require(
        "RadiansToDegrees" in master_machine or "kRadToDeg" in master_machine,
        "MasterMachine RX must convert vision wire radians to App degrees",
    )
    require(
        "DegreesToRadians" in master_machine or "kDegToRad" in master_machine,
        "MasterMachine TX must convert App degrees to vision wire radians",
    )
    require(
        "EncodeTxFlags" in master_machine,
        "MasterMachine TX 必须把颜色、模式、弹速编码进 flags",
    )
    require(
        "struct RxFlagsLayout" in master_machine
        and "kFireModeMask = 0x0003u" in master_machine
        and "kTargetStateMask = 0x000Cu" in master_machine
        and "kTargetTypeMask = 0x00F0u" in master_machine,
        "MasterMachine RX flags bit layout 必须集中冻结为可审查常量",
    )
    require(
        "struct TxFlagsLayout" in master_machine
        and "kEnemyColorMask = 0x0003u" in master_machine
        and "kWorkModeMask = 0x000Cu" in master_machine
        and "kBulletSpeedMask = 0xFF00u" in master_machine,
        "MasterMachine TX flags bit layout 必须集中冻结颜色、模式、弹速字段",
    )
    require(
        "enum class WorkMode" in master_machine
        and "kSmallBuff = 1" in master_machine
        and "kBigBuff = 2" in master_machine,
        "small buff / big buff 需要作为 MasterMachine wire enum 固化",
    )
    require(
        "kVisionRxCommandId = 0x0001" in master_machine
        and "kVisionTxCommandId = 0x0002" in master_machine,
        "MasterMachine 应显式冻结 donor RX/TX command id",
    )
    require(
        "command_id != kVisionRxCommandId" in master_machine,
        "MasterMachine RX 需要校验 command id，避免同链路复用时误解码",
    )
    require(
        "out_state.request_fire_enable = FlagIsSet" in master_machine,
        "AUTO_FIRE 模式不能在 RX 解码层折叠成 request_fire_enable 边沿位",
    )
    require(
        "has_success ? ErrorCode::OK" in master_machine,
        "MasterMachine TX 应向所有启用链路发送，支持 UART/VCP 可并存",
    )

    chassis = (ROOT / "App" / "Chassis" / "ChassisController.cpp").read_text(
        encoding="utf-8"
    )
    require(
        "gimbal_state.relative_yaw_deg" in chassis,
        "Chassis follow must use gimbal relative yaw, not INS yaw",
    )

    gimbal_controller = (
        ROOT / "App" / "Gimbal" / "GimbalController.cpp"
    ).read_text(encoding="utf-8")
    require(
        "ClampPitchCommandDeg" in gimbal_controller,
        "GimbalController 需要在 pitch 输出前增加最小保护限位",
    )


if __name__ == "__main__":
    main()
