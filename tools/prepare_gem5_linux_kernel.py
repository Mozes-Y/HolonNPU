#!/usr/bin/env python3
"""Build the locked Linux 6.8.12 RISC-V kernel tree with GCC 15."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import urllib.request
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def compiler_major(compiler: Path) -> int:
    version = subprocess.run(
        [compiler, "-dumpfullversion"], check=True, text=True, capture_output=True
    ).stdout.strip()
    return int(version.split(".")[0])


def cross_compile_prefix(compiler: Path) -> str:
    match = re.fullmatch(r"(?P<prefix>.+-)gcc(?:-[0-9]+)?", compiler.name)
    if match is None:
        raise SystemExit(f"cannot derive CROSS_COMPILE from {compiler}")
    return str(compiler.with_name(match.group("prefix")))


def download(url: str, destination: Path, expected_sha256: str) -> None:
    if destination.exists() and sha256(destination) == expected_sha256:
        return
    temporary = destination.with_suffix(destination.suffix + ".part")
    temporary.unlink(missing_ok=True)
    print(f"Downloading {url}", flush=True)
    with urllib.request.urlopen(url) as response, temporary.open("wb") as output:
        downloaded = 0
        next_report = 32 * 1024 * 1024
        while block := response.read(1024 * 1024):
            output.write(block)
            downloaded += len(block)
            if downloaded >= next_report:
                print(
                    f"  {destination.name}: {downloaded // (1024 * 1024)} MiB",
                    flush=True,
                )
                next_report += 32 * 1024 * 1024
    actual_sha256 = sha256(temporary)
    if actual_sha256 != expected_sha256:
        temporary.unlink(missing_ok=True)
        raise SystemExit(
            f"SHA-256 mismatch for {destination.name}: "
            f"expected {expected_sha256}, got {actual_sha256}"
        )
    temporary.replace(destination)


def run_make(
    source_dir: Path,
    build_dir: Path,
    compiler: Path,
    cross_compile: str,
    *targets: str,
    jobs: int | None = None,
) -> None:
    command = [
        "make",
        "-C",
        str(source_dir),
        f"O={build_dir}",
        "ARCH=riscv",
        f"CROSS_COMPILE={cross_compile}",
        f"CC={compiler}",
    ]
    if jobs is not None:
        command.append(f"-j{jobs}")
    command.extend(targets)
    subprocess.run(command, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--resource-lock", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=16)
    args = parser.parse_args()

    if args.jobs <= 0:
        raise SystemExit("--jobs must be positive")
    compiler = Path(shutil.which(args.compiler) or args.compiler).resolve()
    if not compiler.exists() or compiler_major(compiler) < 15:
        raise SystemExit("RISC-V GCC 15 or newer is required")
    cross_compile = cross_compile_prefix(compiler)
    resource_lock = args.resource_lock.resolve()
    output_dir = args.output_dir.resolve()
    lock = json.loads(resource_lock.read_text(encoding="utf-8"))
    recipe = lock["kernel_recipe"]
    source_package = recipe["source_package"]

    downloads = output_dir / "downloads"
    source_dir = output_dir / "source"
    build_dir = output_dir / "build"
    downloads.mkdir(parents=True, exist_ok=True)
    recipe_url = (
        "https://raw.githubusercontent.com/gem5/gem5-resources/"
        f"{recipe['commit']}/{recipe['path']}"
    )
    recipe_file = downloads / "gem5-kernel-recipe.Dockerfile"
    download(recipe_url, recipe_file, recipe["sha256"])
    for source_file in source_package["files"]:
        download(
            source_file["url"],
            downloads / source_file["name"],
            source_file["sha256"],
        )

    dsc = downloads / f"linux_{source_package['version']}.dsc"
    source_stamp = source_dir / ".holon-source-version"
    if not source_stamp.exists() or source_stamp.read_text(encoding="utf-8").strip() != source_package["version"]:
        if source_dir.exists():
            shutil.rmtree(source_dir)
        subprocess.run(
            ["dpkg-source", "-x", dsc, source_dir],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        source_stamp.write_text(source_package["version"] + "\n", encoding="utf-8")

    build_dir.mkdir(parents=True, exist_ok=True)
    run_make(source_dir, build_dir, compiler, cross_compile, "defconfig")
    run_make(source_dir, build_dir, compiler, cross_compile, jobs=args.jobs)
    kernel_release = subprocess.run(
        [
            "make",
            "-s",
            "-C",
            str(source_dir),
            f"O={build_dir}",
            "ARCH=riscv",
            f"CROSS_COMPILE={cross_compile}",
            f"CC={compiler}",
            "kernelrelease",
        ],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    if kernel_release != recipe["kernel_release"]:
        raise SystemExit(
            f"built kernel release {kernel_release} does not match locked "
            f"{recipe['kernel_release']}"
        )

    metadata = {
        "schema_version": 1,
        "resource_lock_sha256": sha256(resource_lock),
        "kernel_release": kernel_release,
        "source_package": source_package["name"],
        "source_version": source_package["version"],
        "source_files": {
            entry["name"]: entry["sha256"] for entry in source_package["files"]
        },
        "gem5_resources_recipe": {
            "repository": recipe["repository"],
            "commit": recipe["commit"],
            "path": recipe["path"],
            "sha256": sha256(recipe_file),
        },
        "compiler": str(compiler),
        "compiler_version": subprocess.run(
            [compiler, "--version"], check=True, text=True, capture_output=True
        ).stdout.splitlines()[0],
        "jobs": args.jobs,
        "config_sha256": sha256(build_dir / ".config"),
        "module_symvers_sha256": sha256(build_dir / "Module.symvers"),
        "vmlinux_sha256": sha256(build_dir / "vmlinux"),
    }
    metadata_path = output_dir / "kernel-metadata.json"
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(metadata_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
