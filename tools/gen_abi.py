#!/usr/bin/env python3
from __future__ import annotations

import argparse
import difflib
import json
import sys
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "spec/holon_npu_abi.json"

BANNER = "Generated from spec/holon_npu_abi.json by tools/gen_abi.py. Do not edit."


def load_schema(path: Path = SCHEMA_PATH) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        schema = json.load(f)
    if schema.get("schema_version") != 1:
        raise ValueError("unsupported ABI schema_version")
    return schema


def as_int(value: int | str) -> int:
    if isinstance(value, int):
        return value
    value = value.replace("_", "")
    if value.startswith(("0x", "0X")):
        return int(value, 16)
    return int(value, 10)


def c_hex(value: int | str, width: int = 8) -> str:
    return f"0x{as_int(value):0{width}X}u"


def c_const(type_name: str, name: str, value: int | str, width: int = 8, pad: int = 0) -> str:
    spacer = " " * max(pad - len(name), 1)
    return f"static constexpr {type_name} {name}{spacer}= {c_hex(value, width)};"


def sv_u32(value: int | str) -> str:
    raw = f"{as_int(value):08X}"
    return f"32'h{raw[:4]}_{raw[4:]}"


def sv_reg_offset(value: int | str) -> str:
    return f"12'h{as_int(value):03X}"


def md_hex32(value: int | str) -> str:
    return f"0x{as_int(value):08X}"


def md_offset(value: int | str) -> str:
    return f"0x{as_int(value):03X}"


def md_desc_offset(value: int | str) -> str:
    return f"0x{as_int(value):02X}"


def generated_sv(schema: dict[str, Any]) -> str:
    abi = schema["abi"]
    constants = schema["constants"]
    caps = schema["capabilities"]
    desc = schema["descriptor"]
    command = schema["internal_command"]

    lines: list[str] = [
        f"// {BANNER}",
        "/* verilator lint_off UNUSEDPARAM */",
        "package npu_pkg;",
        f"    localparam int unsigned NPU_ABI_MAJOR = {abi['major']};",
        f"    localparam int unsigned NPU_ABI_MINOR = {abi['minor']};",
        "",
        f"    localparam int unsigned NPU_DESC_SIZE_BYTES = {constants['desc_size']};",
        f"    localparam int unsigned NPU_DESC_ALIGN_BYTES = {constants['desc_align']};",
        f"    localparam int unsigned NPU_TENSOR_ALIGN_BYTES = {constants['tensor_align']};",
        "",
        f"    localparam int unsigned NPU_ARRAY_K = {constants['array_k']};",
        f"    localparam int unsigned NPU_ARRAY_N = {constants['array_n']};",
        f"    localparam int unsigned NPU_INPUT_BITS = {constants['input_bits']};",
        f"    localparam int unsigned NPU_ACC_BITS = {constants['acc_bits']};",
        "",
    ]

    for opcode in schema["opcodes"]:
        lines.append(
            f"    localparam int unsigned NPU_OPCODE_{opcode['name']} = {opcode['value']};"
        )

    lines.extend(
        [
            "",
            f"    localparam logic [31:0] NPU_DEVICE_ID_RESET = {sv_u32(caps['device_id_reset'])};",
            f"    localparam logic [31:0] NPU_ABI_VERSION_RESET = {sv_u32(abi['version_reset'])};",
            f"    localparam logic [31:0] NPU_CAP0_RESET = {sv_u32(caps['cap0_reset'])};",
            f"    localparam logic [31:0] NPU_CAP1_RESET = {sv_u32(caps['cap1_reset'])};",
            "",
        ]
    )

    reg_name_width = max(len(f"NPU_REG_{reg['name']}") for reg in schema["registers"])
    for reg in schema["registers"]:
        name = f"NPU_REG_{reg['name']}"
        lines.append(
            f"    localparam logic [11:0] {name:<{reg_name_width}} = {sv_reg_offset(reg['offset'])};"
        )

    lines.append("")
    for field in desc["fields"]:
        suffix = field.get("macro_suffix")
        if suffix:
            lines.append(
                f"    localparam int unsigned NPU_DESC_OFF_{suffix} = {as_int(field['offset'])};"
            )

    for flag in desc["flags"]:
        lines.append(
            f"    localparam logic [31:0] NPU_DESC_FLAG_{flag['name']} = {sv_u32(flag['value'])};"
        )
    lines.append(
        f"    localparam logic [31:0] NPU_DESC_FLAG_VALID_MASK = {sv_u32(desc['flags_valid_mask'])};"
    )
    lines.append("")

    for field in command["fields"]:
        lines.append(
            f"    localparam int unsigned NPU_GEMM_CMD_{field['name']} = {field['value']};"
        )
    lines.append(f"    localparam int unsigned NPU_GEMM_CMD_W = {command['width']};")
    lines.append("")

    error_name_width = max(len(f"NPU_ERR_{err['name']}") for err in schema["errors"])
    lines.append("    typedef enum logic [3:0] {")
    for index, err in enumerate(schema["errors"]):
        comma = "," if index + 1 < len(schema["errors"]) else ""
        name = f"NPU_ERR_{err['name']}"
        lines.append(f"        {name:<{error_name_width}} = 4'd{err['value']}{comma}")
    lines.extend(["    } npu_error_e;", "", "endpackage", "/* verilator lint_on UNUSEDPARAM */", ""])
    return "\n".join(lines)


