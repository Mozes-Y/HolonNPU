#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path
from typing import Any

from check_abi import ABI_SCHEMA_PATH, ROOT, as_int, load_schema as load_checked_schema


BANNER = "Generated from spec/holon_npu_abi.json by tools/gen_abi.py. Do not edit."


def c_hex(value: int | str, width: int = 8) -> str:
    return f"0x{as_int(value):0{width}X}u"


def c_const(type_name: str, name: str, value: int | str, width: int = 8, pad: int = 0) -> str:
    spacer = " " * max(pad - len(name), 1)
    return f"static constexpr {type_name} {name}{spacer}= {c_hex(value, width)};"


def sv_hex(value: int | str, bits: int) -> str:
    return f"{bits}'h{as_int(value):0{max(bits // 4, 1)}X}"


def sv_const_logic(bits: int, name: str, value: int | str, pad: int = 0) -> str:
    spacer = " " * max(pad - len(name), 1)
    return f"    localparam logic [{bits - 1}:0] {name}{spacer}= {sv_hex(value, bits)};"


def sv_const_int(name: str, value: int | str, pad: int = 0) -> str:
    spacer = " " * max(pad - len(name), 1)
    return f"    localparam int unsigned {name}{spacer}= {as_int(value)};"


def md_hex(value: int | str, width: int = 8) -> str:
    return f"0x{as_int(value):0{width}X}"


def md_offset(value: int | str) -> str:
    return f"0x{as_int(value):03X}"


def md_desc_offset(value: int | str) -> str:
    return f"0x{as_int(value):02X}"


def load_schema(path: Path = ABI_SCHEMA_PATH) -> dict[str, Any]:
    return load_checked_schema(path)


