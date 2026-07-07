#!/usr/bin/env python3
from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ISA_SCHEMA_PATH = ROOT / "spec/holon_npu_isa.json"

REQUIRED_CLASS_FIELDS = {
    "name",
    "value",
    "mask",
    "format",
    "coverage",
    "fault",
    "semantics",
    "description",
}
REQUIRED_RESERVED_FIELDS = {"name", "value", "mask", "description"}


def as_int(value: int | str) -> int:
    if isinstance(value, int):
        return value
    value = value.replace("_", "")
    if value.startswith(("0x", "0X")):
        return int(value, 16)
    return int(value, 10)


def load_schema(path: Path = ISA_SCHEMA_PATH) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        schema = json.load(f)
    if schema.get("schema_version") != 1:
        raise ValueError("unsupported ISA schema_version")
    return schema


def patterns_overlap(a: dict[str, Any], b: dict[str, Any]) -> bool:
    a_value = as_int(a["value"])
    b_value = as_int(b["value"])
    common_mask = as_int(a["mask"]) & as_int(b["mask"])
    return (a_value & common_mask) == (b_value & common_mask)


def check_schema(schema: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    isa = schema.get("isa", {})
    if isa.get("instruction_bits") != 32:
        failures.append("isa.instruction_bits must be 32 for V2")
    if isa.get("alignment_bytes") != 4:
        failures.append("isa.alignment_bytes must be 4 for fixed 32-bit instructions")

    classes = schema.get("instruction_classes", [])
    reserved = schema.get("reserved_classes", [])
    if not classes:
        failures.append("instruction_classes must not be empty")
    if not reserved:
        failures.append("reserved_classes must not be empty")

    seen_names: set[str] = set()
    seen_coverage: set[str] = set()
    all_patterns = classes + reserved

    for entry in classes:
        missing = REQUIRED_CLASS_FIELDS - entry.keys()
        if missing:
            failures.append(f"{entry.get('name', '<unnamed>')}: missing fields {sorted(missing)}")
        name = entry.get("name")
        if name in seen_names:
            failures.append(f"duplicate instruction class name {name}")
        seen_names.add(name)
        coverage = entry.get("coverage")
        if coverage in seen_coverage:
            failures.append(f"duplicate coverage point {coverage}")
        seen_coverage.add(coverage)
        if as_int(entry.get("value", 0)) & ~as_int(entry.get("mask", 0)):
            failures.append(f"{name}: value sets bits outside mask")

    for entry in reserved:
        missing = REQUIRED_RESERVED_FIELDS - entry.keys()
        if missing:
            failures.append(f"{entry.get('name', '<unnamed>')}: missing fields {sorted(missing)}")
        name = entry.get("name")
        if name in seen_names:
            failures.append(f"duplicate reserved/class name {name}")
        seen_names.add(name)
        if as_int(entry.get("value", 0)) & ~as_int(entry.get("mask", 0)):
            failures.append(f"{name}: value sets bits outside mask")

    for index, left in enumerate(all_patterns):
        for right in all_patterns[index + 1 :]:
            if patterns_overlap(left, right):
                failures.append(f"encoding overlap: {left['name']} and {right['name']}")

    expected_classes = {
        "FRONTEND_CONTROL",
        "PREDICATE",
        "VECTOR_CONFIG",
        "VECTOR_ALU",
        "VECTOR_MEMORY",
        "VECTOR_PERMUTE",
        "VECTOR_REDUCTION",
        "QUANTIZATION",
        "MATRIX",
        "DMA",
        "CSR_DEBUG",
        "SYNC",
        "SYSTEM",
    }
    missing_classes = expected_classes - {entry["name"] for entry in classes}
    if missing_classes:
        failures.append(f"missing required V2 classes {sorted(missing_classes)}")

    return failures


def main() -> int:
    try:
        schema = load_schema()
        failures = check_schema(schema)
    except Exception as exc:  # noqa: BLE001 - command-line checker
        print(f"ISA metadata check failed: {exc}", file=sys.stderr)
        return 1

    if failures:
        print("ISA metadata check failed:", file=sys.stderr)
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1

    print("ISA metadata check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
