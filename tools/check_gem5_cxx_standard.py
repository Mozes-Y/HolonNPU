#!/usr/bin/env python3
"""Require C++26 as the effective standard for every gem5 C++ translation unit."""

from __future__ import annotations

import argparse
import json
import shlex
from pathlib import Path


CXX_SUFFIXES = {".cc", ".cpp", ".cxx", ".C"}
VALID_STANDARDS = {"-std=c++26", "-std=gnu++26", "-std=c++2c", "-std=gnu++2c"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("compile_commands", type=Path)
    args = parser.parse_args()

    entries = json.loads(args.compile_commands.read_text(encoding="utf-8"))
    failures: list[str] = []
    checked = 0
    holon_sources: set[str] = set()
    for entry in entries:
        source = Path(entry["file"])
        if source.suffix not in CXX_SUFFIXES:
            continue
        checked += 1
        command = entry.get("arguments") or shlex.split(entry["command"])
        holon_sources.add(source.name)
        standards = [flag for flag in command if flag.startswith("-std=")]
        if not standards or standards[-1] not in VALID_STANDARDS:
            failures.append(f"{source}: effective standard {standards[-1] if standards else 'missing'}")
        if source.name == "holon_npu_device.cc":
            failures.append(f"{source}: superseded Host device is still compiled")
        if source.name in {"holon_npu_execution.cpp", "holon_npu_runtime.cpp"}:
            failures.append(f"{source}: test-side runner/builder is still compiled")
        if source.name == "holon_npu_npu.cpp":
            fp_contract = [flag for flag in command if flag.startswith("-ffp-contract=")]
            rounding = [flag for flag in command if flag in {"-frounding-math", "-fno-rounding-math"}]
            unsafe = {"-ffast-math", "-Ofast", "-funsafe-math-optimizations", "-fassociative-math", "-ffinite-math-only", "-fno-signed-zeros"}
            if not rounding or rounding[-1] != "-frounding-math" or not fp_contract or fp_contract[-1] != "-ffp-contract=off" or unsafe.intersection(command):
                failures.append(f"{source}: deterministic floating-point flags missing or overridden")

    if checked == 0:
        raise SystemExit("compile_commands.json contains no C++ translation units")
    for name in {"holon_npu.cc", "holon_npu_npu.cpp", "holon_npu_semantic.cpp"} - holon_sources:
        failures.append(f"{name}: autonomous model source is absent")
    if failures:
        raise SystemExit("gem5 C++26 audit failed:\n" + "\n".join(failures[:50]))
    print(f"gem5 C++26 audit passed: {checked} translation units")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
