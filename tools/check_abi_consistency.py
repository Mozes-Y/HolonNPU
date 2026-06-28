#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def parse_sv_params(path: Path) -> dict[str, int]:
    params: dict[str, int] = {}
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(r"localparam\s+(?:int\s+unsigned|logic\s+\[[^\]]+\])\s+(\w+)\s*=\s*([^;]+);")
    for name, raw_value in pattern.findall(text):
        params[name] = parse_int(raw_value.strip())
    enum_pattern = re.compile(r"\b(NPU_ERR_\w+)\s*=\s*([^,\n}]+)")
    for name, raw_value in enum_pattern.findall(text):
        params[name] = parse_int(raw_value.strip())
    return params


def parse_c_defines(path: Path) -> dict[str, int]:
    defines: dict[str, int] = {}
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"#define\s+(\w+)\s+(?:UINT(?:32|64)_C\()?((?:0[xX][0-9a-fA-F_]+)|(?:[0-9][0-9a-fA-F_]*))\)?"
    )
    for name, raw_value in pattern.findall(text):
        defines[name] = parse_int(raw_value)
    return defines


def parse_int(raw_value: str) -> int:
    value = raw_value.replace("_", "")
    sized_match = re.fullmatch(r"(\d+)'([hHdDbB])([0-9a-fA-FxXzZ]+)", value)
    if sized_match:
        base_tag = sized_match.group(2).lower()
        digits = sized_match.group(3).lower().replace("x", "0").replace("z", "0")
        if base_tag == "h":
            return int(digits, 16)
        if base_tag == "d":
            return int(digits, 10)
        if base_tag == "b":
            return int(digits, 2)

    if value.startswith(("0x", "0X")):
        return int(value, 16)
    return int(value, 10)


