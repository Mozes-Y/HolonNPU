#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

BANNED_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("project feature macro definition", re.compile(r"^\s*#\s*define\s+HOLON_NPU_", re.MULTILINE)),
    ("project feature macro branch", re.compile(r"^\s*#\s*ifn?def\s+HOLON_NPU_", re.MULTILINE)),
    ("project feature macro branch", re.compile(r"^\s*#\s*if\s+defined\s*\(\s*HOLON_NPU_", re.MULTILINE)),
    ("SystemVerilog project macro definition", re.compile(r"^\s*`define\s+HOLON_NPU_", re.MULTILINE)),
    ("SystemVerilog project macro branch", re.compile(r"^\s*`ifn?def\s+HOLON_NPU_", re.MULTILINE)),
    ("SystemVerilog assertion wrapper macro use", re.compile(r"`HOLON_NPU_(ASSERT|COVER)\b")),
    ("CMake compile definition feature switch", re.compile(r"\btarget_compile_" r"definitions\s*\(")),
    ("Verilator project -D feature switch", re.compile(r"-DHOLON_NPU_")),
)

DOC_STALE_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("removed assertion include reference", re.compile(r"\bnpu_assert\.svh\b")),
    ("removed assertion wrapper reference", re.compile(r"\bHOLON_NPU_" r"ASSERT\b")),
    ("removed coverage wrapper reference", re.compile(r"\bHOLON_NPU_" r"COVER\b")),
    (
        "removed assertion/cover macro wording",
        re.compile(r"\bassertion/cover " r"macros\b", re.IGNORECASE),
    ),
)

SCAN_ROOTS = (
    "CMakeLists.txt",
    "README.md",
    "CHANGELOG.md",
    "docs",
    "include",
    "rtl",
    "sim",
    "sw",
    "tests",
    "tools",
)

SKIP_FILES = {
    "tools/check_macro_policy.py",
}

TEXT_SUFFIXES = {
    ".c",
    ".cpp",
    ".h",
    ".hpp",
    ".py",
    ".md",
    ".sv",
    ".svh",
    ".txt",
}


def iter_files() -> list[Path]:
    files: list[Path] = []
    for root in SCAN_ROOTS:
        path = ROOT / root
        if path.is_file():
            files.append(path)
            continue
        for candidate in path.rglob("*"):
            if candidate.is_file() and candidate.suffix in TEXT_SUFFIXES:
                files.append(candidate)
    return sorted(files)


def main() -> int:
    failures: list[str] = []
    for path in iter_files():
        rel = path.relative_to(ROOT).as_posix()
        if rel in SKIP_FILES:
            continue
        text = path.read_text(encoding="utf-8")
        for description, pattern in BANNED_PATTERNS:
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                failures.append(f"{rel}:{line}: banned {description}: {match.group(0).strip()}")
        if rel == "README.md" or rel.startswith("docs/") or rel == "CHANGELOG.md":
            for description, pattern in DOC_STALE_PATTERNS:
                for match in pattern.finditer(text):
                    line = text.count("\n", 0, match.start()) + 1
                    failures.append(f"{rel}:{line}: banned {description}: {match.group(0).strip()}")

    if failures:
        print("Macro policy check failed:", file=sys.stderr)
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1

    print("Macro policy check passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
