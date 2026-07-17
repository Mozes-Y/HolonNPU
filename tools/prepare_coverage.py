#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare a clean HolonNPU coverage run.")
    parser.add_argument("--coverage-dir", type=Path, required=True)
    parser.add_argument("--expected-test", action="append", default=[])
    args = parser.parse_args()

    expected_tests = sorted(set(args.expected_test))
    if not expected_tests:
        parser.error("at least one --expected-test is required")

    shutil.rmtree(args.coverage_dir, ignore_errors=True)
    args.coverage_dir.mkdir(parents=True)
    (args.coverage_dir / "expected_tests.txt").write_text(
        "".join(f"{name}\n" for name in expected_tests),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
