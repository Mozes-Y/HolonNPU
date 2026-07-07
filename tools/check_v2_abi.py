#!/usr/bin/env python3
from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
V2_ABI_SCHEMA_PATH = ROOT / "spec/holon_npu_v2_abi.json"

CTYPE_BYTES = {
    "uint8_t": 1,
    "uint16_t": 2,
    "uint32_t": 4,
    "uint64_t": 8,
}

REQUIRED_DESCRIPTOR_FIELDS = {
    "size_bytes",
    "version",
    "program_format",
    "holon_isa_major",
    "holon_isa_minor",
    "required_caps",
    "required_op_classes",
    "code_addr",
    "code_size_bytes",
    "entry_pc",
    "arg_addr",
    "arg_size_bytes",
    "local_mem_bytes",
    "program_mem_bytes",
    "stack_bytes",
    "completion_addr",
    "flags",
}


def as_int(value: int | str) -> int:
    if isinstance(value, int):
        return value
    value = value.replace("_", "")
    if value.startswith(("0x", "0X")):
        return int(value, 16)
    return int(value, 10)


def load_schema(path: Path = V2_ABI_SCHEMA_PATH) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        schema = json.load(f)
    if schema.get("schema_version") != 1:
        raise ValueError("unsupported V2 ABI schema_version")
    return schema


def is_power_of_two(value: int) -> bool:
    return value > 0 and (value & (value - 1)) == 0


def check_named_values(
    failures: list[str],
    group_name: str,
    entries: list[dict[str, Any]],
    *,
    require_power_of_two: bool,
) -> None:
    names: set[str] = set()
    values: set[int] = set()
    for entry in entries:
        name = entry.get("name")
        if not name:
            failures.append(f"{group_name}: entry missing name")
        elif name in names:
            failures.append(f"{group_name}: duplicate name {name}")
        names.add(name)

        value = as_int(entry.get("value", 0))
        if value in values:
            failures.append(f"{group_name}: duplicate value 0x{value:X}")
        values.add(value)
        if require_power_of_two and not is_power_of_two(value):
            failures.append(f"{group_name}.{name}: value must be a single capability bit")


def check_descriptor(schema: dict[str, Any], failures: list[str]) -> None:
    constants = schema.get("constants", {})
    descriptor = schema.get("descriptor", {})
    fields = descriptor.get("fields", [])
    size = as_int(constants.get("program_desc_size", 0))

    if size != 128:
        failures.append("constants.program_desc_size must be 128 for V2 ABI 3.0")
    if as_int(constants.get("program_desc_align", 0)) != 16:
        failures.append("constants.program_desc_align must be 16")

    ranges: list[tuple[int, int, str]] = []
    names: set[str] = set()
    for field in fields:
        name = field.get("name", "")
        ctype = field.get("ctype", "")
        if name in names:
            failures.append(f"descriptor: duplicate field {name}")
        names.add(name)
        if ctype not in CTYPE_BYTES:
            failures.append(f"descriptor.{name}: unsupported ctype {ctype}")
            continue
        width = as_int(field.get("width", 0))
        if width != CTYPE_BYTES[ctype] * 8:
            failures.append(f"descriptor.{name}: width does not match {ctype}")
        offset = as_int(field.get("offset", 0))
        if offset % CTYPE_BYTES[ctype] != 0:
            failures.append(f"descriptor.{name}: offset is not naturally aligned")
        end = offset + CTYPE_BYTES[ctype]
        if end > size:
            failures.append(f"descriptor.{name}: field extends beyond descriptor size")
        ranges.append((offset, end, name))

    for index, (left_start, left_end, left_name) in enumerate(ranges):
        for right_start, right_end, right_name in ranges[index + 1 :]:
            if left_start < right_end and right_start < left_end:
                failures.append(f"descriptor overlap: {left_name} and {right_name}")

    if ranges:
        last_end = max(end for _, end, _ in ranges)
        if last_end != size:
            failures.append(f"descriptor fields end at {last_end}, expected {size}")

    missing = REQUIRED_DESCRIPTOR_FIELDS - names
    if missing:
        failures.append(f"descriptor missing required fields {sorted(missing)}")


