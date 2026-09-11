#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import json
import sys
from pathlib import Path
from typing import Any

sys.dont_write_bytecode = True

from check_isa import ISA_SCHEMA_PATH, ROOT, as_int, check_schema


BANNER = "Generated from spec/holon_npu_isa.json by tools/gen_isa.py. Do not edit."


def c_hex(value: int | str, width: int = 8) -> str:
    return f"0x{as_int(value):0{width}X}u"


def load_schema(path: Path = ISA_SCHEMA_PATH) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        schema = json.load(f)
    if schema.get("schema_version") != 2:
        raise ValueError("unsupported ISA schema_version")
    failures = check_schema(schema)
    if failures:
        raise ValueError("; ".join(failures))
    return schema


def generated_reference_md(schema: dict[str, Any]) -> str:
    frontend = schema["scalar"]
    lines = [
        f"<!-- {BANNER} -->", "# HolonNPU Scalar ISA Reference", "",
        "Current executable research baseline, not a released hardware ABI.",
        "Behavioral authority: [ISA](ISA.md).", "",
        f"- Scalar profile: `{frontend['scalar_profile']}`, `{frontend['abi']}`.",
        f"- Environment: `{frontend['execution_environment']}`.",
        f"- Alignment: {frontend['alignment_bytes']} bytes; byte order: {frontend['byte_order']}.",
        f"- Low bits `11`: {frontend['scalar_bytes']}-byte scalar word.",
        f"- Low bits `00/01/10`: {frontend['holon_bytes']}-byte Holon frame.",
        f"- ELF base: `{frontend['elf_profile']['base']}`; stack alignment: {frontend['elf_profile']['stack_alignment']}.",
        "", "## Scalar Instructions", "",
        "| Instruction | Extension | Format | Match | Mask |",
        "| --- | --- | --- | --- | --- |",
    ]
    for e in frontend["instructions"]:
        lines.append(f"| `{e['name']}` | {e['extension']} | `{e['format']}` | `{c_hex(e['value'])}` | `{c_hex(e['mask'])}` |")
    lines.extend(["", "## Traps", "", "| Cause | Value |", "| --- | --- |"])
    lines.extend(f"| `{name}` | {value} |" for name, value in frontend["scalar_traps"].items())
    lines.extend(["", "## Machine CSRs", "", "| CSR | Address | Reset | Write mask |", "| --- | --- | --- | --- |"])
    for c in frontend["machine_csrs"]:
        lines.append(f"| `{c['name']}` | `{c['address']}` | `{c['reset']}` | `{c['write_mask']}` |")
    lines.extend(["", "HPM counter/selector zero ranges:"])
    lines.extend(f"- `{r['first']}..{r['last']}`." for r in frontend["machine_zero_csr_ranges"])
    lines.append("")
    return "\n".join(lines)


