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
    if schema.get("schema_version") != 2:
        raise ValueError("unsupported ISA schema_version")
    return schema


def patterns_overlap(a: dict[str, Any], b: dict[str, Any]) -> bool:
    a_value = as_int(a["value"])
    b_value = as_int(b["value"])
    common_mask = as_int(a["mask"]) & as_int(b["mask"])
    return (a_value & common_mask) == (b_value & common_mask)


def check_scalar(schema: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    frontend = schema.get("scalar", {})
    contract = {
        "scalar_profile": "rv32im_zicsr", "abi": "ilp32",
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
            failures.append(f"scalar.{key} must be {value!r}")
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
            failures.append(f"scalar.{key} must be 3")
    if not frontend.get("authority"):
        failures.append("scalar requires an encoding authority")
    entries = frontend.get("instructions", [])
    counts = Counter(entry.get("extension") for entry in entries)
    if counts != {"I": 40, "M": 8, "Zicsr": 6, "machine": 2}:
        failures.append("scalar requires complete RV32IM/Zicsr and MRET/WFI sets")
    names: set[str] = set()
    valid_entries = []
    formats = {"r", "i", "s", "b", "u", "j", "shift", "fence", "system", "csr", "csr_immediate"}
    for entry in entries:
        name = entry.get("name", "")
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name) or name in names:
            failures.append(f"scalar invalid/duplicate name {name!r}")
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


def check_npu(schema: dict[str, Any]) -> list[str]:
    failures = []
    widths = {"scalar": 5, "vector": 5, "predicate": 5, "tile": 3, "view": 4,
              "element": 4, "policy": 1, "rounding": 2, "displacement": 20,
              "scale": 2, "capability": 2}
    try:
        npu = schema["npu"]
        for key, expected in {
            "families": {"vector": 0, "matrix": 1, "system": 2},
            "opcode_shift": 2, "opcode_bits": 10,
            "register_counts": {"scalar": 32, "vector": 32, "predicate": 32, "tile": 8, "view": 16},
            "types": {"i8": 0, "u8": 1, "i16": 2, "u16": 3, "i32": 4, "u32": 5, "f32": 6},
            "policies": {"merge": 0, "zero": 1}, "roundings": {"rne": 0, "rtz": 1, "rdn": 2, "rup": 3},
            "capacities": {"vector_bytes": 0, "matrix_rows": 1, "matrix_cols": 2, "tile_bytes": 3},
            "traps": {"invalid_operand": 24},
        }.items():
            if npu.get(key) != expected:
                failures.append(f"npu.{key}: unsupported contract")
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
        failures.append("malformed npu metadata")
    return failures


def check_schema(schema: dict[str, Any]) -> list[str]:
    if set(schema) != {"schema_version", "contract", "scalar", "npu"} or schema.get("schema_version") != 2:
        return ["expected canonical scalar/npu schema version 2 without legacy tables"]
    if schema.get("contract") != {
        "name": "Holon RV32/Holon research baseline", "status": "experimental", "reference": "docs/ISA.md"
    }:
        return ["missing current research contract identity"]
    try:
        return check_scalar(schema) + check_npu(schema)
    except (KeyError, TypeError, ValueError, AttributeError) as exc:
        return [f"malformed ISA metadata: {exc}"]


def main() -> int:
    try:
        failures = check_schema(load_schema())
    except (OSError, ValueError) as exc:
        failures = [str(exc)]
    if failures:
        print("ISA metadata check failed:\n" + "\n".join(failures), file=sys.stderr)
        return 1
    print("ISA metadata check passed (current RV32/Holon contract).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
