#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ISA_SCHEMA_PATH = ROOT / "spec/holon_npu_isa.json"
COVERAGE_REGISTRY_PATH = ROOT / "sim/tb_coverage.hpp"

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
REQUIRED_ENCODING_CONSTANT_FIELDS = {"name", "value", "description"}
REQUIRED_OPERATION_CLASS_FIELDS = {"name", "value", "description"}
REQUIRED_INSTRUCTION_FIELDS = {
    "name",
    "class",
    "opcode",
    "format",
    "coverage",
    "fault",
    "semantics",
    "description",
}
REQUIRED_FIELD_LAYOUT_FIELDS = {
    "opcode_shift",
    "rd_shift",
    "rs1_shift",
    "rs2_shift",
    "field_mask",
    "imm_mask",
}


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


def check_semantic_frontend(schema: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    frontend = schema.get("semantic_frontend", {})
    contract = {
        "stage": "scalar_hart", "scalar_profile": "rv32im_zicsr", "abi": "ilp32",
        "elf_profile": {"base": "rv32i2p1", "extensions": ["m2p0", "zicsr2p0", "zmmul1p0"], "stack_alignment": 16},
        "execution_environment": "single_hart_machine", "byte_order": "little",
        "alignment_bytes": 4, "scalar_bytes": 4, "holon_bytes": 8,
        "holon_prefixes": [0, 1, 2], "register_count": 32,
        "register_fields": {"rd": 7, "rs1": 15, "rs2": 20},
        "scalar_memory": {"address_bits": 32, "byte_order": "little", "misaligned": "trap"},
        "scalar_traps": {
            "instruction_address_misaligned": 0, "instruction_access_fault": 1,
            "illegal_instruction": 2, "breakpoint": 3,
            "load_address_misaligned": 4, "load_access_fault": 5,
            "store_address_misaligned": 6, "store_access_fault": 7,
            "machine_environment_call": 11,
        },
    }
    for key, value in contract.items():
        if frontend.get(key) != value:
            failures.append(f"semantic_frontend.{key} must be {value!r}")
    csrs = frontend.get("machine_csrs", [])
    names, addresses = set(), set()
    required = {"mstatus", "misa", "mie", "mtvec", "mstatush", "mcountinhibit",
                "mscratch", "mepc", "mcause", "mtval", "mip", "mcycle", "minstret",
                "mcycleh", "minstreth", "mvendorid", "marchid", "mimpid", "mhartid", "mconfigptr"}
    try:
        for csr in csrs:
            name, address = csr["name"], int(csr["address"], 0)
            reset, mask = int(csr["reset"], 0), int(csr["write_mask"], 0)
            if name in names or address in addresses or not 0 <= address < 4096:
                failures.append("duplicate or invalid machine CSR")
            if not 0 <= reset <= 0xffffffff or not 0 <= mask <= 0xffffffff:
                failures.append("machine CSR value exceeds XLEN")
            if address >> 10 == 3 and mask:
                failures.append("read-only CSR has writable fields")
            names.add(name)
            addresses.add(address)
        for region in frontend.get("machine_zero_csr_ranges", []):
            first, last = int(region["first"], 0), int(region["last"], 0)
            if not 0 <= first <= last < 4096:
                failures.append("invalid machine CSR range")
                continue
            for address in range(first, last + 1):
                if address in addresses:
                    failures.append("overlapping machine CSR range")
                addresses.add(address)
    except (KeyError, TypeError, ValueError):
        failures.append("malformed machine CSR metadata")
    if names != required:
        failures.append("incomplete machine CSR inventory")
    if frontend.get("machine_zero_csr_ranges") != [
        {"first": "0x323", "last": "0x33f"}, {"first": "0xb03", "last": "0xb1f"},
        {"first": "0xb83", "last": "0xb9f"}]:
        failures.append("machine HPM zero ranges must cover counters/selectors 3..31")
    for key in ("prefix_mask", "scalar_prefix"):
        if as_int(frontend.get(key, 0)) != 3:
            failures.append(f"semantic_frontend.{key} must be 3")
    if not frontend.get("authority"):
        failures.append("semantic_frontend requires an encoding authority")
    entries = frontend.get("instructions", [])
    counts = Counter(entry.get("extension") for entry in entries)
    if counts != {"I": 40, "M": 8, "Zicsr": 6, "machine": 2}:
        failures.append("semantic_frontend requires complete RV32IM/Zicsr and MRET/WFI sets")
    names: set[str] = set()
    valid_entries = []
    formats = {"r", "i", "s", "b", "u", "j", "shift", "fence", "system", "csr", "csr_immediate"}
    for entry in entries:
        name = entry.get("name", "")
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name) or name in names:
            failures.append(f"semantic_frontend invalid/duplicate name {name!r}")
        names.add(name)
        if entry.get("format") not in formats:
            failures.append(f"{name}: unknown scalar operand format")
        if not {"mask", "value"} <= entry.keys():
            failures.append(f"{name}: missing scalar encoding pattern")
            continue
        mask, value = as_int(entry["mask"]), as_int(entry["value"])
        if not (0 <= value <= 0xFFFFFFFF and 0 < mask <= 0xFFFFFFFF):
            failures.append(f"{name}: scalar encoding must fit 32 bits")
        elif value & ~mask or mask & 0x7F != 0x7F or value & 3 != 3:
            failures.append(f"{name}: invalid scalar mask/prefix")
        else:
            valid_entries.append(entry)
    for index, entry in enumerate(valid_entries):
        for other in valid_entries[index + 1:]:
            if patterns_overlap(entry, other):
                failures.append(f"scalar encoding overlap: {entry['name']} / {other['name']}")
    return failures


