#!/usr/bin/env python3
"""Guardrail for interface-native RTL core boundaries."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
RTL_ROOT = REPO_ROOT / "rtl"
SIM_RTL_ROOT = REPO_ROOT / "sim" / "rtl"
CMAKE = REPO_ROOT / "CMakeLists.txt"

ALLOWED_FLATTENED_MODULES = {
    "npu_top",
    "npu_common_smoke_top",
    "npu_systolic_array_test_top",
    "npu_tiling_datapath_test_top",
    "npu_command_processor_test_top",
    "npu_read_dma_test_top",
    "npu_write_dma_test_top",
}

PRODUCT_SOURCE_SETS = {
    "NPU_CONTROL_SOURCES",
    "NPU_DMA_MODULE_SOURCES",
    "NPU_COMMAND_SOURCES",
    "NPU_GEMM_SOURCES",
    "NPU_TOP_SOURCES",
    "NPU_INTEGRATION_SOURCES",
}

EXPECTED_INTERFACE_USERS = {
    "npu_vr_if": {
        "rtl/common/npu_fifo.sv",
        "rtl/common/npu_skid_buffer.sv",
        "rtl/common/npu_register_slice.sv",
        "rtl/dma/npu_axi4_read_dma.sv",
        "rtl/dma/npu_axi4_write_dma.sv",
        "rtl/command/npu_command_processor.sv",
        "rtl/integration/npu_gemm_accelerator.sv",
        "rtl/integration/npu_top.sv",
    },
    "npu_axi4_if": {
        "rtl/dma/npu_axi4_read_dma.sv",
        "rtl/dma/npu_axi4_write_dma.sv",
        "rtl/command/npu_command_processor.sv",
        "rtl/integration/npu_gemm_accelerator.sv",
        "rtl/integration/npu_top.sv",
    },
    "npu_axi_lite_if": {
        "rtl/control/npu_control_regs.sv",
        "rtl/integration/npu_top.sv",
    },
}


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def sv_files() -> list[Path]:
    files = list(RTL_ROOT.rglob("*.sv"))
    if SIM_RTL_ROOT.exists():
        files.extend(SIM_RTL_ROOT.rglob("*.sv"))
    return sorted(files)


def is_core_rtl(path: str) -> bool:
    return path.startswith("rtl/")


def is_sim_harness(path: str) -> bool:
    return path.startswith("sim/rtl/")


def is_harness_file(path: str) -> bool:
    name = Path(path).name
    return (
        name.endswith("_test_wrapper.sv")
        or name.endswith("_test_top.sv")
        or name.endswith("_smoke_top.sv")
    )


def rel(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_modules(text: str) -> list[tuple[str, str]]:
    modules: list[tuple[str, str]] = []
    for match in re.finditer(r"\bmodule\s+([A-Za-z_][A-Za-z0-9_]*)\b", text):
        end = re.search(r"\bendmodule\b", text[match.end() :])
        if end is None:
            continue
        module_text = text[match.start() : match.end() + end.end()]
        modules.append((match.group(1), module_text))
    return modules


def cmake_set_body(name: str, text: str) -> str:
    match = re.search(rf"set\({re.escape(name)}\n(.*?)\n\)", text, re.S)
    if match is None:
        return ""
    return match.group(1)


def main() -> int:
    errors: list[str] = []
    files = {rel(path): read(path) for path in sv_files()}

    for interface_name, expected_paths in EXPECTED_INTERFACE_USERS.items():
        definition = f"rtl/common/{interface_name}.sv"
        actual_paths = {
            path
            for path, text in files.items()
            if path != definition and re.search(rf"\b{re.escape(interface_name)}\b", text)
        }
        missing = sorted(expected_paths - actual_paths)
        for path in missing:
            fail(errors, f"{interface_name} is not used in expected core file {path}")

    flattened_bus = re.compile(r"\b(?:m_axi|s_axil)_[A-Za-z0-9_]+_[io]\b")
    wrapper_ref = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*_test_wrapper\b")

    for path, text in files.items():
        if is_core_rtl(path) and is_harness_file(path):
            fail(errors, f"{path} is a test-only RTL harness under rtl/")
        if is_sim_harness(path) and not is_harness_file(path):
            fail(errors, f"{path} is under sim/rtl but is not named as a test harness")

        for module_name, module_text in extract_modules(text):
            is_test_wrapper = module_name.endswith("_test_wrapper")
            is_test_top = module_name.endswith("_test_top")
            is_allowed_boundary = module_name in ALLOWED_FLATTENED_MODULES

            if not (is_test_wrapper or is_test_top or is_allowed_boundary):
                flattened = flattened_bus.search(module_text)
                if flattened is not None:
                    fail(
                        errors,
                        f"{path}:{module_name} declares flattened bus signal "
                        f"{flattened.group(0)}; core RTL must use interfaces",
                    )

            if not (is_test_wrapper or is_test_top):
                refs = sorted(set(wrapper_ref.findall(module_text)))
                refs = [ref for ref in refs if ref != module_name]
                if refs:
                    fail(
                        errors,
                        f"{path}:{module_name} references test wrapper(s): "
                        f"{', '.join(refs)}",
                    )

    cmake = read(CMAKE)
    for set_name in PRODUCT_SOURCE_SETS:
        body = cmake_set_body(set_name, cmake)
        if "sim/rtl/" in body:
            fail(errors, f"{set_name} includes a sim/rtl test harness source")
        for suffix in ("_test_wrapper.sv", "_test_top.sv", "_smoke_top.sv"):
            if suffix in body:
                fail(errors, f"{set_name} includes a {suffix} source")

    harness_sources = sorted(path for path in files if is_sim_harness(path))
    for path in harness_sources:
        occurrences = [
            name
            for name in re.findall(r"set\((NPU_[A-Za-z0-9_]+)\n(.*?)\n\)", cmake, re.S)
            if path in name[1]
        ]
        set_names = [name for name, _ in occurrences]
        if not set_names:
            fail(errors, f"{path} is not referenced by any CMake source set")
        if not any("_TEST_SOURCES" in name or "_LINT_SOURCES" in name for name in set_names):
            fail(errors, f"{path} is not referenced by a test/lint CMake source set")

    if errors:
        print("RTL interface usage check failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("RTL interface usage check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
