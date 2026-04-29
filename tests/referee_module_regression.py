from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
REFEREE_CONFIG = ROOT / "App" / "Config" / "RefereeConfig.hpp"
REFEREE_MODULE = ROOT / "Modules" / "Referee" / "Referee.hpp"
REFEREE_CONSTRAINT_VIEW = ROOT / "App" / "Cmd" / "RefereeConstraintView.hpp"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def slice_between(source: str, start: str, end: str) -> str:
    start_index = source.find(start)
    require(start_index >= 0, f"未找到片段起点: {start}")
    end_index = source.find(end, start_index)
    require(end_index >= 0, f"未找到片段终点: {end}")
    return source[start_index:end_index]


def test_referee_config_defaults_to_enabled() -> None:
    source = read(REFEREE_CONFIG)

    require(
        "bool enabled = true;" in source,
        "RefereeConfig.enabled 默认值必须为 true，避免首批 referee 链路被静默关闭",
    )
    require(
        "inline constexpr RefereeConfig kRefereeConfig{};" in source,
        "RefereeConfig 需要继续保留默认实例，供 AppRuntime 直接注入 referee 模块",
    )


def test_referee_state_and_raw_cache_keep_high_value_donor_fields() -> None:
    source = read(REFEREE_MODULE)
    referee_state_block = slice_between(source, "struct RefereeState {", "class Referee")
    raw_state_cache_block = slice_between(
        source, "struct RawStateCache {", "};\n\n  static constexpr const char* kStateTopicName"
    )

    for token in (
        "cooling_rate",
        "shooter_cooling_value",
        "remaining_heat",
        "robot_hp",
        "buffer_energy",
        "hurt_armor_id",
        "hurt_type",
        "robot_id",
        "robot_level",
        "remaining_energy_bits",
    ):
        require(token in referee_state_block, f"RefereeState 缺少 donor 高价值状态字段: {token}")

    for token in (
        "has_robot_hp",
        "has_buff",
        "has_hurt",
        "has_shoot_data",
        "chassis_output_enabled",
        "shooter_output_enabled",
        "cooling_buff",
        "robot_hp_slots",
        "remaining_energy_bits",
    ):
        require(token in raw_state_cache_block, f"RawStateCache 缺少 donor 高价值缓存字段: {token}")


def test_referee_parser_handles_first_batch_required_cmd_ids() -> None:
    source = read(REFEREE_MODULE)

    for cmd_id in (
        "0x0001",
        "0x0002",
        "0x0101",
        "0x0201",
        "0x0202",
        "0x0203",
        "0x0205",
        "0x0207",
    ):
        require(
            f"case {cmd_id}:" in source,
            f"Referee.hpp 需要显式处理首批冻结 cmd_id {cmd_id}",
        )


def test_student_interactive_stays_out_of_core_referee_state() -> None:
    source = read(REFEREE_MODULE)
    referee_state_block = slice_between(source, "struct RefereeState {", "class Referee")
    raw_state_cache_block = slice_between(
        source, "struct RawStateCache {", "};\n\n  static constexpr const char* kStateTopicName"
    )

    require(
        "case 0x0301:" not in source or "student_interactive" not in referee_state_block,
        "0x0301 student_interactive 即使接入也不能混进 RefereeState 核心状态",
    )
    require(
        "student_interactive" not in referee_state_block
        and "interactive" not in referee_state_block,
        "RefereeState 必须保持比赛/功率/热量/射击等核心状态边界，不承载 student_interactive",
    )
    require(
        "student_interactive" not in raw_state_cache_block,
        "RawStateCache 首批也不应把 student_interactive 混成核心缓存",
    )


def test_referee_constraint_view_remains_app_folded_constraints() -> None:
    source = read(REFEREE_CONSTRAINT_VIEW)

    require(
        "struct RefereeConstraintView" in source
        and "constexpr RefereeConstraintView BuildRefereeConstraintView" in source,
        "RefereeConstraintView 必须继续作为 App 层由 RefereeState 折叠出的约束视图",
    )
    for token in (
        "referee_online",
        "match_gate_active",
        "fire_gate_active",
        "power_gate_active",
        "motion_scale",
        "max_fire_rate_hz",
    ):
        require(token in source, f"RefereeConstraintView 缺少控制器消费所需的约束字段: {token}")

    for forbidden_pattern in (
        r"student_interactive",
        r"\bwidget\b",
        r"\bsprite\b",
        r"\bglyph\b",
        r"\bcanvas\b",
        r"\bGraph_Data_t\b",
        r"\bString_Data_t\b",
    ):
        require(
            re.search(forbidden_pattern, source) is None,
            f"RefereeConstraintView 不应退化为 UI 图元缓存，检测到越界模式: {forbidden_pattern}",
        )


def main() -> None:
    test_referee_config_defaults_to_enabled()
    test_referee_state_and_raw_cache_keep_high_value_donor_fields()
    test_referee_parser_handles_first_batch_required_cmd_ids()
    test_student_interactive_stays_out_of_core_referee_state()
    test_referee_constraint_view_remains_app_folded_constraints()


if __name__ == "__main__":
    main()