def generated_regs_h(schema: dict[str, Any]) -> str:
    abi = schema["abi"]
    constants = schema["constants"]
    caps = schema["capabilities"]
    non_reserved_regs = [reg for reg in schema["registers"] if not reg["name"].startswith("RESERVED_")]

    lines = [
        f"/* {BANNER} */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        c_const("uint32_t", "HOLON_NPU_DEVICE_ID_RESET", caps["device_id_reset"], pad=36),
        c_const("uint32_t", "HOLON_NPU_ABI_VERSION_RESET", abi["version_reset"], pad=36),
        c_const("uint32_t", "HOLON_NPU_CAP0_RESET", caps["cap0_reset"], pad=36),
        c_const("uint32_t", "HOLON_NPU_CAP1_RESET", caps["cap1_reset"], pad=36),
        "",
        c_const("uint8_t", "HOLON_NPU_ABI_MAJOR", abi["major"], width=2, pad=36),
        c_const("uint8_t", "HOLON_NPU_ABI_MINOR", abi["minor"], width=2, pad=36),
        c_const("uint16_t", "HOLON_NPU_DESC_SIZE", constants["desc_size"], width=4, pad=36),
        c_const("uint32_t", "HOLON_NPU_DESC_ALIGN", constants["desc_align"], pad=36),
        c_const("uint32_t", "HOLON_NPU_TENSOR_ALIGN", constants["tensor_align"], pad=36),
        c_const("uint16_t", "HOLON_NPU_ARRAY_K", constants["array_k"], width=4, pad=36),
        c_const("uint16_t", "HOLON_NPU_ARRAY_N", constants["array_n"], width=4, pad=36),
        c_const("uint16_t", "HOLON_NPU_INPUT_BITS", constants["input_bits"], width=4, pad=36),
        c_const("uint16_t", "HOLON_NPU_ACC_BITS", constants["acc_bits"], width=4, pad=36),
        "",
    ]

    macro_width = max(len(f"HOLON_NPU_REG_{reg['name']}") for reg in non_reserved_regs)
    for reg in non_reserved_regs:
        name = f"HOLON_NPU_REG_{reg['name']}"
        lines.append(c_const("uint32_t", name, reg["offset"], width=2, pad=macro_width))

    field_macros: list[tuple[str, str]] = []
    for reg in schema["registers"]:
        for field in reg.get("fields", []):
            if "macro" in field:
                field_macros.append((f"HOLON_NPU_{field['macro']}", field["value"]))
        if "valid_mask_macro" in reg:
            field_macros.append((f"HOLON_NPU_{reg['valid_mask_macro']}", reg["valid_mask"]))

    lines.append("")
    field_width = max(len(name) for name, _ in field_macros)
    for name, value in field_macros:
        lines.append(c_const("uint32_t", name, value, pad=field_width))

    lines.append("")
    err_width = max(len(f"HOLON_NPU_ERR_{err['name']}") for err in schema["errors"])
    for err in schema["errors"]:
        name = f"HOLON_NPU_ERR_{err['name']}"
        lines.append(c_const("uint32_t", name, err["value"], width=2, pad=err_width))

    lines.append("")
    return "\n".join(lines)


