#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import json
import sys
from pathlib import Path
from typing import Any

from check_isa import ISA_SCHEMA_PATH, ROOT, as_int, check_schema


BANNER = "Generated from spec/holon_npu_isa.json by tools/gen_isa.py. Do not edit."


def c_hex(value: int | str, width: int = 8) -> str:
    return f"0x{as_int(value):0{width}X}u"


def c_const(type_name: str, name: str, value: int | str, width: int = 8, pad: int = 0) -> str:
    spacer = " " * max(pad - len(name), 1)
    return f"static constexpr {type_name} {name}{spacer}= {c_hex(value, width)};"


def sv_hex(value: int | str, width: int = 8) -> str:
    return f"32'h{as_int(value):0{width}X}"


def sv_param(type_name: str, name: str, value: int | str, width: int = 8, pad: int = 0) -> str:
    spacer = " " * max(pad - len(name), 1)
    return f"    localparam {type_name} {name}{spacer}= {sv_hex(value, width)};"


def sv_opcode_param(name: str, value: int | str, pad: int = 0) -> str:
    spacer = " " * max(pad - len(name), 1)
    return f"    localparam logic [3:0] {name}{spacer}= 4'h{as_int(value):01X};"


def load_schema(path: Path = ISA_SCHEMA_PATH) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        schema = json.load(f)
    if schema.get("schema_version") != 1:
        raise ValueError("unsupported ISA schema_version")
    failures = check_schema(schema)
    if failures:
        raise ValueError("; ".join(failures))
    return schema


def generated_header(schema: dict[str, Any]) -> str:
    isa = schema["isa"]
    common_masks = schema["common_masks"]
    field_layout = schema["field_layout"]
    classes = schema["instruction_classes"]
    reserved = schema["reserved_classes"]
    instructions = schema["instructions"]
    encoding_constants = schema["encoding_constants"]

    class_names = [(f"HOLON_NPU_ISA_CLASS_{entry['name']}", entry["value"]) for entry in classes]
    masks = [(f"HOLON_NPU_ISA_CLASS_{entry['name']}_MASK", entry["mask"]) for entry in classes]
    reserved_names = [(f"HOLON_NPU_ISA_CLASS_{entry['name']}", entry["value"]) for entry in reserved]
    opcode_names = [(f"HOLON_NPU_ISA_OPCODE_{entry['name']}", entry["opcode"]) for entry in instructions]
    encoding_names = [(f"HOLON_NPU_ISA_{entry['name']}", entry["value"]) for entry in encoding_constants]
    all_constants = class_names + masks + reserved_names + opcode_names + encoding_names
    pad = max(len(name) for name, _ in all_constants)

    lines = [
        f"/* {BANNER} */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        c_const("uint8_t", "HOLON_NPU_ISA_MAJOR", isa["major"], width=2, pad=34),
        c_const("uint8_t", "HOLON_NPU_ISA_MINOR", isa["minor"], width=2, pad=34),
        c_const("uint8_t", "HOLON_NPU_ISA_INSTRUCTION_BYTES", isa["alignment_bytes"], width=2, pad=34),
        c_const("uint8_t", "HOLON_NPU_ISA_INSTRUCTION_BITS", isa["instruction_bits"], width=2, pad=34),
        c_const("uint8_t", "HOLON_NPU_ISA_OPCODE_SHIFT", field_layout["opcode_shift"], width=2, pad=34),
        c_const("uint8_t", "HOLON_NPU_ISA_RD_SHIFT", field_layout["rd_shift"], width=2, pad=34),
        c_const("uint8_t", "HOLON_NPU_ISA_RS1_SHIFT", field_layout["rs1_shift"], width=2, pad=34),
        c_const("uint8_t", "HOLON_NPU_ISA_RS2_SHIFT", field_layout["rs2_shift"], width=2, pad=34),
        c_const("uint32_t", "HOLON_NPU_ISA_CLASS_MASK", common_masks["class"], pad=34),
        c_const("uint32_t", "HOLON_NPU_ISA_FIELD_MASK", field_layout["field_mask"], pad=34),
        c_const("uint32_t", "HOLON_NPU_ISA_IMM_MASK", field_layout["imm_mask"], pad=34),
        "",
    ]

    for name, value in class_names:
        lines.append(c_const("uint32_t", name, value, pad=pad))
    lines.append("")
    for name, value in masks:
        lines.append(c_const("uint32_t", name, value, pad=pad))
    lines.append("")
    for name, value in reserved_names:
        lines.append(c_const("uint32_t", name, value, pad=pad))
    lines.append("")
    for name, value in opcode_names:
        lines.append(c_const("uint8_t", name, value, width=2, pad=pad))
    lines.append("")
    for name, value in encoding_names:
        lines.append(c_const("uint32_t", name, value, pad=pad))

    lines.extend(
        [
            "",
            "typedef enum holon_npu_isa_class {",
        ]
    )
    for entry in classes:
        lines.append(f"    HOLON_NPU_ISA_ENUM_{entry['name']} = {c_hex(entry['value'])},")
    for index, entry in enumerate(reserved):
        comma = "," if index + 1 < len(reserved) else ""
        lines.append(f"    HOLON_NPU_ISA_ENUM_{entry['name']} = {c_hex(entry['value'])}{comma}")
    lines.extend(["} holon_npu_isa_class_t;", ""])
    return "\n".join(lines)


