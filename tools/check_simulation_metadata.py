#!/usr/bin/env python3
"""Validate gem5 build provenance and the effective C++ standard."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--compile-commands", type=Path, required=True)
    args = parser.parse_args()

    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    required = {
        "gem5", "overlay_sha256", "compiler", "riscv_compiler",
        "cxx_standard", "build_type", "build_jobs", "host", "model", "compile_commands_sha256",
    }
    missing = sorted(required - metadata.keys())
    if missing:
        raise SystemExit(f"simulation metadata missing keys: {', '.join(missing)}")
    if metadata["gem5"].get("branch") != "stable" or not metadata["gem5"].get("commit"):
        raise SystemExit("simulation metadata does not identify upstream stable and its commit")
    if metadata["cxx_standard"] != "C++26":
        raise SystemExit("simulation metadata does not record C++26")
    if not isinstance(metadata["build_jobs"], int) or metadata["build_jobs"] < 1:
        raise SystemExit("simulation metadata does not record a valid build parallelism")
    if metadata["model"].get("type") != "HolonNpu" or not metadata["model"].get("source_sha256"):
        raise SystemExit("simulation metadata does not identify the autonomous model and sources")
    repo = Path(__file__).resolve().parents[1]
    for source, expected in metadata["model"]["source_sha256"].items():
        path = (repo / source).resolve()
        if not path.is_relative_to(repo) or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise SystemExit(f"simulation metadata source drift: {source}")
    if hashlib.sha256(args.compile_commands.read_bytes()).hexdigest() != metadata["compile_commands_sha256"]:
        raise SystemExit("simulation metadata compilation database drift")
    subprocess.run(
        [sys.executable, str(Path(__file__).with_name("check_gem5_cxx_standard.py")),
         str(args.compile_commands)],
        check=True,
    )
    print(f"gem5 stable {metadata['gem5']['commit']} metadata passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