def generated_desc_h(schema: dict[str, Any]) -> str:
    desc = schema["descriptor"]
    constants = schema["constants"]
    opcode = schema["opcodes"][0]

    lines = [
        f"/* {BANNER} */",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        '#include "holon_npu_regs.h"',
        "",
        c_const("uint8_t", f"HOLON_NPU_OPCODE_{opcode['name']}", opcode["value"], width=2),
        "",
    ]

    flag_names = [(f"HOLON_NPU_DESC_FLAG_{flag['name']}", flag["value"]) for flag in desc["flags"]]
    flag_names.append(("HOLON_NPU_DESC_FLAG_VALID_MASK", desc["flags_valid_mask"]))
    flag_width = max(len(name) for name, _ in flag_names)
    for name, value in flag_names:
        lines.append(c_const("uint32_t", name, value, pad=flag_width))

    lines.append("")
    offset_macros: list[tuple[str, str]] = []
    for field in desc["fields"]:
        suffix = field.get("macro_suffix")
        if suffix:
            offset_macros.append((f"HOLON_NPU_DESC_OFF_{suffix}", field["offset"]))
    off_width = max(len(name) for name, _ in offset_macros)
    for name, value in offset_macros:
        lines.append(c_const("uint32_t", name, value, width=2, pad=off_width))

    lines.extend(["", f"typedef struct {desc['struct_name']} {{"])
    for field in desc["fields"]:
        lines.append(f"    {field['ctype']} {field['name']};")
    lines.extend([f"}} {desc['typedef_name']};", ""])

    config_fields = [
        ("uint32_t", "m"),
        ("uint32_t", "n"),
        ("uint32_t", "k"),
        ("uint32_t", "flags"),
        ("uint64_t", "a_addr"),
        ("uint64_t", "b_addr"),
        ("uint64_t", "c_addr"),
        ("uint32_t", "a_row_stride_bytes"),
        ("uint32_t", "b_row_stride_bytes"),
        ("uint32_t", "c_row_stride_bytes"),
    ]
    lines.append("typedef struct holon_npu_gemm_config {")
    for ctype, name in config_fields:
        lines.append(f"    {ctype} {name};")
    lines.extend([f"}} {desc['config_typedef_name']};", ""])

    lines.extend(
        [
            "static_assert(sizeof(holon_npu_gemm_desc_t) == HOLON_NPU_DESC_SIZE,",
            '              "holon_npu_gemm_desc_t must be 128 bytes");',
        ]
    )

    for field in desc["fields"]:
        suffix = field.get("macro_suffix")
        expected = (
            f"HOLON_NPU_DESC_OFF_{suffix}" if suffix else f"0x{as_int(field['offset']):02X}"
        )
        lines.extend(
            [
                f"static_assert(offsetof(holon_npu_gemm_desc_t, {field['name']}) == {expected},",
                f'              "descriptor {field["name"]} offset mismatch");',
            ]
        )

    lines.append("")
    return "\n".join(lines)


def field_rows(schema: dict[str, Any], reg: dict[str, Any]) -> list[dict[str, Any]]:
    if "fields_ref" in reg:
        return schema["capabilities"][reg["fields_ref"]]
    return reg.get("fields", [])