def generated_sv_pkg(schema: dict[str, Any]) -> str:
    isa = schema["isa"]
    common_masks = schema["common_masks"]
    field_layout = schema["field_layout"]
    classes = schema["instruction_classes"]
    reserved = schema["reserved_classes"]
    instructions = schema["instructions"]
    encoding_constants = schema["encoding_constants"]

    class_names = [(f"NPU_ISA_CLASS_{entry['name']}", entry["value"]) for entry in classes]
    masks = [(f"NPU_ISA_CLASS_{entry['name']}_MASK", entry["mask"]) for entry in classes]
    reserved_names = [(f"NPU_ISA_CLASS_{entry['name']}", entry["value"]) for entry in reserved]
    opcode_names = [(f"NPU_ISA_OPCODE_{entry['name']}", entry["opcode"]) for entry in instructions]
    encoding_names = [(f"NPU_ISA_{entry['name']}", entry["value"]) for entry in encoding_constants]
    all_constants = class_names + masks + reserved_names + opcode_names + encoding_names
    pad = max(len(name) for name, _ in all_constants)

    lines = [
        f"// {BANNER}",
        "/* verilator lint_off UNUSEDPARAM */",
        "package npu_isa_pkg;",
        f"    localparam int unsigned NPU_ISA_MAJOR             = {as_int(isa['major'])};",
        f"    localparam int unsigned NPU_ISA_MINOR             = {as_int(isa['minor'])};",
        f"    localparam int unsigned NPU_ISA_INSTRUCTION_BYTES = {as_int(isa['alignment_bytes'])};",
        f"    localparam int unsigned NPU_ISA_INSTRUCTION_BITS  = {as_int(isa['instruction_bits'])};",
        f"    localparam int unsigned NPU_ISA_OPCODE_SHIFT      = {as_int(field_layout['opcode_shift'])};",
        f"    localparam int unsigned NPU_ISA_RD_SHIFT          = {as_int(field_layout['rd_shift'])};",
        f"    localparam int unsigned NPU_ISA_RS1_SHIFT         = {as_int(field_layout['rs1_shift'])};",
        f"    localparam int unsigned NPU_ISA_RS2_SHIFT         = {as_int(field_layout['rs2_shift'])};",
        f"    localparam logic [31:0] NPU_ISA_CLASS_MASK        = {sv_hex(common_masks['class'])};",
        f"    localparam logic [31:0] NPU_ISA_FIELD_MASK        = {sv_hex(field_layout['field_mask'])};",
        f"    localparam logic [31:0] NPU_ISA_IMM_MASK          = {sv_hex(field_layout['imm_mask'])};",
        "",
    ]

    for name, value in class_names:
        lines.append(sv_param("logic [31:0]", name, value, pad=pad))
    lines.append("")
    for name, value in masks:
        lines.append(sv_param("logic [31:0]", name, value, pad=pad))
    lines.append("")
    for name, value in reserved_names:
        lines.append(sv_param("logic [31:0]", name, value, pad=pad))
    lines.append("")
    for name, value in opcode_names:
        lines.append(sv_opcode_param(name, value, pad=pad))
    lines.append("")
    for name, value in encoding_names:
        lines.append(sv_param("logic [31:0]", name, value, pad=pad))

    lines.extend(["", "endpackage", "/* verilator lint_on UNUSEDPARAM */", ""])
    return "\n".join(lines)


