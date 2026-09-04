#!/usr/bin/env python3
"""Validate the reviewed gem5 full-system resource lock."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


HEX_DIGEST = re.compile(r"^[0-9a-f]+$")


def require_string(mapping: dict[str, object], key: str) -> str:
    value = mapping.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{key} must be a non-empty string")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("lock", type=Path)
    args = parser.parse_args()

    data = json.loads(args.lock.read_text(encoding="utf-8"))
    if "catalog" in data:
        raise SystemExit("runtime catalog lookup is forbidden by the immutable resource lock")
    workload = data.get("workload")
    resources = data.get("resources")
    recipe = data.get("kernel_recipe")
    if not isinstance(workload, dict) or not isinstance(resources, dict) or not isinstance(recipe, dict):
        raise SystemExit("resource lock requires workload, resources, and kernel_recipe objects")
    if require_string(workload, "id") != "riscv-ubuntu-24.04-boot-no-systemd":
        raise SystemExit("Linux baseline must remain the reviewed Ubuntu 24.04 workload")
    if require_string(workload, "resource_version") != "2.0.0":
        raise SystemExit("unexpected Ubuntu 24.04 workload version")

    expected = {"bootloader", "kernel", "disk_image"}
    if set(resources) != expected:
        raise SystemExit(f"resource lock must contain exactly {sorted(expected)}")
    expected_categories = {
        "bootloader": "bootloader",
        "kernel": "kernel",
        "disk_image": "disk-image",
    }
    for name, resource in resources.items():
        if not isinstance(resource, dict):
            raise SystemExit(f"{name} resource must be an object")
        require_string(resource, "id")
        require_string(resource, "resource_version")
        if require_string(resource, "category") != expected_categories[name]:
            raise SystemExit(f"{name} has an unexpected resource category")
        if require_string(resource, "architecture") != "RISCV":
            raise SystemExit(f"{name} must target RISCV")
        url = require_string(resource, "url")
        digest = require_string(resource, "md5")
        if not url.startswith("https://dist.gem5.org/"):
            raise SystemExit(f"{name} does not use the official gem5 distribution host")
        if len(digest) != 32 or not HEX_DIGEST.fullmatch(digest):
            raise SystemExit(f"{name} has an invalid MD5 digest")
        if not isinstance(resource.get("size"), int) or resource["size"] <= 0:
            raise SystemExit(f"{name} has an invalid size")
        expected_compression = "gzip" if name == "disk_image" else "none"
        if require_string(resource, "compression") != expected_compression:
            raise SystemExit(f"{name} has an unexpected compression mode")
    if require_string(resources["disk_image"], "root_partition") != "1":
        raise SystemExit("Ubuntu 24.04 root partition must remain locked to 1")

    commit = require_string(recipe, "commit")
    digest = require_string(recipe, "sha256")
    if len(commit) != 40 or not HEX_DIGEST.fullmatch(commit):
        raise SystemExit("kernel recipe commit must be a full Git SHA")
    if len(digest) != 64 or not HEX_DIGEST.fullmatch(digest):
        raise SystemExit("kernel recipe SHA-256 is invalid")
    if require_string(recipe, "branch") != "stable":
        raise SystemExit("kernel recipe must follow gem5-resources stable")
    if require_string(recipe, "kernel_release") != "6.8.12":
        raise SystemExit("kernel recipe and locked kernel release disagree")

    source_package = recipe.get("source_package")
    if not isinstance(source_package, dict):
        raise SystemExit("kernel recipe requires an immutable source_package")
    if require_string(source_package, "name") != "linux":
        raise SystemExit("unexpected kernel source package name")
    if require_string(source_package, "version") != "6.8.0-47.47":
        raise SystemExit("unexpected kernel source package version")
    source_files = source_package.get("files")
    if not isinstance(source_files, list) or len(source_files) != 3:
        raise SystemExit("kernel source package must lock exactly three files")
    expected_names = {
        "linux_6.8.0.orig.tar.gz",
        "linux_6.8.0-47.47.diff.gz",
        "linux_6.8.0-47.47.dsc",
    }
    actual_names: set[str] = set()
    for source_file in source_files:
        if not isinstance(source_file, dict):
            raise SystemExit("kernel source file entries must be objects")
        name = require_string(source_file, "name")
        url = require_string(source_file, "url")
        source_digest = require_string(source_file, "sha256")
        if not url.startswith(
            "https://launchpad.net/ubuntu/+archive/primary/+sourcefiles/linux/6.8.0-47.47/"
        ):
            raise SystemExit(f"{name} does not use the locked Ubuntu source archive")
        if len(source_digest) != 64 or not HEX_DIGEST.fullmatch(source_digest):
            raise SystemExit(f"{name} has an invalid SHA-256 digest")
        actual_names.add(name)
    if actual_names != expected_names:
        raise SystemExit("kernel source package file set is incomplete")

    print("gem5 resource lock passed: Ubuntu 24.04 / Linux 6.8.12")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
