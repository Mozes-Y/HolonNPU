#!/usr/bin/env python3
"""Build the freestanding RISC-V HolonNPU smoke workload with GCC 15+."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--include-dir", type=Path, required=True)
    parser.add_argument("--workload", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    version = subprocess.run(
        [args.compiler, "-dumpfullversion"], check=True, text=True, capture_output=True
    ).stdout.strip()
    if int(version.split(".")[0]) < 15:
        raise SystemExit(f"RISC-V GCC 15 or newer is required, found {version}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            args.compiler,
            "-std=c23",
            "-march=rv64im_zicsr",
            "-mabi=lp64",
            "-mcmodel=medany",
            "-ffreestanding",
            "-fno-builtin",
            "-nostdlib",
            "-nostartfiles",
            "-static",
            "-O2",
            "-I",
            str(args.include_dir),
            "-T",
            str(args.source_dir / "link.ld"),
            str(args.source_dir / "start.S"),
            str(args.source_dir / "freestanding.c"),
            str(args.workload),
            "-o",
            str(args.output),
        ],
        check=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
