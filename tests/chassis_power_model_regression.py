from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODEL_HEADER = ROOT / "App" / "Chassis" / "ChassisMotorPowerModel.hpp"
LIMITER_HEADER = ROOT / "App" / "Chassis" / "ChassisPowerLimiter.hpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_chassis_motor_power_model_exists_and_matches_donor_terms() -> None:
    require(
        MODEL_HEADER.exists(),
        "需要新增 App/Chassis/ChassisMotorPowerModel.hpp 承载 donor PowerControl 纯模型",
    )
    source = read(MODEL_HEADER)

    for token in (
        "ChassisMotorPowerModel",
        "EstimateMotorPowerW",
        "SolveCurrentCommandForPower",
        "AllocateChassisMotorPower",
        "0.0003662109375f",
        "187.0f / 3591.0f / 9.55f",
        "1.23e-07f",
        "1.453e-07f",
        "4.081f",
    ):
        require(token in source, f"功率模型缺少 donor 等价项: {token}")


def test_chassis_power_model_defends_invalid_inputs() -> None:
    source = read(MODEL_HEADER)

    require(
        "std::isfinite" in source,
        "功率模型需要过滤 NaN/Inf，避免异常反馈污染功率分配",
    )
    require(
        "discriminant" in source and "0.0f" in source,
        "反解电流时需要对二次方程判别式做保护",
    )
    require(
        "positive_power" in source,
        "donor 只把正机械功率计入预算，模型需要保留该语义",
    )


def test_power_limiter_exposes_motor_power_diagnostics_without_taking_over_can() -> None:
    limiter = read(LIMITER_HEADER)

    require(
        "ChassisPowerAllocation" in limiter,
        "ChassisPowerLimiter 输出需要暴露 donor 风格电机功率分配诊断",
    )
    require(
        "motor_power_limited" in limiter,
        "ChassisPowerLimiter 需要标记电机功率分配是否触发限功率",
    )
    require(
        "allocated_current_cmd_raw" in limiter,
        "诊断应包含分配后的电流命令，但本阶段不直接接管 DJIMotor CAN 输出",
    )


def main() -> None:
    test_chassis_motor_power_model_exists_and_matches_donor_terms()
    test_chassis_power_model_defends_invalid_inputs()
    test_power_limiter_exposes_motor_power_diagnostics_without_taking_over_can()


if __name__ == "__main__":
    main()