def generated_scalar_metadata(schema: dict[str, Any]) -> str:
    frontend = schema["scalar"]
    entries = frontend["instructions"]
    formats = sorted({entry["format"] for entry in entries})
    lines = [
        f"// {BANNER}", "#pragma once", "",
        "#include <array>", "#include <cstdint>", "#include <string_view>", "",
        "namespace holon_npu::semantic::instruction {", "",
    ]
    for key in ("alignment_bytes", "scalar_bytes", "holon_bytes", "prefix_mask", "scalar_prefix", "register_count"):
        lines.append(f"inline constexpr std::uint32_t {key} = {c_hex(frontend[key])};")
    for name, shift in frontend["register_fields"].items():
        lines.append(f"inline constexpr unsigned {name}_shift = {shift};")
    profile = frontend["elf_profile"]
    lines.append(f"inline constexpr std::string_view elf_base = \"{profile['base']}\";")
    lines.append(f"inline constexpr unsigned elf_stack_alignment = {profile['stack_alignment']};")
    lines.append("inline constexpr std::array elf_extensions{")
    lines.extend(f"    std::string_view{{\"{extension}\"}}," for extension in profile["extensions"])
    lines.append("};")
    lines.extend(["", "enum class machine_csr : std::uint16_t {"])
    lines.extend(f"    {csr['name']} = {csr['address']}," for csr in frontend["machine_csrs"])
    lines.extend(["};", "struct machine_csr_spec {", "    machine_csr address;",
                  "    std::uint32_t reset, write_mask;", "};",
                  "inline constexpr std::array machine_csrs{"])
    for csr in frontend["machine_csrs"]:
        lines.append(f"    machine_csr_spec{{machine_csr::{csr['name']}, {c_hex(csr['reset'])}, {c_hex(csr['write_mask'])}}},")
    lines.extend(["};", "struct zero_csr_range { std::uint16_t first, last; };",
                  "inline constexpr std::array machine_zero_csr_ranges{"])
    lines.extend(f"    zero_csr_range{{{r['first']}, {r['last']}}}," for r in frontend["machine_zero_csr_ranges"])
    lines.extend(["};", ""])
    lines.extend(["", "enum class scalar_trap_cause : std::uint8_t {"])
    lines.extend(f"    {name} = {value}," for name, value in frontend["scalar_traps"].items())
    lines.extend(["};", "", "enum class scalar_opcode : std::uint8_t {"])
    lines.extend(f"    {entry['name']}," for entry in entries)
    lines.extend(["};", "", "enum class scalar_format : std::uint8_t {"])
    lines.extend(f"    {name}," for name in formats)
    lines.extend([
        "};", "", "struct scalar_pattern {",
        "    scalar_opcode opcode;", "    scalar_format format;",
        "    std::string_view mnemonic;", "    std::uint32_t value;",
        "    std::uint32_t mask;", "};", "",
        "inline constexpr std::array scalar_patterns{",
    ])
    for entry in entries:
        lines.append(
            f"    scalar_pattern{{scalar_opcode::{entry['name']}, scalar_format::{entry['format']}, "
            f"\"{entry['name'].lower()}\", {c_hex(entry['value'])}, {c_hex(entry['mask'])}}},"
        )
    lines.extend(["};", "", "} // namespace holon_npu::semantic::instruction", ""])
    return "\n".join(lines)


def generated_npu_metadata(schema: dict[str, Any]) -> str:
    npu = schema["npu"]
    lines = ["// Generated from spec/holon_npu_isa.json; do not edit.", "#pragma once", "",
             "#include <array>", "#include <cstdint>", "#include <string_view>", "",
             "namespace holon_npu::semantic::instruction {", ""]
    enums = {"npu_type": npu["types"], "mask_policy": npu["policies"], "rounding_mode": npu["roundings"],
             "resource_capacity": npu["capacities"],
             "npu_trap": npu["traps"],
             "npu_kind": dict.fromkeys(npu["roles"].values()),
             "npu_role": dict.fromkeys(npu["roles"])}
    for name, values in enums.items():
        lines.append(f"enum class {name} : std::uint8_t {{")
        lines.extend(f"    {key}" + (f" = {value}" if value is not None else "") + "," for key, value in values.items())
        lines.append("};")
        if name in {"npu_type", "mask_policy", "rounding_mode", "resource_capacity"}:
            lines.append(f"inline constexpr std::array {name}_names{{")
            lines.extend(f"    std::string_view{{\"{key}\"}}," for key in sorted(values, key=values.get))
            lines.append("};")
    for name, count in npu["register_counts"].items():
        lines.append(f"inline constexpr unsigned npu_{name}_count = {count};")
    lines.extend(["inline constexpr unsigned npu_max_operands = 10;", "enum class npu_opcode : std::uint16_t {"])
    for entry in npu["instructions"]:
        code = (entry["opcode"] << npu["opcode_shift"]) | npu["families"][entry["family"]]
        lines.append(f"    {entry['name']} = 0x{code:03x},")
    lines.extend(["};", "struct npu_field {", "    npu_role role; npu_kind kind;",
                  "    unsigned shift, width; std::string_view name;", "};", "struct npu_pattern {",
                  "    npu_opcode opcode; std::string_view name;", "    std::uint64_t variable_mask; unsigned type_mask;",
                  "    unsigned count; std::array<npu_field, npu_max_operands> fields;", "};",
                  "inline constexpr std::array npu_patterns{"])
    for entry in npu["instructions"]:
        fields = npu["formats"][entry["format"]]
        mask = sum(((1 << width) - 1) << shift for _, shift, width in fields)
        type_mask = sum(1 << npu["types"][t] for t in entry.get("types", npu["types"]))
        lines.append(f"    npu_pattern{{npu_opcode::{entry['name']}, \"{entry['name'].lower()}\", 0x{mask:016x}ull, 0x{type_mask:x}, {len(fields)}, {{{{")
        lines.extend(f"        {{npu_role::{role}, npu_kind::{npu['roles'][role]}, {shift}, {width}, \"{role}\"}}," for role, shift, width in fields)
        lines.append("    }}},")
    lines.extend(["};", "", "} // namespace holon_npu::semantic::instruction", ""])
    return "\n".join(lines)


