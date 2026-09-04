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
    for entry in entries:
        source = Path(entry["file"])
        if source.suffix not in CXX_SUFFIXES:
            continue
        checked += 1
        command = entry.get("arguments") or shlex.split(entry["command"])
        standards = [flag for flag in command if flag.startswith("-std=")]
        if not standards or standards[-1] not in VALID_STANDARDS:
            failures.append(f"{source}: effective standard {standards[-1] if standards else 'missing'}")

    if checked == 0:
        raise SystemExit("compile_commands.json contains no C++ translation units")
    if failures:
        raise SystemExit("gem5 C++26 audit failed:\n" + "\n".join(failures[:50]))
    print(f"gem5 C++26 audit passed: {checked} translation units")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
