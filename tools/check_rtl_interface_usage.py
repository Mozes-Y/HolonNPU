#!/usr/bin/env python3
"""Validate interface-native RTL ownership and canonical product reachability."""

from __future__ import annotations

import re
import sys
from collections import deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RTL_ROOT = ROOT / "rtl"
SIM_RTL_ROOT = ROOT / "sim" / "rtl"
CMAKE_PATH = ROOT / "CMakeLists.txt"
PRODUCT_ROOT_TARGET = "holon_npu_rtl_top"
INTERFACES = {
    "npu_vr_if": "rtl/common/npu_vr_if.sv",
    "npu_axi4_if": "rtl/common/npu_axi4_if.sv",
    "npu_axi_lite_if": "rtl/common/npu_axi_lite_if.sv",
    "npu_frontend_if": "rtl/frontend/npu_frontend_if.sv",
    "npu_localmem_rd_if": "rtl/localmem/npu_localmem_rd_if.sv",
    "npu_localmem_wr_if": "rtl/localmem/npu_localmem_wr_if.sv",
}
VERSIONED_PRODUCT_TOKEN = re.compile(
    r"(?:npu_v[0-9]+|NPU_V[0-9]+|HOLON_NPU_V[0-9]+|holon_npu_v[0-9]+|\bv[0-9]+_)"
)


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def harness_name(path: str) -> bool:
    name = Path(path).name
    return name.endswith(("_test_wrapper.sv", "_test_top.sv", "_smoke_top.sv"))


def parse_source_targets(cmake: str) -> dict[str, set[str]]:
    targets: dict[str, set[str]] = {}
    pattern = re.compile(
        r"\bholon_npu_add_sv_source_target\s*\(\s*([A-Za-z0-9_]+)\s*(.*?)\)",
        re.S,
    )
    for match in pattern.finditer(cmake):
        name = match.group(1)
        if name in targets:
            raise ValueError(f"duplicate SystemVerilog source target {name}")
        targets[name] = set(re.findall(r"[A-Za-z0-9_./-]+\.sv", match.group(2)))
    return targets


def parse_target_links(cmake: str, known_targets: set[str]) -> dict[str, set[str]]:
    links = {target: set() for target in known_targets}
    pattern = re.compile(r"\btarget_link_libraries\s*\(\s*([A-Za-z0-9_]+)\s+(.*?)\)", re.S)
    for match in pattern.finditer(cmake):
        owner = match.group(1)
        if owner not in links:
            continue
        links[owner].update(
            token for token in re.findall(r"\bholon_npu_(?:rtl|sim)_[A-Za-z0-9_]+\b", match.group(2))
            if token in known_targets and token != owner
        )
    return links


def reachable_from(root: str, links: dict[str, set[str]]) -> set[str]:
    reached: set[str] = set()
    queue = deque([root])
    while queue:
        target = queue.popleft()
        if target in reached:
            continue
        reached.add(target)
        queue.extend(links.get(target, ()))
    return reached


def main() -> int:
    errors: list[str] = []
    cmake = CMAKE_PATH.read_text(encoding="utf-8")
    try:
        targets = parse_source_targets(cmake)
    except ValueError as error:
        errors.append(str(error))
        targets = {}
    links = parse_target_links(cmake, set(targets))
    product_targets = reachable_from(PRODUCT_ROOT_TARGET, links)

    rtl_files = {relative(path): path.read_text(encoding="utf-8") for path in RTL_ROOT.rglob("*.sv")}
    sim_files = {
        relative(path): path.read_text(encoding="utf-8") for path in SIM_RTL_ROOT.rglob("*.sv")
    }
    all_files = rtl_files | sim_files

    if PRODUCT_ROOT_TARGET not in targets:
        errors.append(f"missing canonical product root target {PRODUCT_ROOT_TARGET}")

    for path in rtl_files:
        if harness_name(path):
            errors.append(f"test-only harness is inside product RTL tree: {path}")
    for path in sim_files:
        if not harness_name(path):
            errors.append(f"simulation RTL is not explicitly named as a harness: {path}")

    for path, text in rtl_files.items():
        if VERSIONED_PRODUCT_TOKEN.search(path) or VERSIONED_PRODUCT_TOKEN.search(text):
            errors.append(f"version-prefixed product identifier in {path}")
        if path != "rtl/integration/npu_top.sv" and re.search(r"\b(?:m_axi|s_axil)_[A-Za-z0-9_]+_[io]\b", text):
            errors.append(f"flattened protocol ports in core RTL: {path}")
        if re.search(r"\b[A-Za-z_][A-Za-z0-9_]*_(?:test_wrapper|test_top)\b", text):
            errors.append(f"product RTL references a simulation harness: {path}")

    for interface, definition in INTERFACES.items():
        if definition not in rtl_files:
            errors.append(f"missing interface definition {definition}")
            continue
        users = {
            path for path, text in rtl_files.items()
            if path != definition and re.search(rf"\b{re.escape(interface)}\b", text)
        }
        if not users:
            errors.append(f"{interface} has no product RTL consumer")

    for path in all_files:
        owners = {target for target, sources in targets.items() if path in sources}
        if len(owners) != 1:
            errors.append(
                f"{path} must have exactly one CMake source owner; found "
                f"{', '.join(sorted(owners)) if owners else 'none'}"
            )
            continue
        owner = next(iter(owners))
        expected = "holon_npu_rtl_" if path.startswith("rtl/") else "holon_npu_sim_"
        if not owner.startswith(expected):
            errors.append(f"{path} is owned by the wrong target class: {owner}")
        if path.startswith("rtl/") and owner not in product_targets:
            errors.append(f"product RTL {path} is unreachable from {PRODUCT_ROOT_TARGET} via {owner}")

    for target, sources in targets.items():
        for source in sources:
            if source not in all_files:
                errors.append(f"{target} references missing source {source}")
        if target.startswith("holon_npu_rtl_") and target not in product_targets:
            errors.append(f"RTL target is not part of the product graph: {target}")

    public_paths = [ROOT / "include", ROOT / "sw", ROOT / "spec"]
    for base in public_paths:
        for path in base.rglob("*"):
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            if VERSIONED_PRODUCT_TOKEN.search(relative(path)) or VERSIONED_PRODUCT_TOKEN.search(text):
                errors.append(f"version-prefixed public product identifier in {relative(path)}")

    if errors:
        print("RTL ownership/interface check failed:")
        for error in sorted(set(errors)):
            print(f"- {error}")
        return 1
    print(f"RTL ownership/interface check passed: {len(product_targets)} product targets reachable.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
