#!/usr/bin/env python3
"""Run the locked HolonNPU Linux full-system workload."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


def md5(path: Path) -> str:
    digest = hashlib.md5(usedforsecurity=False)
    with path.open("rb") as source:
        while block := source.read(8 * 1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(8 * 1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def validate_resources(lock: dict[str, object], resource_directory: Path) -> dict[str, object]:
    resources = lock["resources"]
    evidence: dict[str, object] = {"schema_version": 1, "resources": {}}
    for name, raw_entry in resources.items():
        entry = raw_entry
        path = resource_directory / entry["id"]
        if not path.is_file():
            raise SystemExit(f"prepared gem5 resource is missing: {path}")
        actual_size = path.stat().st_size
        actual_md5 = md5(path)
        if actual_size != entry["size"] or actual_md5 != entry["md5"]:
            raise SystemExit(f"prepared gem5 resource failed validation: {name}")
        evidence["resources"][name] = {
            "path": str(path.resolve()),
            "size": actual_size,
            "md5": actual_md5,
        }
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gem5", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--resource-lock", type=Path, required=True)
    parser.add_argument("--guest-bundle", type=Path, required=True)
    parser.add_argument("--resource-directory", type=Path, required=True)
    parser.add_argument("--dtc", type=Path, required=True)
    parser.add_argument("--fdtoverlay", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    for tool in (args.dtc, args.fdtoverlay):
        if not tool.is_file() or not os.access(tool, os.X_OK):
            raise SystemExit(f"device-tree tool is unavailable: {tool}")

    lock = json.loads(args.resource_lock.read_text(encoding="utf-8"))
    guest_metadata_path = args.guest_bundle.resolve().with_name("guest-metadata.json")
    if not guest_metadata_path.is_file():
        raise SystemExit("guest bundle metadata is missing")
    guest_metadata = json.loads(guest_metadata_path.read_text(encoding="utf-8"))
    if guest_metadata.get("bundle_sha256") != sha256(args.guest_bundle):
        raise SystemExit("guest bundle does not match guest-metadata.json")
    kernel_metadata = guest_metadata.get("kernel", {})
    kernel_recipe = lock["kernel_recipe"]
    if (
        kernel_metadata.get("kernel_release") != kernel_recipe["kernel_release"]
        or kernel_metadata.get("source_version")
        != kernel_recipe["source_package"]["version"]
        or kernel_metadata.get("resource_lock_sha256")
        != sha256(args.resource_lock)
    ):
        raise SystemExit("guest kernel metadata does not match the resource lock")
    resource_directory = args.resource_directory.resolve()
    evidence = validate_resources(lock, resource_directory)
    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)
    shutil.copy2(args.resource_lock, args.output / "resources.lock.json")
    shutil.copy2(guest_metadata_path, args.output / "guest-metadata.json")
    (args.output / "resources-metadata.json").write_text(
        json.dumps(evidence, indent=2) + "\n", encoding="utf-8"
    )
    environment = os.environ | {"PYTHONDONTWRITEBYTECODE": "1"}
    subprocess.run(
        [
            str(args.gem5), "-d", str(args.output), str(args.config),
            "--resource-directory", str(resource_directory),
            "--resource-lock", str(args.resource_lock),
            "--guest-bundle", str(args.guest_bundle),
            "--workload-id", lock["workload"]["id"],
            "--workload-version", lock["workload"]["resource_version"],
            "--dtc", str(args.dtc.resolve()),
            "--fdtoverlay", str(args.fdtoverlay.resolve()),
        ],
        env=environment,
        check=True,
    )
    terminal = args.output / "board.platform.terminal"
    terminal_text = terminal.read_text(encoding="utf-8", errors="replace")
    if (
        terminal_text.count("HOLON_NPU_LINUX_PASS") != 1
        or "HOLON_NPU_LINUX_FAIL" in terminal_text
    ):
        raise SystemExit("Linux guest did not report a unique passing sentinel")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