def generated_interface_md(schema: dict[str, Any]) -> str:
    abi = schema["abi"]
    constants = schema["constants"]
    desc = schema["descriptor"]
    opcode = schema["opcodes"][0]

    lines = [
        f"<!-- {BANNER} -->",
        "# HolonNPU Interface Specification",
        "",
        "This document defines the software-visible ABI, register map, descriptor",
        "layout, and protocol-level software contract for HolonNPU. The tables in",
        "this file are generated from `spec/holon_npu_abi.json`; edit the schema",
        "and regenerate outputs instead of editing this file by hand.",
        "",
        "## ABI Rules",
        "",
        f"- ABI version: {abi['major']}.{abi['minor']}.",
    ]
    lines.extend(f"- {rule}" for rule in abi["rules"])

    lines.extend(
        [
            "",
            "## Constants",
            "",
            "| Name | Value | Description |",
            "| ---- | ----- | ----------- |",
            f"| `HOLON_NPU_ABI_MAJOR` | `{abi['major']}` | Major ABI version. |",
            f"| `HOLON_NPU_ABI_MINOR` | `{abi['minor']}` | Minor ABI version. |",
            f"| `HOLON_NPU_DESC_SIZE` | `{constants['desc_size']}` | GEMM descriptor size in bytes. |",
            f"| `HOLON_NPU_DESC_ALIGN` | `{constants['desc_align']}` | Descriptor base alignment in bytes. |",
            f"| `HOLON_NPU_TENSOR_ALIGN` | `{constants['tensor_align']}` | Tensor base and row-stride alignment. |",
            f"| `HOLON_NPU_OPCODE_{opcode['name']}` | `{opcode['value']}` | {opcode['description']} |",
            f"| `HOLON_NPU_ARRAY_K` | `{constants['array_k']}` | v1.1 stationary B-weight/K lanes. |",
            f"| `HOLON_NPU_ARRAY_N` | `{constants['array_n']}` | v1.1 systolic-array columns. |",
            f"| `HOLON_NPU_INPUT_BITS` | `{constants['input_bits']}` | A and B operand width. |",
            f"| `HOLON_NPU_ACC_BITS` | `{constants['acc_bits']}` | Accumulator and output width. |",
            "",
            "## AXI-Lite Control Interface",
            "",
            "### Protocol",
            "",
            "- Address width: at least 12 bits for the v1 4 KiB register aperture.",
            "- Data width: 32 bits.",
            "- Write strobes: byte strobes are supported.",
            "- AW and W may arrive in the same cycle or on independent cycles.",
            "  Hardware pairs one accepted address with one accepted data beat before",
            "  issuing `B`.",
            "- Supported responses: `OKAY` and `SLVERR`.",
            "- Writes to read-only registers have no state side effect and return `SLVERR`.",
            "- Reads from write-only registers return zero and `OKAY`.",
            "- Unmapped reads and writes return `SLVERR`.",
            "",
            "### Register Map",
            "",
            "| Offset | Name | Width | Access | Reset | Description |",
            "| ------ | ---- | ----- | ------ | ----- | ----------- |",
        ]
    )
    for reg in schema["registers"]:
        name = reg.get("doc_name", reg["name"])
        lines.append(
            f"| `{md_offset(reg['offset'])}` | `{name}` | {reg['width']} | {reg['access']} | "
            f"`{md_hex32(reg['reset'])}` | {reg['description']} |"
        )

    lines.extend(
        [
            "",
            "### Register Fields",
            "",
            "| Register | Bits | Name | Reset | Description |",
            "| -------- | ---- | ---- | ----- | ----------- |",
        ]
    )
    for reg in schema["registers"]:
        for field in field_rows(schema, reg):
            name = reg.get("doc_name", reg["name"])
            lines.append(
                f"| `{name}` | `{field['bits']}` | `{field['name']}` | `{field['reset']}` | "
                f"{field['description']} |"
            )

    lines.extend(
        [
            "",
            "### Register Side Effects",
            "",
            "- `DOORBELL.START=1` is accepted only when `STATUS.BUSY=0`.",
            "- A valid doorbell write clears `STATUS.DONE`, clears `STATUS.ERROR`,",
            "  clears `ERROR_CODE`, and starts descriptor fetch.",
            "- A doorbell write while busy returns `SLVERR` and has no state side effect.",
            "- A doorbell write with reserved bits set returns `SLVERR` and has no state side effect.",
            "- `CONTROL.SOFT_RESET=1` returns the control plane to reset state and cancels",
            "  any active descriptor. The write returns `OKAY`.",
            "- `CLEAR.DONE=1` clears `STATUS.DONE` and `IRQ_STATUS.DONE_IRQ`.",
            "- `CLEAR.ERROR=1` clears `STATUS.ERROR`, `ERROR_CODE`, and `IRQ_STATUS.ERROR_IRQ`.",
            "- `CLEAR.PERF=1` clears all performance counters.",
            "- `PERF_CYCLES` increments while a descriptor is architecturally in flight.",
            "- `PERF_BUSY_CYCLES` increments on in-flight cycles where the backend reports active work.",
            "- `irq_o` is asserted when `(IRQ_ENABLE & IRQ_STATUS) != 0`.",
            "",
            "## Status And Error Codes",
            "",
            "`STATUS` is bit-based, not an enum. Legal terminal combinations are:",
            "",
            "- Idle: `IDLE=1`, `BUSY=0`, `DONE=0`, `ERROR=0`.",
            "- Busy: `IDLE=0`, `BUSY=1`, `DONE=0`, `ERROR=0`.",
            "- Done: `IDLE=1`, `BUSY=0`, `DONE=1`, `ERROR=0`.",
            "- Error: `IDLE=1`, `BUSY=0`, `DONE=0`, `ERROR=1`.",
            "",
            "| Code | Name | Description |",
            "| ---- | ---- | ----------- |",
        ]
    )
    for err in schema["errors"]:
        lines.append(f"| `{err['value']}` | `ERR_{err['name']}` | {err['description']} |")

    lines.extend(
        [
            "",
            "## AXI4 Master Interface",
            "",
            "### Protocol",
            "",
            "- Address width: 64 bits.",
            "- Data width: 128 bits.",
            "- Burst type: `INCR` only.",
            "- Burst length: 1 to 16 beats.",
            "- Maximum burst payload: 256 bytes.",
            "- Outstanding reads: 1.",
            "- Outstanding writes: 1.",
            "- Descriptor fetch: one aligned 128-byte read.",
            "- Tensor reads and writes are generated as aligned 16-byte beat accesses.",
            "- Phase 7 DMA requests must use a 16-byte aligned base address and a nonzero",
            "  byte count that is a multiple of 16 bytes.",
            "- Requests that violate alignment or size constraints fail before issuing AXI",
            "  traffic and report `ERR_UNSUPPORTED_ALIGNMENT`.",
            "",
            "### Response Mapping",
            "",
            "| AXI Response | Handling |",
            "| ------------ | -------- |",
            "| `OKAY` | Continue. |",
            "| `EXOKAY` | Treat as `OKAY`; exclusive access is not generated. |",
            "| `SLVERR` | Set `ERR_AXI_READ` or `ERR_AXI_WRITE`. |",
            "| `DECERR` | Set `ERR_AXI_READ` or `ERR_AXI_WRITE`. |",
            "",
            "## GEMM Descriptor ABI",
            "",
            "The command processor fetches exactly one 128-byte descriptor from",
            "`DESC_ADDR_HI:DESC_ADDR_LO` after a valid doorbell write.",
            "",
            "### Descriptor Layout",
            "",
            "| Byte Offset | Field | Width | Required Value | Description |",
            "| ----------- | ----- | ----- | -------------- | ----------- |",
        ]
    )
    for field in desc["fields"]:
        lines.append(
            f"| `{md_desc_offset(field['offset'])}` | `{field['name']}` | {field['width']} | "
            f"`{field['required']}` | {field['description']} |"
        )

    lines.extend(
        [
            "",
            "### Descriptor Flags",
            "",
            "| Bit | Name | Reset/Required | Description |",
            "| --- | ---- | -------------- | ----------- |",
        ]
    )
    for flag in desc["flags"]:
        lines.append(
            f"| `[{flag['bit']}]` | `{flag['name']}` | Optional | {flag['description']} |"
        )
    lines.append("| `[31:3]` | `RESERVED` | `0` | Nonzero value raises `ERR_INVALID_FLAGS`. |")

    lines.extend(
        [
            "",
            "### GEMM Semantics",
            "",
            "```text",
            "C[m,n] = sum(k: 0..K-1) int32(A[m,k]) * int32(B[k,n])",
            "```",
            "",
            "- A and B elements are signed INT8.",
            "- C elements are signed INT32.",
            "- Accumulation uses signed INT32 wraparound semantics matching two's-complement hardware arithmetic.",
            "- A, B, and C are row-major.",
            "- HolonNPU v1.5 uses a B-weight-stationary systolic array with streaming partial sums.",
            "- v1 does not add bias, scaling, activation, transposition, saturation, or accumulation with an existing C matrix.",
            "- Non-multiple tile dimensions are valid; inactive M, N, and K lanes are masked.",
            "",
            "## Interrupt Semantics",
            "",
            "- `IRQ_STATUS.DONE_IRQ` is set only if descriptor flag `IRQ_ON_DONE` is set.",
            "- `IRQ_STATUS.ERROR_IRQ` is set if descriptor flag `IRQ_ON_ERROR` is set.",
            "- If an error occurs before descriptor flags are available, `ERROR_IRQ` is set.",
            "- The external interrupt line is level-sensitive and asserted while any enabled IRQ status bit is set.",
            "- Software clears interrupt causes with `IRQ_STATUS` write-one-to-clear or with matching `CLEAR` bits.",
            "",
            "## Software API Contract",
            "",
            "| Function | Purpose |",
            "| -------- | ------- |",
            "| `holon_npu_init(base)` | Bind a driver instance to an MMIO base pointer. |",
            "| `holon_npu_get_caps(dev, caps)` | Read `DEVICE_ID`, `ABI_VERSION`, `CAP0`, and `CAP1`. |",
            "| `holon_npu_build_gemm_desc(desc, cfg)` | Fill a 128-byte GEMM descriptor and zero reserved fields. |",
            "| `holon_npu_submit(dev, desc_pa)` | Write descriptor address and doorbell. |",
            "| `holon_npu_poll(dev)` | Read `STATUS` once and return decoded state. |",
            "| `holon_npu_wait(dev, timeout)` | Poll until done, error, or timeout. |",
            "| `holon_npu_error(dev)` | Read `ERROR_CODE`. |",
            "| `holon_npu_clear(dev, mask)` | Clear done, error, or performance counters. |",
            "| `holon_npu_read_perf(dev, perf)` | Read performance counters for software diagnostics. |",
            "",
            "The driver must not submit a descriptor while `STATUS.BUSY=1`. The driver",
            "must align descriptor and tensor addresses according to this ABI or return an",
            "argument error before touching hardware.",
            "",
        ]
    )
    return "\n".join(lines)


def render_all(schema: dict[str, Any]) -> dict[str, str]:
    return {
        "rtl/common/npu_pkg.sv": generated_sv(schema),
        "include/holon_npu_regs.h": generated_regs_h(schema),
        "include/holon_npu_desc.h": generated_desc_h(schema),
        "docs/INTERFACE.md": generated_interface_md(schema),
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
            print(f"{rel_path} is not up to date with {SCHEMA_PATH.relative_to(ROOT)}", file=sys.stderr)
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

    schema = load_schema()
    outputs = render_all(schema)

    if args.check:
        return check_outputs(outputs, args.output_root)

    write_outputs(outputs, args.output_root)
    for rel_path in outputs:
        print(rel_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