def check_semantic_npu(schema: dict[str, Any]) -> list[str]:
    failures = []
    widths = {"scalar": 5, "vector": 5, "predicate": 5, "tile": 3, "view": 4,
              "element": 4, "policy": 1, "rounding": 2, "displacement": 20,
              "scale": 2, "capability": 2}
    try:
        npu = schema["semantic_npu"]
        for key, expected in {
            "stage": "operand_contract", "families": {"vector": 0, "matrix": 1, "system": 2},
            "opcode_shift": 2, "opcode_bits": 10,
            "register_counts": {"scalar": 32, "vector": 32, "predicate": 32, "tile": 8, "view": 16},
            "types": {"i8": 0, "u8": 1, "i16": 2, "u16": 3, "i32": 4, "u32": 5, "f32": 6},
            "policies": {"merge": 0, "zero": 1}, "roundings": {"rne": 0, "rtz": 1, "rdn": 2, "rup": 3},
            "capacities": {"vector_bytes": 0, "matrix_rows": 1, "matrix_cols": 2, "tile_bytes": 3},
            "traps": {"invalid_operand": 24},
        }.items():
            if npu.get(key) != expected:
                failures.append(f"semantic_npu.{key}: unsupported contract")
        roles, formats = npu["roles"], npu["formats"]
        used_roles = set()
        for role, kind in roles.items():
            if not re.fullmatch(r"[a-z][a-z0-9_]*", role) or kind not in widths:
                failures.append("invalid NPU operand role/kind")
        for name, fields in formats.items():
            if not re.fullmatch(r"[a-z][a-z0-9_]*", name) or len(fields) > 10:
                failures.append("invalid NPU operand format")
            mask, seen = 0xfff, set()
            for role, shift, width in fields:
                used_roles.add(role)
                if role not in roles or role in seen or width != widths.get(roles.get(role)):
                    failures.append(f"{name}: duplicate/invalid field domain")
                    continue
                seen.add(role)
                if not 12 <= shift < 64 or shift + width > 64:
                    failures.append(f"{name}: field exceeds instruction")
                    continue
                bits = ((1 << width) - 1) << shift
                if mask & bits:
                    failures.append(f"{name}: overlapping operand fields")
                mask |= bits
        if used_roles != set(roles):
            failures.append("unused or unknown NPU roles")
        names, codes, used_formats, families = set(), set(), set(), set()
        for entry in npu["instructions"]:
            name, family, opcode, form = entry["name"], entry["family"], entry["opcode"], entry["format"]
            if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name) or name in names:
                failures.append("duplicate/invalid NPU instruction name")
            names.add(name)
            if family not in npu["families"] or not 0 <= opcode < 1024 or (family, opcode) in codes:
                failures.append(f"{name}: overlapping/invalid NPU opcode")
            if family == "vector" and opcode == 0:
                failures.append(f"{name}: all-zero instruction must remain illegal")
            codes.add((family, opcode)); families.add(family); used_formats.add(form)
            if form not in formats or not re.fullmatch(r"[a-z][a-z0-9_]*", entry["semantics"]):
                failures.append(f"{name}: missing operand/semantic contract")
            if "types" in entry and (not entry["types"] or len(entry["types"]) != len(set(entry["types"]))
                    or not set(entry["types"]) <= set(npu["types"])
                    or not any(field[0] == "type" for field in formats.get(form, []))):
                failures.append(f"{name}: invalid operation type domain")
        if used_formats != set(formats) or families != set(npu["families"]):
            failures.append("unused NPU formats or prefix families")
    except (KeyError, TypeError, ValueError):
        failures.append("malformed semantic_npu metadata")
    return failures