def generated_reference_md(schema: dict[str, Any]) -> str:
    isa = schema["isa"]
    common_masks = schema["common_masks"]
    field_layout = schema["field_layout"]
    lines = [
        f"<!-- {BANNER} -->",
        "# HolonNPU ISA Reference",
        "",
        "This file is generated from `spec/holon_npu_isa.json`. Edit the schema",
        "and regenerate outputs instead of editing this file by hand.",
        "",
        "## ISA Version",
        "",
        f"- ISA version: {isa['major']}.{isa['minor']}.",
        f"- Instruction width: {isa['instruction_bits']} bits.",
        f"- Instruction alignment: {isa['alignment_bytes']} bytes.",
        f"- Ownership: {isa['ownership']}",
        "",
        "## Initial Field Layout",
        "",
        f"- Opcode shift: {field_layout['opcode_shift']}.",
        f"- Destination/local-offset field shift: {field_layout['rd_shift']}.",
        f"- Source/count field shift: {field_layout['rs1_shift']}.",
        f"- Source/reserved field shift: {field_layout['rs2_shift']}.",
        f"- Class mask: `{c_hex(common_masks['class'])}`.",
        f"- 4-bit field mask: `{c_hex(field_layout['field_mask'])}`.",
        f"- 12-bit immediate mask: `{c_hex(field_layout['imm_mask'])}`.",
        "",
        "## Instruction Classes",
        "",
        "| Class | Value | Mask | Format | Fault | Coverage | Semantics | Description |",
        "| ----- | ----- | ---- | ------ | ----- | -------- | --------- | ----------- |",
    ]
    for entry in schema["instruction_classes"]:
        lines.append(
            f"| `{entry['name']}` | `{c_hex(entry['value'])}` | `{c_hex(entry['mask'])}` | "
            f"`{entry['format']}` | `{entry['fault']}` | `{entry['coverage']}` | "
            f"`{entry['semantics']}` | {entry['description']} |"
        )

    lines.extend(
        [
            "",
            "## Encoding Constants",
            "",
            "| Name | Value | Description |",
            "| ---- | ----- | ----------- |",
        ]
    )
    for entry in schema["encoding_constants"]:
        lines.append(
            f"| `{entry['name']}` | `{c_hex(entry['value'])}` | {entry['description']} |"
        )

    lines.extend(
        [
            "",
            "## Implemented Instructions",
            "",
            "| Instruction | Class | Opcode | Format | Fault | Coverage | Semantics | Description |",
            "| ----------- | ----- | ------ | ------ | ----- | -------- | --------- | ----------- |",
        ]
    )
    for entry in schema["instructions"]:
        lines.append(
            f"| `{entry['name']}` | `{entry['class']}` | `{c_hex(entry['opcode'], width=2)}` | "
            f"`{entry['format']}` | `{entry['fault']}` | `{entry['coverage']}` | "
            f"{entry['semantics']} | {entry['description']} |"
        )

    lines.extend(
        [
            "",
            "## Reserved Classes",
            "",
            "| Class | Value | Mask | Description |",
            "| ----- | ----- | ---- | ----------- |",
        ]
    )
    for entry in schema["reserved_classes"]:
        lines.append(
            f"| `{entry['name']}` | `{c_hex(entry['value'])}` | `{c_hex(entry['mask'])}` | "
            f"{entry['description']} |"
        )

    lines.extend(
        [
            "",
            "## Architectural State",
            "",
        ]
    )
    for state_group, names in schema["architectural_state"].items():
        lines.append(f"- `{state_group}`: {', '.join(f'`{name}`' for name in names)}.")
    lines.append("")
    return "\n".join(lines)


def render_all(schema: dict[str, Any]) -> dict[str, str]:
    return {
        "include/holon_npu_isa.h": generated_header(schema),
        "rtl/common/npu_isa_pkg.sv": generated_sv_pkg(schema),
        "docs/ISA_REFERENCE.md": generated_reference_md(schema),
    }


def write_outputs(outputs: dict[str, str], output_root: Path) -> None:
    for rel_path, text in outputs.items():
        path = output_root / rel_path
        path.parent.mkdir(parents=True, exist_ok=True)
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