def generated_header(schema: dict[str, Any]) -> str:
    abi = schema["abi"]
    constants = schema["constants"]
    descriptor = schema["descriptor"]
    completion_record = schema["completion_record"]
    registers = schema["registers"]

    register_constants = [(f"HOLON_NPU_REG_{reg['name']}", reg["offset"]) for reg in registers]
    register_reset_constants = [
        (f"HOLON_NPU_RESET_{reg['name']}", reg["reset"]) for reg in registers
    ]
    lifecycle_constants = [
        (f"HOLON_NPU_STATUS_{state['name']}", state["value"])
        for state in schema["lifecycle_states"]
    ]
    flag_constants = [(f"HOLON_NPU_PROGRAM_FLAG_{flag['name']}", flag["value"]) for flag in schema["flags"]]
    flag_constants.append(("HOLON_NPU_PROGRAM_FLAG_VALID_MASK", schema["flags_valid_mask"]))
    control_constants = [(f"HOLON_NPU_CONTROL_{entry['name']}", entry["value"]) for entry in schema["control_bits"]]
    control_constants.append(("HOLON_NPU_CONTROL_VALID_MASK", schema["control_valid_mask"]))
    doorbell_constants = [(f"HOLON_NPU_DOORBELL_{entry['name']}", entry["value"]) for entry in schema["doorbell_bits"]]
    doorbell_constants.append(("HOLON_NPU_DOORBELL_VALID_MASK", schema["doorbell_valid_mask"]))
    irq_constants = [(f"HOLON_NPU_IRQ_{entry['name']}", entry["value"]) for entry in schema["irq_bits"]]
    irq_constants.append(("HOLON_NPU_IRQ_VALID_MASK", schema["irq_valid_mask"]))
    op_class_constants = [
        (f"HOLON_NPU_PROGRAM_OP_CLASS_{entry['name']}", entry["value"])
        for entry in schema["op_classes"]
    ]
    capability_constants = [
        (f"HOLON_NPU_CAP_{entry['name']}", entry["value"])
        for entry in schema["capabilities"]
    ]
    fault_constants = [(f"HOLON_NPU_FAULT_{fault['name']}", fault["value"]) for fault in schema["faults"]]
    completion_status_constants = [
        (f"HOLON_NPU_COMPLETION_STATUS_{status['name']}", status["value"])
        for status in schema["completion_status"]
    ]
    offset_constants = [
        (f"HOLON_NPU_PROGRAM_DESC_OFF_{field['macro_suffix']}", field["offset"])
        for field in descriptor["fields"]
        if "macro_suffix" in field
    ]
    completion_offset_constants = [
        (f"HOLON_NPU_COMPLETION_OFF_{field['macro_suffix']}", field["offset"])
        for field in completion_record["fields"]
    ]

    lines = [
        f"/* {BANNER} */",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        '#include "holon_npu_isa.h"',
        "",
        c_const("uint8_t", "HOLON_NPU_ABI_MAJOR", abi["major"], width=2, pad=42),
        c_const("uint8_t", "HOLON_NPU_ABI_MINOR", abi["minor"], width=2, pad=42),
        c_const("uint32_t", "HOLON_NPU_ABI_VERSION_RESET", abi["version_reset"], pad=42),
        "",
        c_const("uint16_t", "HOLON_NPU_PROGRAM_DESC_SIZE", constants["program_desc_size"], width=4, pad=42),
        c_const("uint32_t", "HOLON_NPU_PROGRAM_DESC_ALIGN", constants["program_desc_align"], pad=42),
        c_const("uint32_t", "HOLON_NPU_PROGRAM_IMAGE_ALIGN", constants["program_image_align"], pad=42),
        c_const("uint32_t", "HOLON_NPU_PROGRAM_ARGUMENT_ALIGN", constants["argument_align"], pad=42),
        c_const("uint32_t", "HOLON_NPU_PROGRAM_COMPLETION_ALIGN", constants["completion_align"], pad=42),
        c_const("uint16_t", "HOLON_NPU_COMPLETION_RECORD_SIZE", constants["completion_record_size"], width=4, pad=42),
        c_const("uint8_t", "HOLON_NPU_PROGRAM_FORMAT_HOLON", constants["program_format_holon"], width=2, pad=42),
        c_const("uint32_t", "HOLON_NPU_PROGRAM_MEM_MAX_BYTES", constants["program_mem_max_bytes"], pad=42),
        c_const("uint32_t", "HOLON_NPU_LOCAL_MEM_MAX_BYTES", constants["local_mem_max_bytes"], pad=42),
        c_const("uint32_t", "HOLON_NPU_PROGRAM_STACK_MAX_BYTES", constants["stack_max_bytes"], pad=42),
        "",
    ]

    for title, consts, type_name, width in (
        ("Register offsets", register_constants, "uint32_t", 3),
        ("Register reset values", register_reset_constants, "uint32_t", 8),
        ("Lifecycle status bits", lifecycle_constants, "uint32_t", 8),
        ("Program descriptor flags", flag_constants, "uint32_t", 8),
        ("Control bits", control_constants, "uint32_t", 8),
        ("Doorbell bits", doorbell_constants, "uint32_t", 8),
        ("IRQ bits", irq_constants, "uint32_t", 8),
        ("Required operation class bits", op_class_constants, "uint64_t", 16),
        ("Capability bits", capability_constants, "uint64_t", 16),
        ("Fault codes", fault_constants, "uint32_t", 2),
        ("Completion status values", completion_status_constants, "uint32_t", 8),
        ("Program descriptor offsets", offset_constants, "uint32_t", 2),
        ("Completion record offsets", completion_offset_constants, "uint32_t", 2),
    ):
        lines.append(f"/* {title}. */")
        pad = max(len(name) for name, _ in consts)
        for name, value in consts:
            lines.append(c_const(type_name, name, value, width=width, pad=pad))
        lines.append("")

    lines.append(f"typedef struct {descriptor['struct_name']} {{")
    for field in descriptor["fields"]:
        lines.append(f"    {field['ctype']} {field['name']};")
    lines.extend([f"}} {descriptor['typedef_name']};", ""])

    lines.append(f"typedef struct {completion_record['struct_name']} {{")
    for field in completion_record["fields"]:
        lines.append(f"    {field['ctype']} {field['name']};")
    lines.extend([f"}} {completion_record['typedef_name']};", ""])

    lines.extend(
        [
            f"static_assert(sizeof({descriptor['typedef_name']}) == HOLON_NPU_PROGRAM_DESC_SIZE);",
            f"static_assert(sizeof({completion_record['typedef_name']}) == HOLON_NPU_COMPLETION_RECORD_SIZE);",
            f"static_assert(HOLON_NPU_ABI_VERSION_RESET == {c_hex(abi['version_reset'])});",
            "static_assert(HOLON_NPU_PROGRAM_FORMAT_HOLON == 0x01u);",
            "static_assert(HOLON_NPU_ISA_MAJOR == 0x01u);",
        ]
    )
    for field in descriptor["fields"]:
        suffix = field.get("macro_suffix")
        if suffix:
            lines.append(
                f"static_assert(offsetof({descriptor['typedef_name']}, {field['name']}) == "
                f"HOLON_NPU_PROGRAM_DESC_OFF_{suffix});"
            )
    for field in completion_record["fields"]:
        lines.append(
            f"static_assert(offsetof({completion_record['typedef_name']}, {field['name']}) == "
            f"HOLON_NPU_COMPLETION_OFF_{field['macro_suffix']});"
        )

    lines.append("")
    return "\n".join(lines)


