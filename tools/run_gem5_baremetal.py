#!/usr/bin/env python3
"""Run the fast RISC-V system gate and validate HolonNPU gem5 statistics."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
from pathlib import Path


STAT_LINE = re.compile(r"^(\S+)\s+([0-9.eE+-]+)\s*(?:#.*)?$")


def read_stats(path: Path) -> dict[str, float]:
    stats: dict[str, float] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = STAT_LINE.match(line)
        if match is not None:
            stats[match.group(1)] = float(match.group(2))
    return stats


def find_stat(stats: dict[str, float], leaf: str) -> float:
    matches = [value for name, value in stats.items() if name.endswith(f"holon.{leaf}")]
    if len(matches) != 1:
        raise SystemExit(f"expected one HolonNPU statistic named {leaf}, found {len(matches)}")
    return matches[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gem5", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    environment = os.environ | {"PYTHONDONTWRITEBYTECODE": "1"}
    subprocess.run(
        [
            str(args.gem5),
            "-d",
            str(args.output),
            str(args.config),
            "--binary",
            str(args.binary),
        ],
        env=environment,
        check=True,
    )

    stats = read_stats(args.output / "stats.txt")
    minimums = {
        "dmaCommands": 2,
        "dmaTransactions": 20,
        "dmaBytes": 1,
        "dmaWaitCycles": 1,
        "vectorOperations": 1,
        "matrixOperations": 1,
        "matrixMacs": 8,
        "irqCount": 3,
        "faultCount": 1,
        "resetCount": 1,
        "completionCount": 3,
    }
    failures = [
        f"{name}: expected >= {minimum}, got {find_stat(stats, name):g}"
        for name, minimum in minimums.items()
        if find_stat(stats, name) < minimum
    ]
    active = find_stat(stats, "activeCycles")
    total = find_stat(stats, "totalCycles")
    if total <= active:
        failures.append(f"totalCycles must include memory wait: total={total:g}, active={active:g}")
    if failures:
        raise SystemExit("gem5 bare-metal statistics gate failed:\n" + "\n".join(failures))

    print(
        "HolonNPU gem5 bare-metal gate passed: "
        f"transactions={find_stat(stats, 'dmaTransactions'):g}, "
        f"active_cycles={active:g}, total_cycles={total:g}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
