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
        "model_defaults": {
            "device_clock_hz": 1000000000,
            "vector_lanes": 16,
            "matrix_m": 16,
            "matrix_k": 16,
            "matrix_n": 16,
            "maximum_dma_bytes": 256,
            "dma_page_bytes": 4096,
            "frontend_cycles": 1,
            "scalar_local_cycles": 2,
            "vector_issue_cycles": 1,
            "quant_parameter_words": 6,
            "matrix_descriptor_words": 8,
            "matrix_validate_cycles": 1,
            "matrix_clear_cycles": 1,
            "matrix_drain_cycles": 1,
            "dma_setup_cycles": 4,
            "sync_cycles": 1,
            "scratchpad_read_cycles": 2,
            "scratchpad_write_cycles": 2,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