def generated_sv_package(schema: dict[str, Any]) -> str:
    abi = schema["abi"]
    constants = schema["constants"]
    descriptor = schema["descriptor"]
    completion_record = schema["completion_record"]
    registers = schema["registers"]

    register_offsets = [(f"NPU_REG_{reg['name']}", reg["offset"]) for reg in registers]
    register_resets = [(f"NPU_RESET_{reg['name']}", reg["reset"]) for reg in registers]
    lifecycle_constants = [
        (f"NPU_STATUS_{state['name']}", state["value"])
        for state in schema["lifecycle_states"]
    ]
    flag_constants = [(f"NPU_PROGRAM_FLAG_{flag['name']}", flag["value"]) for flag in schema["flags"]]
    flag_constants.append(("NPU_PROGRAM_FLAG_VALID_MASK", schema["flags_valid_mask"]))
    control_constants = [(f"NPU_CONTROL_{entry['name']}", entry["value"]) for entry in schema["control_bits"]]
    control_constants.append(("NPU_CONTROL_VALID_MASK", schema["control_valid_mask"]))
    doorbell_constants = [(f"NPU_DOORBELL_{entry['name']}", entry["value"]) for entry in schema["doorbell_bits"]]
    doorbell_constants.append(("NPU_DOORBELL_VALID_MASK", schema["doorbell_valid_mask"]))
    irq_constants = [(f"NPU_IRQ_{entry['name']}", entry["value"]) for entry in schema["irq_bits"]]
    irq_constants.append(("NPU_IRQ_VALID_MASK", schema["irq_valid_mask"]))
    op_class_constants = [
        (f"NPU_OP_CLASS_{entry['name']}", entry["value"])
        for entry in schema["op_classes"]
    ]
    capability_constants = [
        (f"NPU_CAP_{entry['name']}", entry["value"])
        for entry in schema["capabilities"]
    ]
    fault_constants = [(f"NPU_FAULT_{fault['name']}", fault["value"]) for fault in schema["faults"]]
    completion_status_constants = [
        (f"NPU_COMPLETION_STATUS_{status['name']}", status["value"])
        for status in schema["completion_status"]
    ]
    offset_constants = [
        (f"NPU_PROGRAM_DESC_OFF_{field['macro_suffix']}", field["offset"])
        for field in descriptor["fields"]
        if "macro_suffix" in field
    ]
    completion_offset_constants = [
        (f"NPU_COMPLETION_OFF_{field['macro_suffix']}", field["offset"])
        for field in completion_record["fields"]
    ]

    lines = [
        f"// {BANNER}",
        "/* verilator lint_off UNUSEDPARAM */",
        "package npu_pkg;",
        sv_const_int("NPU_ABI_MAJOR", abi["major"], pad=34),
        sv_const_int("NPU_ABI_MINOR", abi["minor"], pad=34),
        sv_const_logic(32, "NPU_ABI_VERSION_RESET", abi["version_reset"], pad=34),
        "",
        sv_const_int("NPU_PROGRAM_DESC_SIZE", constants["program_desc_size"], pad=38),
        sv_const_int("NPU_PROGRAM_DESC_ALIGN", constants["program_desc_align"], pad=38),
        sv_const_int("NPU_PROGRAM_IMAGE_ALIGN", constants["program_image_align"], pad=38),
        sv_const_int("NPU_PROGRAM_ARGUMENT_ALIGN", constants["argument_align"], pad=38),
        sv_const_int("NPU_PROGRAM_COMPLETION_ALIGN", constants["completion_align"], pad=38),
        sv_const_int("NPU_COMPLETION_RECORD_SIZE", constants["completion_record_size"], pad=38),
        sv_const_int("NPU_PROGRAM_FORMAT_HOLON", constants["program_format_holon"], pad=38),
        sv_const_int("NPU_PROGRAM_MEM_MAX_BYTES", constants["program_mem_max_bytes"], pad=38),
        sv_const_int("NPU_LOCAL_MEM_MAX_BYTES", constants["local_mem_max_bytes"], pad=38),
        sv_const_int("NPU_PROGRAM_STACK_MAX_BYTES", constants["stack_max_bytes"], pad=38),
        "",
    ]

    for title, consts, bits in (
        ("register offsets", register_offsets, 12),
        ("register reset values", register_resets, 32),
        ("lifecycle status bits", lifecycle_constants, 32),
        ("program flags", flag_constants, 32),
        ("control bits", control_constants, 32),
        ("doorbell bits", doorbell_constants, 32),
        ("IRQ bits", irq_constants, 32),
        ("operation classes", op_class_constants, 64),
        ("capabilities", capability_constants, 64),
        ("fault codes", fault_constants, 32),
        ("completion status values", completion_status_constants, 32),
    ):
        lines.append(f"    // {title}.")
        pad = max(len(name) for name, _ in consts)
        for name, value in consts:
            lines.append(sv_const_logic(bits, name, value, pad=pad))
        lines.append("")

    lines.append("    // Program descriptor offsets.")
    pad = max(len(name) for name, _ in offset_constants)
    for name, value in offset_constants:
        lines.append(sv_const_int(name, value, pad=pad))
    lines.append("")

    lines.append("    // Completion record offsets.")
    pad = max(len(name) for name, _ in completion_offset_constants)
    for name, value in completion_offset_constants:
        lines.append(sv_const_int(name, value, pad=pad))
    lines.append("")

    lines.extend(["endpackage", "/* verilator lint_on UNUSEDPARAM */", ""])
    return "\n".join(lines)


