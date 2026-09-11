#!/usr/bin/env python3
"""Keep current C/C++ code free of project behavior macros."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check(root: Path) -> list[str]:
    failures = []
    for directory in ("include", "sim", "sw", "tests"):
        for path in (root / directory).rglob("*"):
            if path.suffix not in {".c", ".cpp", ".h", ".hpp"}:
                continue
            for number, line in enumerate(path.read_text().splitlines(), 1):
                directive = re.match(r"\s*#\s*(define|if|ifdef|ifndef|elif)\b(.*)", line)
                if directive and not (
                    directive[1] in {"ifdef", "ifndef"} and directive[2].strip() == "__cplusplus"
                ):
                    failures.append(f"{path.relative_to(root)}:{number}: project behavior macro")
    cmake_files = [root / "CMakeLists.txt"]
    for directory in ("include", "sim", "sw", "tests", "cmake"):
        cmake_files.extend((root / directory).rglob("CMakeLists.txt"))
        cmake_files.extend((root / directory).rglob("*.cmake"))
    for path in cmake_files:
        if re.search(r"target_compile_" + r"definitions\s*\(", path.read_text()):
            failures.append(f"{path.relative_to(root)}: compile-definition behavior switch")
    return failures


def main() -> int:
    failures = check(ROOT)
    if failures:
        print("\n".join(failures))
        return 1
    print("Macro policy check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
