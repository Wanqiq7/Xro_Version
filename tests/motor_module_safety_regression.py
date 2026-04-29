from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DJI_HEADER = ROOT / "Modules" / "DJIMotor" / "DJIMotor.hpp"
DM_HEADER = ROOT / "Modules" / "DMMotor" / "DMMotor.hpp"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_dji_motor_has_donor_aligned_safety_and_mapping() -> None:
    source = read(DJI_HEADER)

    require(
        "speed_filter_alpha" in source and "current_filter_alpha" in source,
        "DJIMotorConfig 需要提供速度/电流一阶滤波配置",
    )
    require(
        "MakeDJIMotorIdConfig" in source,
        "DJIMotor 需要提供 DJI 类型+ID 到反馈帧/控制帧/槽位的映射辅助函数",
    )
    require(
        "EnterSafeStop()" in source,
        "DJIMotor 超时或未初始化时需要进入统一安全停机路径",
    )
    require(
        "if (!state_.feedback.initialized || !state_.feedback.online)" in source,
        "DJIMotor Enable 后未收到有效反馈前不应闭环输出",
    )


def test_dm_motor_offline_clears_mit_output() -> None:
    source = read(DM_HEADER)

    require(
        "EnterSafeStop()" in source,
        "DMMotor 需要统一安全停机路径",
    )
    require(
        "if (!feedback_.online)" in source and "EnterSafeStop();" in source,
        "DMMotor 离线时需要清除旧 MIT 命令并停止继续输出",
    )


def main() -> None:
    test_dji_motor_has_donor_aligned_safety_and_mapping()
    test_dm_motor_offline_clears_mit_output()


if __name__ == "__main__":
    main()
