#!/usr/bin/env python3
"""Build the matching RISC-V Linux module, smoke binary, and readfile bundle."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path


def compiler_major(compiler: str) -> int:
    version = subprocess.run(
        [compiler, "-dumpfullversion"], check=True, text=True, capture_output=True
    ).stdout.strip()
    return int(version.split(".")[0])


def encode_file(path: Path) -> str:
    return base64.b64encode(path.read_bytes()).decode("ascii")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cross_compile_prefix(compiler: Path) -> str:
    match = re.fullmatch(r"(?P<prefix>.+-)gcc(?:-[0-9]+)?", compiler.name)
    if match is None:
        raise SystemExit(f"cannot derive CROSS_COMPILE from {compiler}")
    return str(compiler.with_name(match.group("prefix")))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--kernel-build-dir", type=Path, required=True)
    parser.add_argument("--kernel-metadata", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--resource-lock", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    if compiler_major(args.compiler) < 15:
        raise SystemExit("RISC-V GCC 15 or newer is required for Linux guest artifacts")
    kernel_build_dir = args.kernel_build_dir.resolve()
    kernel_metadata_path = args.kernel_metadata.resolve()
    source_root = args.source_root.resolve()
    resource_lock = args.resource_lock.resolve()
    output_dir = args.output_dir.resolve()
    lock = json.loads(resource_lock.read_text(encoding="utf-8"))
    required_release = lock["kernel_recipe"]["kernel_release"]
    kernel_metadata = json.loads(kernel_metadata_path.read_text(encoding="utf-8"))
    if kernel_metadata.get("kernel_release") != required_release:
        raise SystemExit("kernel metadata release does not match the resource lock")
    if kernel_metadata.get("source_version") != lock["kernel_recipe"]["source_package"]["version"]:
        raise SystemExit("kernel metadata source version does not match the resource lock")
    if kernel_metadata.get("resource_lock_sha256") != sha256(resource_lock):
        raise SystemExit("kernel metadata was not built from the current resource lock")
    compiler_path = Path(shutil.which(args.compiler) or args.compiler).resolve()
    cross_compile = cross_compile_prefix(compiler_path)
    kernel_toolchain = [
        "ARCH=riscv",
        f"CROSS_COMPILE={cross_compile}",
        f"CC={compiler_path}",
    ]

    kernel_release = subprocess.run(
        [
            "make", "-s", "-C", str(kernel_build_dir),
            *kernel_toolchain, "kernelrelease",
        ],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    if kernel_release != required_release:
        raise SystemExit(
            f"kernel build release {kernel_release} does not match locked {required_release}"
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    module_source = output_dir / "module-source"
    if module_source.exists():
        shutil.rmtree(module_source)
    shutil.copytree(source_root / "sim/gem5/linux", module_source)
    subprocess.run(
        [
            "make", "-C", str(kernel_build_dir),
            *kernel_toolchain,
            f"M={module_source}", "modules",
        ],
        check=True,
    )

    application = output_dir / "holon_npu_linux_smoke"
    subprocess.run(
        [
            str(compiler_path), "-std=c23", "-static", "-O2", "-Wall", "-Wextra",
            "-I", str(source_root / "include"),
            "-I", str(source_root / "sim/gem5/linux"),
            str(source_root / "sim/gem5/linux/holon_npu_linux_smoke.c"),
            "-o", str(application),
        ],
        check=True,
    )

    module = module_source / "holon_npu_sim.ko"
    guest_metadata = {
        "schema_version": 1,
        "kernel": kernel_metadata,
        "module_sha256": sha256(module),
        "application_sha256": sha256(application),
    }
    bundle = output_dir / "guest-bundle.sh"
    bundle.write_text(
        "\n".join(
            [
                "#!/bin/sh",
                "set -eu",
                "base64 -d >/tmp/holon_npu_sim.ko <<'HOLON_MODULE'",
                encode_file(module),
                "HOLON_MODULE",
                "base64 -d >/tmp/holon_npu_linux_smoke <<'HOLON_APP'",
                encode_file(application),
                "HOLON_APP",
                "chmod 0755 /tmp/holon_npu_linux_smoke",
                "set +e",
                "printf '12345\\n' | sudo -S sh -c '",
                "  insmod /tmp/holon_npu_sim.ko && /tmp/holon_npu_linux_smoke",
                "  status=$?",
                "  rmmod holon_npu_sim >/dev/null 2>&1 || true",
                "  exit $status",
                "'",
                "status=$?",
                "set -e",
                "if [ \"$status\" -eq 0 ]; then",
                "  echo HOLON_NPU_LINUX_PASS",
                "else",
                "  echo \"HOLON_NPU_LINUX_FAIL status=$status\"",
                "fi",
                "m5 exit",
                "",
            ]
        ),
        encoding="utf-8",
    )
    os.chmod(bundle, 0o755)
    guest_metadata["bundle_sha256"] = sha256(bundle)
    (output_dir / "guest-metadata.json").write_text(
        json.dumps(guest_metadata, indent=2) + "\n", encoding="utf-8"
    )
    print(bundle)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
