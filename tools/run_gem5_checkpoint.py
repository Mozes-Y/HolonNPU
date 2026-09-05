#!/usr/bin/env python3
"""Capture and restore an idle HolonNPU gem5 checkpoint in separate processes."""

from __future__ import annotations

import argparse
import configparser
import os
import shutil
import subprocess
from pathlib import Path


def run_gem5(command: list[str], expected_sentinel: str) -> None:
    completed = subprocess.run(
        command,
        env=os.environ | {"PYTHONDONTWRITEBYTECODE": "1"},
        check=False,
        text=True,
        capture_output=True,
    )
    print(completed.stdout, end="")
    print(completed.stderr, end="")
    if completed.returncode != 0:
        raise SystemExit(f"gem5 checkpoint command failed with {completed.returncode}")
    if expected_sentinel not in completed.stdout:
        raise SystemExit(f"gem5 output did not contain {expected_sentinel}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gem5", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.output.exists():
        shutil.rmtree(args.output)
    capture_output = args.output / "capture"
    restore_output = args.output / "restore"
    checkpoint = args.output / "checkpoint"
    capture_output.mkdir(parents=True)
    restore_output.mkdir(parents=True)

    run_gem5(
        [
            str(args.gem5),
            "-d",
            str(capture_output),
            str(args.config),
            "--binary",
            str(args.binary),
            "--checkpoint-out",
            str(checkpoint),
        ],
        "HOLON_NPU_CHECKPOINT_CAPTURED",
    )

    checkpoint_state = checkpoint / "m5.cpt"
    if not checkpoint_state.is_file():
        raise SystemExit(f"checkpoint state is missing: {checkpoint_state}")
    serialized = configparser.ConfigParser(interpolation=None)
    serialized.optionxform = str
    serialized.read(checkpoint_state, encoding="utf-8")
    if not serialized.has_section("system.holon"):
        raise SystemExit("checkpoint is missing the HolonNPU section")
    required_fields = {
        "descriptorAddress",
        "irqEnable",
        "irqStatus",
        "interruptAsserted",
        "elapsedCycles",
    }
    missing_fields = sorted(required_fields - serialized["system.holon"].keys())
    if missing_fields:
        raise SystemExit("checkpoint is missing HolonNPU state: " + ", ".join(missing_fields))

    run_gem5(
        [
            str(args.gem5),
            "-d",
            str(restore_output),
            str(args.config),
            "--binary",
            str(args.binary),
            "--restore",
            str(checkpoint),
        ],
        "HOLON_NPU_CHECKPOINT_RESTORED",
    )
    print("HolonNPU idle checkpoint round trip passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