def generated_reference_md(schema: dict[str, Any]) -> str:
    abi = schema["abi"]
    constants = schema["constants"]
    descriptor = schema["descriptor"]
    completion_record = schema["completion_record"]
    lines = [
        f"<!-- {BANNER} -->",
        "# HolonNPU ABI 3.0 Reference",
        "",
        "This file is generated from `spec/holon_npu_abi.json`. Edit the schema",
        "and regenerate outputs instead of editing this file by hand.",
        "",
        "## ABI Version",
        "",
        f"- ABI version: {abi['major']}.{abi['minor']}.",
        f"- Reset value: `{md_hex(abi['version_reset'])}`.",
        f"- Program descriptor size: `{constants['program_desc_size']}` bytes.",
        f"- Program descriptor alignment: `{constants['program_desc_align']}` bytes.",
        f"- Program image alignment: `{constants['program_image_align']}` bytes.",
            f"- Argument/completion alignment: `{constants['argument_align']}` / `{constants['completion_align']}` bytes.",
            f"- Completion record size: `{constants['completion_record_size']}` bytes.",
        "",
        "## ABI Rules",
        "",
    ]
    for rule in abi["rules"]:
        lines.append(f"- {rule}")

    lines.extend(
        [
            "",
            "## Register Map",
            "",
            "| Offset | Name | Access | Reset | Description |",
            "| ------ | ---- | ------ | ----- | ----------- |",
        ]
    )
    for reg in schema["registers"]:
        lines.append(
            f"| `{md_offset(reg['offset'])}` | `{reg['name']}` | `{reg['access']}` | "
            f"`{md_hex(reg['reset'])}` | {reg['description']} |"
        )

    lines.extend(
        [
            "",
            "## Lifecycle Status Bits",
            "",
            "| Name | Value | Description |",
            "| ---- | ----- | ----------- |",
        ]
    )
    for state in schema["lifecycle_states"]:
        lines.append(f"| `{state['name']}` | `{md_hex(state['value'])}` | {state['description']} |")

    lines.extend(
        [
            "",
            "## Program Descriptor Layout",
            "",
            "| Offset | Field | C Type | Required | Description |",
            "| ------ | ----- | ------ | -------- | ----------- |",
        ]
    )
    for field in descriptor["fields"]:
        lines.append(
            f"| `{md_desc_offset(field['offset'])}` | `{field['name']}` | `{field['ctype']}` | "
            f"`{field['required']}` | {field['description']} |"
        )

    lines.extend(
        [
            "",
            "## Completion Record Layout",
            "",
            "| Offset | Field | C Type | Required | Description |",
            "| ------ | ----- | ------ | -------- | ----------- |",
        ]
    )
    for field in completion_record["fields"]:
        lines.append(
            f"| `{md_desc_offset(field['offset'])}` | `{field['name']}` | `{field['ctype']}` | "
            f"`{field['required']}` | {field['description']} |"
        )

    lines.extend(
        [
            "",
            "## Completion Status Values",
            "",
            "| Name | Value | Description |",
            "| ---- | ----- | ----------- |",
        ]
    )
    for status in schema["completion_status"]:
        lines.append(
            f"| `{status['name']}` | `{md_hex(status['value'])}` | {status['description']} |"
        )

    lines.extend(
        [
            "",
            "## Program Flags",
            "",
            "| Name | Value | Description |",
            "| ---- | ----- | ----------- |",
        ]
    )
    for flag in schema["flags"]:
        lines.append(f"| `{flag['name']}` | `{md_hex(flag['value'])}` | {flag['description']} |")
    lines.append(f"| `VALID_MASK` | `{md_hex(schema['flags_valid_mask'])}` | OR of all defined program flags. |")

    for title, key, mask_key in (
        ("Control Bits", "control_bits", "control_valid_mask"),
        ("Doorbell Bits", "doorbell_bits", "doorbell_valid_mask"),
        ("IRQ Bits", "irq_bits", "irq_valid_mask"),
    ):
        lines.extend(
            [
                "",
                f"## {title}",
                "",
                "| Name | Value | Description |",
                "| ---- | ----- | ----------- |",
            ]
        )
        for entry in schema[key]:
            lines.append(f"| `{entry['name']}` | `{md_hex(entry['value'])}` | {entry['description']} |")
        lines.append(f"| `VALID_MASK` | `{md_hex(schema[mask_key])}` | OR of all defined {title.lower()}. |")

    for title, key in (
        ("Required Operation Classes", "op_classes"),
        ("Capability Bits", "capabilities"),
        ("Fault Codes", "faults"),
    ):
        lines.extend(
            [
                "",
                f"## {title}",
                "",
                "| Name | Value | Description |",
                "| ---- | ----- | ----------- |",
            ]
        )
        for entry in schema[key]:
            value_width = 16 if key in {"op_classes", "capabilities"} else 2
            lines.append(f"| `{entry['name']}` | `{md_hex(entry['value'], value_width)}` | {entry['description']} |")

    lines.append("")
    return "\n".join(lines)


def render_all(schema: dict[str, Any]) -> dict[str, str]:
    return {
        "rtl/common/npu_pkg.sv": generated_sv_package(schema),
        "include/holon_npu_program.h": generated_header(schema),
        "docs/INTERFACE_REFERENCE.md": generated_reference_md(schema),
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
            print(f"{rel_path} is not up to date with {ABI_SCHEMA_PATH.relative_to(ROOT)}", file=sys.stderr)
            diff = difflib.unified_diff(
                actual.splitlines(),
                expected.splitlines(),
                fromfile=f"{rel_path} (current)",
                tofile=f"{rel_path} (generated)",
                lineterm="",
            )
            for line in list(diff)[:140]:
                print(line, file=sys.stderr)
    if errors == 0:
        print(f"ABI generated-source check passed ({len(outputs)} outputs).")
    return 1 if errors else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate HolonNPU ABI outputs from schema.")
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
        print(f"ABI generation failed: {exc}", file=sys.stderr)
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
