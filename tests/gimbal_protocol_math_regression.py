import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def compact(text):
    return re.sub(r"\s+", "", text)


def has_rate_integration(text, target_name, rate_name):
    statement_pattern = re.compile(rf"{target_name}\s*(?:\+=|=[^;]*{target_name}\s*\+)[^;]*;")
    for match in statement_pattern.finditer(text):
        statement = match.group(0)
        if rate_name in statement and "dt_s" in statement:
            return True
    return False


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

    gimbal_controller = (
        ROOT / "App" / "Gimbal" / "GimbalController.cpp"
    ).read_text(encoding="utf-8")
    require(
        "ClampPitchCommandDeg" in gimbal_controller,
        "GimbalController 需要在 pitch 输出前增加最小保护限位",
    )

    aim_command = (ROOT / "App" / "Topics" / "AimCommand.hpp").read_text(
        encoding="utf-8"
    )
    require(
        "yaw_rate_degps" in aim_command and "pitch_rate_degps" in aim_command,
        "AimCommand 必须区分绝对角度目标与手动角速度输入",
    )

    operator_input = (
        ROOT / "App" / "Cmd" / "OperatorInputSnapshot.hpp"
    ).read_text(encoding="utf-8")
    require(
        "yaw_delta_deg" not in operator_input
        and "pitch_delta_deg" not in operator_input,
        "OperatorInputSnapshot 不能继续暴露 yaw_delta_deg / pitch_delta_deg 误导命名",
    )
    require(
        "yaw_rate_degps" in operator_input
        and "pitch_rate_degps" in operator_input,
        "OperatorInputSnapshot 必须暴露 yaw_rate_degps / pitch_rate_degps 角速度输入",
    )

    input_config = (ROOT / "App" / "Config" / "InputConfig.hpp").read_text(
        encoding="utf-8"
    )
    require(
        "kMaxYawDeltaDeg" not in input_config
        and "kMaxPitchDeltaDeg" not in input_config,
        "InputConfig 不能继续使用 kMaxYawDeltaDeg / kMaxPitchDeltaDeg 误导命名",
    )
    require(
        "kMaxYawRateDegps" in input_config
        and "kMaxPitchRateDegps" in input_config,
        "InputConfig 必须提供 kMaxYawRateDegps / kMaxPitchRateDegps 角速度上限",
    )

    remote_input_mapper = (
        ROOT / "App" / "Cmd" / "RemoteInputMapper.hpp"
    ).read_text(encoding="utf-8")
    require(
        "kMaxYawDeltaDeg" not in remote_input_mapper
        and "kMaxPitchDeltaDeg" not in remote_input_mapper
        and "snapshot.yaw_delta_deg" not in remote_input_mapper
        and "snapshot.pitch_delta_deg" not in remote_input_mapper,
        "RemoteInputMapper 不能继续把遥控杆输入写入 delta 角度语义",
    )
    require(
        re.search(
            r"snapshot\.yaw_rate_degps\s*=[^;]*InputConfig::kMaxYawRateDegps",
            remote_input_mapper,
        )
        is not None,
        "RemoteInputMapper 必须使用 kMaxYawRateDegps 生成 snapshot.yaw_rate_degps",
    )
    require(
        re.search(
            r"snapshot\.pitch_rate_degps\s*=[^;]*InputConfig::kMaxPitchRateDegps",
            remote_input_mapper,
        )
        is not None,
        "RemoteInputMapper 必须使用 kMaxPitchRateDegps 生成 snapshot.pitch_rate_degps",
    )

    decision_controller = (
        ROOT / "App" / "Cmd" / "DecisionController.cpp"
    ).read_text(encoding="utf-8")
    decision_compact = compact(decision_controller)
    require(
        "input.target_yaw_deg+input.yaw_delta_deg" not in decision_compact,
        "DecisionController 不能把 yaw_delta_deg 当作绝对 yaw 目标增量一次性相加",
    )
    require(
        "input.target_pitch_deg+input.pitch_delta_deg" not in decision_compact,
        "DecisionController 不能把 pitch_delta_deg 当作绝对 pitch 目标增量一次性相加",
    )
    require(
        re.search(
            r"aim_command\.yaw_rate_degps\s*=\s*input\.yaw_rate_degps",
            decision_controller,
        )
        is not None,
        "DecisionController 必须把 input.yaw_rate_degps 传递为 aim_command.yaw_rate_degps",
    )
    require(
        re.search(
            r"aim_command\.pitch_rate_degps\s*=\s*input\.pitch_rate_degps",
            decision_controller,
        )
        is not None,
        "DecisionController 必须把 input.pitch_rate_degps 传递为 aim_command.pitch_rate_degps",
    )

    gimbal_controller_header = (
        ROOT / "App" / "Gimbal" / "GimbalController.hpp"
    ).read_text(encoding="utf-8")
    require(
        "manual_yaw_target_deg_" in gimbal_controller_header
        and "manual_pitch_target_deg_" in gimbal_controller_header,
        "GimbalController 必须持有手动 yaw/pitch 目标状态，而不是每周期使用瞬时小角度目标",
    )
    require(
        re.search(r"manual\w*initialized\w*_", gimbal_controller_header)
        is not None,
        "GimbalController 必须持有手动目标初始化标志，用于进入手动时从 INS 对齐",
    )
    require(
        "dt_s" in gimbal_controller
        and (
            "Timebase::GetMicroseconds" in gimbal_controller
            or "last_control_time" in gimbal_controller
        ),
        "GimbalController 必须计算控制周期 dt_s，用于积分手动角速度",
    )
    require(
        has_rate_integration(
            gimbal_controller, "manual_yaw_target_deg_", "yaw_rate_degps"
        ),
        "GimbalController 必须用 yaw_rate_degps * dt_s 积分 manual_yaw_target_deg_",
    )
    require(
        has_rate_integration(
            gimbal_controller, "manual_pitch_target_deg_", "pitch_rate_degps"
        ),
        "GimbalController 必须用 pitch_rate_degps * dt_s 积分 manual_pitch_target_deg_",
    )
    require(
        "manual_yaw_target_deg_ = ins_state_.yaw_total_deg" in gimbal_controller,
        "GimbalController 进入手动控制时应以当前 INS total yaw 初始化手动目标",
    )
    gimbal_config = (ROOT / "App" / "Config" / "GimbalConfig.hpp").read_text(
        encoding="utf-8"
    )
    require(
        ".cycle = false" in gimbal_config,
        "Yaw DJI 电机角度闭环使用 degree 量纲，不能启用 LibXR PID 的 2π cycle",
    )
    require(
        re.search(
            r"manual_pitch_target_deg_\s*=\s*ClampPitchCommandDeg\(\s*ins_state_\.pitch_deg\s*\)",
            gimbal_controller,
        )
        is not None,
        "GimbalController 进入手动控制时应以 clamp 后的当前 INS pitch 初始化手动目标",
    )

    chassis = (ROOT / "App" / "Chassis" / "ChassisController.cpp").read_text(
        encoding="utf-8"
    )
    chassis_math = (ROOT / "App" / "Chassis" / "ChassisControlMath.hpp").read_text(
        encoding="utf-8"
    )
    require(
        "SelectFollowGimbalYawErrorDeg(gimbal_state)" in chassis
        and "return gimbal_state.relative_yaw_deg" in chassis_math,
        "Chassis follow must use gimbal relative yaw, not INS yaw",
    )


if __name__ == "__main__":
    main()
