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
    "npu_v2_top",
    "npu_common_smoke_top",
    "npu_systolic_array_test_top",
    "npu_tiling_datapath_test_top",
    "npu_command_processor_test_top",
    "npu_read_dma_test_top",
    "npu_write_dma_test_top",
}

INTERFACE_DEFINITIONS = {
    "npu_vr_if": "rtl/common/npu_vr_if.sv",
    "npu_axi4_if": "rtl/common/npu_axi4_if.sv",
    "npu_axi_lite_if": "rtl/common/npu_axi_lite_if.sv",
    "npu_frontend_if": "rtl/frontend/npu_frontend_if.sv",
    "npu_v2_localmem_rd_if": "rtl/localmem/npu_v2_localmem_rd_if.sv",
    "npu_v2_localmem_wr_if": "rtl/localmem/npu_v2_localmem_wr_if.sv",
}

EXPECTED_INTERFACE_USERS = {
    "npu_vr_if": {
        "rtl/common/npu_fifo.sv",
        "rtl/common/npu_skid_buffer.sv",
        "rtl/common/npu_register_slice.sv",
        "rtl/dma/npu_axi4_read_dma.sv",
        "rtl/dma/npu_axi4_write_dma.sv",
        "rtl/command/npu_command_processor.sv",
        "rtl/integration/npu_v2_frontend_tile.sv",
        "rtl/integration/npu_gemm_accelerator.sv",
        "rtl/integration/npu_top.sv",
        "rtl/matrix/npu_v2_matrix_engine.sv",
        "rtl/vector/npu_v2_vector_engine.sv",
    },
    "npu_axi4_if": {
        "rtl/control/npu_v2_program_loader.sv",
        "rtl/control/npu_v2_completion_writer.sv",
        "rtl/dma/npu_v2_dma_fabric.sv",
        "rtl/dma/npu_axi4_read_dma.sv",
        "rtl/dma/npu_axi4_write_dma.sv",
        "rtl/command/npu_command_processor.sv",
        "rtl/integration/npu_v2_axi_write_arbiter.sv",
        "rtl/integration/npu_v2_control_plane.sv",
        "rtl/integration/npu_v2_frontend_tile.sv",
        "rtl/integration/npu_v2_top.sv",
        "rtl/integration/npu_gemm_accelerator.sv",
        "rtl/integration/npu_top.sv",
    },
    "npu_axi_lite_if": {
        "rtl/control/npu_control_regs.sv",
        "rtl/control/npu_v2_control_regs.sv",
        "rtl/integration/npu_v2_control_plane.sv",
        "rtl/integration/npu_v2_frontend_tile.sv",
        "rtl/integration/npu_v2_top.sv",
        "rtl/integration/npu_top.sv",
    },
    "npu_frontend_if": {
        "rtl/dma/npu_v2_dma_fabric.sv",
        "rtl/frontend/npu_reference_frontend.sv",
        "rtl/integration/npu_v2_frontend_tile.sv",
    },
    "npu_v2_localmem_rd_if": {
        "rtl/dma/npu_v2_dma_fabric.sv",
        "rtl/frontend/npu_reference_frontend.sv",
        "rtl/integration/npu_v2_control_plane.sv",
        "rtl/integration/npu_v2_frontend_tile.sv",
        "rtl/localmem/npu_v2_data_port_arbiter.sv",
        "rtl/localmem/npu_v2_engine_data_arbiter.sv",
        "rtl/localmem/npu_v2_local_memory.sv",
        "rtl/matrix/npu_v2_matrix_engine.sv",
        "rtl/vector/npu_v2_vector_engine.sv",
    },
    "npu_v2_localmem_wr_if": {
        "rtl/control/npu_v2_program_loader.sv",
        "rtl/dma/npu_v2_dma_fabric.sv",
        "rtl/frontend/npu_reference_frontend.sv",
        "rtl/integration/npu_v2_control_plane.sv",
        "rtl/integration/npu_v2_frontend_tile.sv",
        "rtl/localmem/npu_v2_data_port_arbiter.sv",
        "rtl/localmem/npu_v2_engine_data_arbiter.sv",
        "rtl/localmem/npu_v2_local_memory.sv",
        "rtl/matrix/npu_v2_matrix_engine.sv",
        "rtl/vector/npu_v2_vector_engine.sv",
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


def cmake_sv_source_targets(text: str) -> tuple[dict[str, set[str]], list[str]]:
    targets: dict[str, set[str]] = {}
    duplicates: list[str] = []
    pattern = re.compile(
        r"\bholon_npu_add_sv_source_target\s*\(\s*([A-Za-z0-9_]+)\s*(.*?)\)",
        re.S,
    )
    for match in pattern.finditer(text):
        name = match.group(1)
        if name in targets:
            duplicates.append(name)
        targets[name] = set(re.findall(r"[A-Za-z0-9_./-]+\.sv", match.group(2)))
    return targets, duplicates


def main() -> int:
    errors: list[str] = []
    files = {rel(path): read(path) for path in sv_files()}

    for interface_name, expected_paths in EXPECTED_INTERFACE_USERS.items():
        definition = INTERFACE_DEFINITIONS[interface_name]
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
    source_targets, duplicate_targets = cmake_sv_source_targets(cmake)
    for name in duplicate_targets:
        fail(errors, f"CMake source target {name} is declared more than once")

    for target_name, sources in source_targets.items():
        if len(re.findall(rf"\b{re.escape(target_name)}\b", cmake)) < 2:
            fail(errors, f"SystemVerilog source target {target_name} is declared but never consumed")
        if target_name.startswith("holon_npu_rtl_"):
            invalid = sorted(source for source in sources if not is_core_rtl(source))
        elif target_name.startswith("holon_npu_sim_"):
            invalid = sorted(source for source in sources if not is_sim_harness(source))
        else:
            fail(errors, f"unclassified SystemVerilog source target {target_name}")
            invalid = []
        for source in invalid:
            fail(errors, f"{target_name} owns source outside its directory boundary: {source}")

    for path in sorted(files):
        owners = sorted(name for name, sources in source_targets.items() if path in sources)
        expected_prefix = "holon_npu_rtl_" if is_core_rtl(path) else "holon_npu_sim_"
        if len(owners) != 1:
            owner_text = ", ".join(owners) if owners else "none"
            fail(errors, f"{path} must have exactly one CMake source owner; found {owner_text}")
        elif not owners[0].startswith(expected_prefix):
            fail(errors, f"{path} is owned by wrong target class {owners[0]}")

    for target_name, sources in source_targets.items():
        for source in sorted(sources):
            if source not in files:
                fail(errors, f"{target_name} references missing SystemVerilog source {source}")

    if errors:
        print("RTL interface usage check failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("RTL interface usage check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
