from __future__ import annotations

import json
import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "docs" / "schema" / "winmtr-report-v1.json"
GOLDEN_PATH = ROOT / "tests" / "golden" / "winmtr-report-v1.json"
TIMESTAMP = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def exact_keys(value: dict[str, object], required: set[str], allowed: set[str], name: str) -> None:
    keys = set(value)
    require(required <= keys, f"{name} is missing {sorted(required - keys)}")
    require(keys <= allowed, f"{name} has undocumented fields {sorted(keys - allowed)}")


def nonnegative_integer(value: object, name: str) -> None:
    require(type(value) is int and value >= 0, f"{name} must be a non-negative integer")


def finite_number(value: object, name: str) -> None:
    require(type(value) in (int, float), f"{name} must be numeric")
    require(math.isfinite(float(value)) and float(value) >= 0, f"{name} must be finite and non-negative")


def main() -> None:
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    report = json.loads(GOLDEN_PATH.read_text(encoding="utf-8"))
    required_root = set(schema["required"])
    required_hop = set(schema["$defs"]["hop"]["required"])
    required_responder = set(schema["$defs"]["responder"]["required"])
    allowed_root = set(schema["properties"])
    allowed_hop = set(schema["$defs"]["hop"]["properties"])
    allowed_responder = set(schema["$defs"]["responder"]["properties"])
    outcomes = set(schema["$defs"]["hop"]["properties"]["last_outcome"]["enum"])

    exact_keys(report, required_root, allowed_root, "report")
    require(report["schema_version"] == schema["properties"]["schema_version"]["const"],
            "schema version mismatch")
    nonnegative_integer(report["session_id"], "session_id")
    nonnegative_integer(report["duration_ms"], "duration_ms")
    require(isinstance(report["target"], str), "target must be a string")
    require(bool(TIMESTAMP.fullmatch(report["started_at_utc"])), "invalid start timestamp")
    require(report["ended_at_utc"] is None or bool(TIMESTAMP.fullmatch(report["ended_at_utc"])),
            "invalid end timestamp")
    require(report["statistics"] == {
        "loss": "timed_out/completed",
        "stddev": "sample_standard_deviation",
        "jitter": "ewma_absolute_consecutive_delta_alpha_1_16",
    }, "statistics definitions changed without a schema version change")

    require(isinstance(report["hops"], list), "hops must be an array")
    nullable_metrics = {
        "best_ms", "average_ms", "worst_ms", "last_ms", "jitter_ms",
        "recent_jitter_ms", "stddev_ms",
    }
    counter_fields = {
        "sent", "completed", "received", "timed_out", "in_flight", "local_errors",
        "scheduler_skipped", "cache_skipped", "late_completions", "cancelled",
        "post_destination_completions", "scheduler_late_slots",
        "scheduler_lateness_total_ms", "scheduler_lateness_max_ms", "last_error_code",
    }
    text_fields = {
        "host", "ip", "country", "asn", "isp", "metadata_source",
        "metadata_failure_reason",
    }
    responder_text_fields = {
        "id", "host", "ip", "country", "asn", "isp", "metadata_source",
        "metadata_failure_reason",
    }

    for index, hop in enumerate(report["hops"]):
        name = f"hop[{index}]"
        require(isinstance(hop, dict), f"{name} must be an object")
        exact_keys(hop, required_hop, allowed_hop, name)
        nonnegative_integer(hop["hop"], f"{name}.hop")
        require(hop["hop"] >= 1, f"{name}.hop must start at one")
        for field in counter_fields:
            nonnegative_integer(hop[field], f"{name}.{field}")
        for field in text_fields:
            require(isinstance(hop[field], str), f"{name}.{field} must be a string")
        finite_number(hop["loss_percent"], f"{name}.loss_percent")
        require(hop["loss_percent"] <= 100, f"{name}.loss_percent exceeds 100")
        expected_loss = 0 if hop["completed"] == 0 else 100 * hop["timed_out"] / hop["completed"]
        require(abs(hop["loss_percent"] - expected_loss) < 0.01, f"{name} loss formula mismatch")
        require(hop["last_outcome"] in outcomes, f"{name} has an unknown outcome")
        for field in nullable_metrics:
            if hop["received"] == 0:
                require(hop[field] is None, f"{name}.{field} must be null without replies")
            else:
                finite_number(hop[field], f"{name}.{field}")
        require(isinstance(hop["responders"], list), f"{name}.responders must be an array")
        for responder_index, responder in enumerate(hop["responders"]):
            responder_name = f"{name}.responders[{responder_index}]"
            require(isinstance(responder, dict), f"{responder_name} must be an object")
            exact_keys(responder, required_responder, allowed_responder, responder_name)
            for field in responder_text_fields:
                require(isinstance(responder[field], str), f"{responder_name}.{field} must be a string")
            require(bool(re.fullmatch(r"[0-9a-f]{16}", responder["id"])),
                    f"{responder_name}.id is not a stable 64-bit hex ID")
            nonnegative_integer(responder["hit_count"], f"{responder_name}.hit_count")
            nonnegative_integer(responder["last_seen_sequence"],
                                f"{responder_name}.last_seen_sequence")

    require("𠀀" in report["target"] and "🌐" in report["target"],
            "golden sample lost non-BMP Unicode coverage")
    print("Report schema and golden sample validation passed.")


if __name__ == "__main__":
    main()
