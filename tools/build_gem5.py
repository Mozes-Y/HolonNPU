#!/usr/bin/env python3
"""Build gem5 stable with HolonNPU EXTRAS and emit audited provenance."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def gem5_recognized_compiler(compiler: str) -> str:
    """Resolve generic ``c++`` launchers to a compiler identity gem5 recognizes."""

    path = Path(compiler).resolve()
    version = subprocess.run(
        [str(path), "-dumpfullversion"], check=True, text=True, capture_output=True
    ).stdout.strip()
    # SCons hashes command spellings. Prefer the public GCC launcher only when
    # it names the exact same binary, preserving custom compiler selections.
    candidate = path.parent / f"g++-{version.split('.')[0]}"
    if candidate.is_file() and candidate.samefile(path):
        return str(candidate)
    return str(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--extras", type=Path, required=True)
    parser.add_argument("--patch", type=Path, required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--riscv-compiler", required=True)
    parser.add_argument("--jobs", type=int, default=16)
    parser.add_argument("--metadata", type=Path, required=True)
    args = parser.parse_args()

    if args.jobs < 1:
        raise SystemExit("--jobs must be a positive integer")

    target_name = Path("build/RISCV/gem5.opt")
    compile_commands_name = Path("build/RISCV/compile_commands.json")
    target = args.source / target_name
    compile_commands = args.source / compile_commands_name
    scons = shutil.which("scons")
    if scons is None:
        raise SystemExit("scons is required to build gem5")
    compiler = gem5_recognized_compiler(args.compiler)
    environment = os.environ.copy()
    environment["CXX"] = compiler
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    common_scons_args = [
        f"EXTRAS={args.extras.resolve()}",
        "--ignore-style",
        f"-j{args.jobs}",
    ]
    subprocess.run(
        [scons, str(target_name), *common_scons_args],
        cwd=args.source,
        env=environment,
        check=True,
    )
    if not target.is_file() or not os.access(target, os.X_OK):
        raise SystemExit(f"gem5 build completed without an executable: {target}")
    subprocess.run(
        [scons, str(compile_commands_name), *common_scons_args],
        cwd=args.source,
        env=environment,
        check=True,
    )
    if not compile_commands.is_file():
        raise SystemExit(
            f"gem5 compilation database was not generated: {compile_commands}"
        )
    repo = args.extras.resolve().parents[1]
    subprocess.run(
        [sys.executable, str(repo / "tools/check_gem5_cxx_standard.py"), str(compile_commands)],
        check=True,
    )
    subprocess.run(
        [
            sys.executable,
            str(repo / "tools/write_simulation_metadata.py"),
            "--source",
            str(args.source),
            "--patch",
            str(args.patch),
            "--compiler",
            compiler,
            "--riscv-compiler",
            args.riscv_compiler,
            "--build-type",
            "opt",
            "--build-jobs",
            str(args.jobs),
            "--output",
            str(args.metadata),
        ],
        check=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
