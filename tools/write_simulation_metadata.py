#!/usr/bin/env python3
"""Write immutable provenance for a HolonNPU gem5 build."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--patch", type=Path, required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--riscv-compiler", required=True)
    parser.add_argument("--build-type", required=True)
    parser.add_argument("--build-jobs", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    commit = subprocess.run(
        ["git", "-C", str(args.source), "rev-parse", "HEAD"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    compiler = subprocess.run(
        [args.compiler, "--version"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.splitlines()[0]
    riscv_compiler = subprocess.run(
        [args.riscv_compiler, "--version"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.splitlines()[0]
    repo = Path(__file__).resolve().parents[1]
    inputs = sorted({
        *repo.glob("sim/semantic/*.cpp"), *repo.glob("sim/semantic/*.hpp"),
        *repo.glob("sim/gem5/*.cc"), *repo.glob("sim/gem5/*.hh"),
        *repo.glob("sim/gem5/*.cpp"), *repo.glob("sim/gem5/*.hpp"),
        repo / "sim/gem5/HolonNpu.py", repo / "sim/gem5/SConscript",
    } - {repo / "sim/semantic/holon_npu_execution.cpp", repo / "sim/semantic/holon_npu_execution.hpp"})
    sources = {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    metadata = {
        "gem5": {"branch": "stable", "commit": commit},
        "overlay_sha256": hashlib.sha256(args.patch.read_bytes()).hexdigest(),
        "compiler": compiler,
        "riscv_compiler": riscv_compiler,
        "cxx_standard": "C++26",
        "build_type": args.build_type,
        "build_jobs": args.build_jobs,
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
        },
        "model": {
            "type": "HolonNpu",
            "parameters": "per-run config.json and source fingerprints",
            "source_sha256": sources,
        },
        "compile_commands_sha256": hashlib.sha256(
            (args.source / "build/RISCV/compile_commands.json").read_bytes()
        ).hexdigest(),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