def generated_npu_reference(schema: dict[str, Any]) -> str:
    npu = schema["npu"]
    lines = ["<!-- Generated from spec/holon_npu_isa.json; do not edit. -->",
             "# Holon NPU Operand Reference", "", "Current executable research operand contract.",
             "State, arithmetic and fault authority: [ISA](ISA.md).", "",
             "Low bits: vector/predicate=00, matrix=01, DMA/system=10. Opcode is bits 11:2.",
             "Unused fields and unlisted opcodes are illegal. Format fields below are role:lsb:width.", "",
             "| Format | Fields |", "| --- | --- |"]
    for name, fields in npu["formats"].items():
        lines.append(f"| `{name}` | " + ", ".join(f"`{r}:{s}:{w}`" for r, s, w in fields) + " |")
    lines.extend(["", "| Instruction | Family | Opcode | Format | Type domain | Semantic contract |", "| --- | --- | --- | --- | --- | --- |"])
    for e in npu["instructions"]:
        types = ", ".join(e.get("types", npu["types"])) if any(f[0] == "type" for f in npu["formats"][e["format"]]) else "-"
        lines.append(f"| `{e['name']}` | {e['family']} | {e['opcode']} | `{e['format']}` | "
                     + types + f" | `{e['semantics']}` |")
    lines.extend(["", "| Role | Domain |", "| --- | --- |"])
    lines.extend(f"| `{role}` | {kind} |" for role, kind in npu["roles"].items())
    lines.extend(["", "| Domain | Values |", "| --- | --- |"])
    for key in ("types", "policies", "roundings", "capacities", "register_counts", "traps"):
        lines.append(f"| {key} | " + ", ".join(f"`{name}`={code}" for name, code in npu[key].items()) + " |")
    lines.append("")
    return "\n".join(lines)


def render_all(schema: dict[str, Any]) -> dict[str, str]:
    return {
        "docs/ISA_REFERENCE.md": generated_reference_md(schema),
        "sim/semantic/holon_npu_scalar_metadata.hpp": generated_scalar_metadata(schema),
        "sim/semantic/holon_npu_operand_metadata.hpp": generated_npu_metadata(schema),
        "docs/NPU_OPERAND_REFERENCE.md": generated_npu_reference(schema),
    }


def write_outputs(outputs: dict[str, str], output_root: Path) -> None:
    for rel_path, text in outputs.items():
        path = output_root / rel_path
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            path.write_text(text, encoding="utf-8")


def check_outputs(outputs: dict[str, str], root: Path) -> int:
    errors = 0
    for rel_path, expected in outputs.items():
        actual_path = root / rel_path
        if not actual_path.exists():
            print(f"missing generated output: {rel_path}", file=sys.stderr)
            errors += 1
            continue
        actual = actual_path.read_text(encoding="utf-8")
        if actual != expected:
            errors += 1
            print(f"{rel_path} is not up to date with {ISA_SCHEMA_PATH.relative_to(ROOT)}", file=sys.stderr)
            diff = difflib.unified_diff(
                actual.splitlines(),
                expected.splitlines(),
                fromfile=f"{rel_path} (current)",
                tofile=f"{rel_path} (generated)",
                lineterm="",
            )
            for line in list(diff)[:120]:
                print(line, file=sys.stderr)
    if errors == 0:
        print(f"ISA generated-source check passed ({len(outputs)} outputs).")
    return 1 if errors else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate HolonNPU ISA outputs from schema.")
    parser.add_argument("--check", action="store_true", help="Verify tracked outputs are current.")
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT,
        help="Directory where generated outputs are written.",
    )
    args = parser.parse_args()

    try:
        schema = load_schema()
    except Exception as exc:  # noqa: BLE001 - command-line generator
        print(f"ISA generation failed: {exc}", file=sys.stderr)
        return 1

    outputs = render_all(schema)
    if args.check:
        return check_outputs(outputs, args.output_root)

    write_outputs(outputs, args.output_root)
    for rel_path in outputs:
        print(rel_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