def check_schema(schema: dict[str, Any]) -> list[str]:
    failures = check_semantic_frontend(schema) + check_semantic_npu(schema)
    isa = schema.get("isa", {})
    if isa.get("instruction_bits") != 32:
        failures.append("isa.instruction_bits must be 32")
    if isa.get("alignment_bytes") != 4:
        failures.append("isa.alignment_bytes must be 4 for fixed 32-bit instructions")

    classes = schema.get("instruction_classes", [])
    reserved = schema.get("reserved_classes", [])
    instructions = schema.get("instructions", [])
    encoding_constants = schema.get("encoding_constants", [])
    field_layout = schema.get("field_layout", {})
    if not classes:
        failures.append("instruction_classes must not be empty")
    if not reserved:
        failures.append("reserved_classes must not be empty")
    if not instructions:
        failures.append("instructions must not be empty")

    operation_classes = schema.get("operation_classes", [])
    operation_names: set[str] = set()
    operation_values: set[int] = set()
    for entry in operation_classes:
        missing = REQUIRED_OPERATION_CLASS_FIELDS - entry.keys()
        if missing:
            failures.append(
                f"{entry.get('name', '<unnamed>')}: missing operation-class fields {sorted(missing)}"
            )
        name = entry.get("name")
        value = as_int(entry.get("value", 0))
        if name in operation_names:
            failures.append(f"duplicate operation-class name {name}")
        if value in operation_values:
            failures.append(f"duplicate operation-class value 0x{value:X}")
        if value == 0 or (value & (value - 1)) != 0:
            failures.append(f"{name}: operation-class value must be one bit")
        operation_names.add(name)
        operation_values.add(value)
    required_operation_classes = {
        "FRONTEND_CONTROL", "PREDICATE", "VECTOR", "QUANTIZATION", "MATRIX",
        "DMA", "CSR_DEBUG", "SYNC", "SYSTEM",
    }
    missing_operations = required_operation_classes - operation_names
    if missing_operations:
        failures.append(f"missing required operation classes {sorted(missing_operations)}")

    missing_layout = REQUIRED_FIELD_LAYOUT_FIELDS - field_layout.keys()
    if missing_layout:
        failures.append(f"field_layout missing fields {sorted(missing_layout)}")
    elif not (
        field_layout["opcode_shift"] == 24
        and field_layout["rd_shift"] == 20
        and field_layout["rs1_shift"] == 16
        and field_layout["rs2_shift"] == 12
        and as_int(field_layout["field_mask"]) == 0xF
        and as_int(field_layout["imm_mask"]) == 0xFFF
    ):
        failures.append("field_layout must match the initial 4-bit register/opcode and 12-bit immediate format")

    seen_names: set[str] = set()
    seen_coverage: set[str] = set()
    all_patterns = classes + reserved

    for entry in encoding_constants:
        missing = REQUIRED_ENCODING_CONSTANT_FIELDS - entry.keys()
        if missing:
            failures.append(
                f"{entry.get('name', '<unnamed>')}: missing encoding constant fields {sorted(missing)}"
            )
        name = entry.get("name")
        if name in seen_names:
            failures.append(f"duplicate encoding constant name {name}")
        seen_names.add(name)
        value = as_int(entry.get("value", 0))
        if value < 0 or value > 0xFFFF_FFFF:
            failures.append(f"{name}: encoding constant must fit in 32 bits")

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
        failures.append(f"missing required classes {sorted(missing_classes)}")

    class_names = {entry["name"] for entry in classes}
    class_instruction_counts = {name: 0 for name in class_names}
    seen_instruction_opcodes: set[tuple[str, int]] = set()
    for entry in instructions:
        missing = REQUIRED_INSTRUCTION_FIELDS - entry.keys()
        if missing:
            failures.append(f"{entry.get('name', '<unnamed>')}: missing instruction fields {sorted(missing)}")
        name = entry.get("name")
        if name in seen_names:
            failures.append(f"duplicate instruction/class name {name}")
        seen_names.add(name)
        coverage = entry.get("coverage")
        if coverage in seen_coverage:
            failures.append(f"duplicate instruction/class coverage point {coverage}")
        seen_coverage.add(coverage)
        class_name = entry.get("class")
        if class_name not in class_names:
            failures.append(f"{name}: instruction class {class_name} is not defined")
        else:
            class_instruction_counts[class_name] += 1
        opcode = as_int(entry.get("opcode", 0))
        if opcode > 0xF:
            failures.append(f"{name}: opcode must fit in the 4-bit opcode field")
        key = (str(class_name), opcode)
        if key in seen_instruction_opcodes:
            failures.append(f"duplicate opcode {opcode:#x} in instruction class {class_name}")
        seen_instruction_opcodes.add(key)

    empty_classes = sorted(name for name, count in class_instruction_counts.items() if count == 0)
    if empty_classes:
        failures.append(f"implemented instruction classes without instructions {empty_classes}")

    return failures


def check_coverage_registry(schema: dict[str, Any]) -> list[str]:
    if not COVERAGE_REGISTRY_PATH.exists():
        return [f"missing typed coverage registry {COVERAGE_REGISTRY_PATH.relative_to(ROOT)}"]

    registry_text = COVERAGE_REGISTRY_PATH.read_text(encoding="utf-8")
    registered = set(
        re.findall(
            r'coverage_point_info\{coverage_point::\w+,\s*"([^"]+)"',
            registry_text,
        )
    )
    required = {
        entry["coverage"]
        for entry in schema.get("instruction_classes", []) + schema.get("instructions", [])
    }
    missing = sorted(required - registered)
    return [f"ISA coverage points missing from typed registry {missing}"] if missing else []


def main() -> int:
    try:
        schema = load_schema()
        failures = check_schema(schema)
        failures.extend(check_coverage_registry(schema))
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