def check_registers(schema: dict[str, Any], failures: list[str]) -> None:
    registers = schema.get("registers", [])
    names: set[str] = set()
    offsets: set[int] = set()
    for reg in registers:
        name = reg.get("name")
        if name in names:
            failures.append(f"registers: duplicate name {name}")
        names.add(name)
        offset = as_int(reg.get("offset", 0))
        if offset % 4 != 0:
            failures.append(f"registers.{name}: offset must be 32-bit aligned")
        if offset in offsets:
            failures.append(f"registers: duplicate offset 0x{offset:03X}")
        offsets.add(offset)
        if reg.get("access") not in {"RO", "WO", "RW"}:
            failures.append(f"registers.{name}: invalid access {reg.get('access')}")

    required = {
        "DEVICE_ID",
        "ABI_VERSION",
        "ISA_VERSION",
        "CAP0_LO",
        "CAP0_HI",
        "OP_CLASS_LO",
        "OP_CLASS_HI",
        "CONTROL",
        "STATUS",
        "FAULT_CODE",
        "DEBUG_PC",
        "PROGRAM_DESC_ADDR_LO",
        "PROGRAM_DESC_ADDR_HI",
        "DOORBELL",
        "IRQ_ENABLE",
        "IRQ_STATUS",
        "IRQ_CLEAR",
    }
    missing = required - names
    if missing:
        failures.append(f"registers missing required entries {sorted(missing)}")


def check_schema(schema: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    abi = schema.get("abi", {})
    constants = schema.get("constants", {})

    if abi.get("major") != 3 or abi.get("minor") != 0:
        failures.append("abi major/minor must be 3.0")
    if as_int(abi.get("version_reset", 0)) != 0x00030000:
        failures.append("abi.version_reset must be 0x00030000")
    if as_int(constants.get("program_image_align", 0)) != 4:
        failures.append("program image alignment must be 4 bytes")
    if as_int(constants.get("argument_align", 0)) != 16:
        failures.append("argument alignment must be 16 bytes")
    if as_int(constants.get("completion_align", 0)) != 16:
        failures.append("completion alignment must be 16 bytes")

    flags = schema.get("flags", [])
    check_named_values(failures, "flags", flags, require_power_of_two=True)
    valid_mask = as_int(schema.get("flags_valid_mask", 0))
    computed_mask = 0
    for flag in flags:
        value = as_int(flag.get("value", 0))
        computed_mask |= value
        if "bit" in flag and value != (1 << as_int(flag["bit"])):
            failures.append(f"flags.{flag.get('name')}: bit does not match value")
    if valid_mask != computed_mask:
        failures.append("flags_valid_mask must equal OR of all flag values")

    for group_name, key, mask_key in (
        ("control_bits", "control_bits", "control_valid_mask"),
        ("irq_bits", "irq_bits", "irq_valid_mask"),
    ):
        entries = schema.get(key, [])
        check_named_values(failures, group_name, entries, require_power_of_two=True)
        computed_mask = 0
        for entry in entries:
            value = as_int(entry.get("value", 0))
            computed_mask |= value
            if "bit" in entry and value != (1 << as_int(entry["bit"])):
                failures.append(f"{group_name}.{entry.get('name')}: bit does not match value")
        if as_int(schema.get(mask_key, 0)) != computed_mask:
            failures.append(f"{mask_key} must equal OR of all {group_name} values")

    check_named_values(failures, "op_classes", schema.get("op_classes", []), require_power_of_two=True)
    check_named_values(failures, "capabilities", schema.get("capabilities", []), require_power_of_two=True)
    check_named_values(
        failures,
        "lifecycle_states",
        schema.get("lifecycle_states", []),
        require_power_of_two=True,
    )
    check_named_values(failures, "faults", schema.get("faults", []), require_power_of_two=False)

    fault_names = {fault.get("name") for fault in schema.get("faults", [])}
    for required in ("NONE", "INVALID_PROGRAM_DESCRIPTOR", "UNSUPPORTED_ABI_OR_ISA", "ILLEGAL_INSTRUCTION"):
        if required not in fault_names:
            failures.append(f"faults missing {required}")

    check_registers(schema, failures)
    check_descriptor(schema, failures)
    return failures


def main() -> int:
    try:
        schema = load_schema()
        failures = check_schema(schema)
    except Exception as exc:  # noqa: BLE001 - command-line checker
        print(f"V2 ABI schema check failed: {exc}", file=sys.stderr)
        return 1

    if failures:
        print("V2 ABI schema check failed:", file=sys.stderr)
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1

    print("V2 ABI schema check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