def check_equal(name: str, sv: dict[str, int], c: dict[str, int], sv_name: str, c_name: str) -> list[str]:
    errors: list[str] = []
    if sv_name not in sv:
        errors.append(f"missing SV constant {sv_name} for {name}")
    if c_name not in c:
        errors.append(f"missing C constant {c_name} for {name}")
    if errors:
        return errors
    if sv[sv_name] != c[c_name]:
        errors.append(f"{name}: {sv_name}={sv[sv_name]:#x}, {c_name}={c[c_name]:#x}")
    return errors


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    sv = parse_sv_params(root / "rtl/common/npu_pkg.sv")
    c_regs = parse_c_defines(root / "include/holon_npu_regs.h")
    c_desc = parse_c_defines(root / "include/holon_npu_desc.h")
    c = {**c_regs, **c_desc}

    pairs = [
        ("ABI major", "NPU_ABI_MAJOR", "HOLON_NPU_ABI_MAJOR"),
        ("ABI minor", "NPU_ABI_MINOR", "HOLON_NPU_ABI_MINOR"),
        ("descriptor size", "NPU_DESC_SIZE_BYTES", "HOLON_NPU_DESC_SIZE"),
        ("descriptor alignment", "NPU_DESC_ALIGN_BYTES", "HOLON_NPU_DESC_ALIGN"),
        ("tensor alignment", "NPU_TENSOR_ALIGN_BYTES", "HOLON_NPU_TENSOR_ALIGN"),
        ("array rows", "NPU_ARRAY_M", "HOLON_NPU_ARRAY_M"),
        ("array cols", "NPU_ARRAY_N", "HOLON_NPU_ARRAY_N"),
        ("input bits", "NPU_INPUT_BITS", "HOLON_NPU_INPUT_BITS"),
        ("acc bits", "NPU_ACC_BITS", "HOLON_NPU_ACC_BITS"),
        ("opcode", "NPU_OPCODE_GEMM_I8I8I32", "HOLON_NPU_OPCODE_GEMM_I8I8I32"),
        ("device id", "NPU_DEVICE_ID_RESET", "HOLON_NPU_DEVICE_ID_RESET"),
        ("ABI version reset", "NPU_ABI_VERSION_RESET", "HOLON_NPU_ABI_VERSION_RESET"),
        ("CAP0 reset", "NPU_CAP0_RESET", "HOLON_NPU_CAP0_RESET"),
        ("CAP1 reset", "NPU_CAP1_RESET", "HOLON_NPU_CAP1_RESET"),
        ("reg DEVICE_ID", "NPU_REG_DEVICE_ID", "HOLON_NPU_REG_DEVICE_ID"),
        ("reg ABI_VERSION", "NPU_REG_ABI_VERSION", "HOLON_NPU_REG_ABI_VERSION"),
        ("reg CAP0", "NPU_REG_CAP0", "HOLON_NPU_REG_CAP0"),
        ("reg CAP1", "NPU_REG_CAP1", "HOLON_NPU_REG_CAP1"),
        ("reg CONTROL", "NPU_REG_CONTROL", "HOLON_NPU_REG_CONTROL"),
        ("reg STATUS", "NPU_REG_STATUS", "HOLON_NPU_REG_STATUS"),
        ("reg ERROR_CODE", "NPU_REG_ERROR_CODE", "HOLON_NPU_REG_ERROR_CODE"),
        ("reg IRQ_ENABLE", "NPU_REG_IRQ_ENABLE", "HOLON_NPU_REG_IRQ_ENABLE"),
        ("reg IRQ_STATUS", "NPU_REG_IRQ_STATUS", "HOLON_NPU_REG_IRQ_STATUS"),
        ("reg DOORBELL", "NPU_REG_DOORBELL", "HOLON_NPU_REG_DOORBELL"),
        ("reg DESC_ADDR_LO", "NPU_REG_DESC_ADDR_LO", "HOLON_NPU_REG_DESC_ADDR_LO"),
        ("reg DESC_ADDR_HI", "NPU_REG_DESC_ADDR_HI", "HOLON_NPU_REG_DESC_ADDR_HI"),
        ("reg CLEAR", "NPU_REG_CLEAR", "HOLON_NPU_REG_CLEAR"),
        ("reg PERF_CYCLES_LO", "NPU_REG_PERF_CYCLES_LO", "HOLON_NPU_REG_PERF_CYCLES_LO"),
        ("reg PERF_CYCLES_HI", "NPU_REG_PERF_CYCLES_HI", "HOLON_NPU_REG_PERF_CYCLES_HI"),
        ("reg PERF_BUSY_LO", "NPU_REG_PERF_BUSY_LO", "HOLON_NPU_REG_PERF_BUSY_LO"),
        ("reg PERF_BUSY_HI", "NPU_REG_PERF_BUSY_HI", "HOLON_NPU_REG_PERF_BUSY_HI"),
        ("reg PERF_DESC_COUNT", "NPU_REG_PERF_DESC_COUNT", "HOLON_NPU_REG_PERF_DESC_COUNT"),
        ("reg PERF_ERROR_COUNT", "NPU_REG_PERF_ERROR_COUNT", "HOLON_NPU_REG_PERF_ERROR_COUNT"),
        ("flag IRQ_ON_DONE", "NPU_DESC_FLAG_IRQ_ON_DONE", "HOLON_NPU_DESC_FLAG_IRQ_ON_DONE"),
        ("flag IRQ_ON_ERROR", "NPU_DESC_FLAG_IRQ_ON_ERROR", "HOLON_NPU_DESC_FLAG_IRQ_ON_ERROR"),
        (
            "flag CLEAR_PERF_ON_START",
            "NPU_DESC_FLAG_CLEAR_PERF_ON_START",
            "HOLON_NPU_DESC_FLAG_CLEAR_PERF_ON_START",
        ),
        ("flag valid mask", "NPU_DESC_FLAG_VALID_MASK", "HOLON_NPU_DESC_FLAG_VALID_MASK"),
        ("ERR_NONE", "NPU_ERR_NONE", "HOLON_NPU_ERR_NONE"),
        ("ERR_INVALID_DESC_VERSION", "NPU_ERR_INVALID_DESC_VERSION", "HOLON_NPU_ERR_INVALID_DESC_VERSION"),
        ("ERR_INVALID_OPCODE", "NPU_ERR_INVALID_OPCODE", "HOLON_NPU_ERR_INVALID_OPCODE"),
        ("ERR_INVALID_DESC_SIZE", "NPU_ERR_INVALID_DESC_SIZE", "HOLON_NPU_ERR_INVALID_DESC_SIZE"),
        ("ERR_INVALID_FLAGS", "NPU_ERR_INVALID_FLAGS", "HOLON_NPU_ERR_INVALID_FLAGS"),
        ("ERR_UNSUPPORTED_ALIGNMENT", "NPU_ERR_UNSUPPORTED_ALIGNMENT", "HOLON_NPU_ERR_UNSUPPORTED_ALIGNMENT"),
        ("ERR_AXI_READ", "NPU_ERR_AXI_READ", "HOLON_NPU_ERR_AXI_READ"),
        ("ERR_AXI_WRITE", "NPU_ERR_AXI_WRITE", "HOLON_NPU_ERR_AXI_WRITE"),
        ("ERR_INTERNAL_PROTOCOL", "NPU_ERR_INTERNAL_PROTOCOL", "HOLON_NPU_ERR_INTERNAL_PROTOCOL"),
        ("ERR_DOORBELL_BUSY", "NPU_ERR_DOORBELL_BUSY", "HOLON_NPU_ERR_DOORBELL_BUSY"),
        ("ERR_RESERVED_NONZERO", "NPU_ERR_RESERVED_NONZERO", "HOLON_NPU_ERR_RESERVED_NONZERO"),
        ("ERR_DIMENSION_ZERO", "NPU_ERR_DIMENSION_ZERO", "HOLON_NPU_ERR_DIMENSION_ZERO"),
        ("ERR_DIMENSION_UNSUPPORTED", "NPU_ERR_DIMENSION_UNSUPPORTED", "HOLON_NPU_ERR_DIMENSION_UNSUPPORTED"),
    ]

    errors: list[str] = []
    for name, sv_name, c_name in pairs:
        errors.extend(check_equal(name, sv, c, sv_name, c_name))

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"ABI consistency check passed ({len(pairs)} constants).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
