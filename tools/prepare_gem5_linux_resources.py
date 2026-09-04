#!/usr/bin/env python3
"""Prepare immutable gem5 Linux resources from the reviewed lock file."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import shutil
import urllib.request
from pathlib import Path


CHUNK_SIZE = 8 * 1024 * 1024


def file_digest(path: Path, algorithm: str) -> str:
    digest = hashlib.new(algorithm, usedforsecurity=False)
    with path.open("rb") as source:
        while block := source.read(CHUNK_SIZE):
            digest.update(block)
    return digest.hexdigest()


def content_digests(path: Path) -> dict[str, str]:
    digests = {
        "md5": hashlib.md5(usedforsecurity=False),
        "sha256": hashlib.sha256(),
    }
    with path.open("rb") as source:
        while block := source.read(CHUNK_SIZE):
            for digest in digests.values():
                digest.update(block)
    return {name: digest.hexdigest() for name, digest in digests.items()}


def validated_digests(
    path: Path, entry: dict[str, object]
) -> dict[str, str] | None:
    if not path.is_file() or path.stat().st_size != entry["size"]:
        return None
    digests = content_digests(path)
    return digests if digests["md5"] == entry["md5"] else None


def download(url: str, destination: Path) -> None:
    request = urllib.request.Request(
        url, headers={"User-Agent": "HolonNPU-resource-preparer/1"}
    )
    with (
        urllib.request.urlopen(request) as response,
        destination.open("wb") as output,
    ):
        total = int(response.headers.get("Content-Length", "0"))
        copied = 0
        while block := response.read(CHUNK_SIZE):
            output.write(block)
            copied += len(block)
            if total:
                print(f"\r{destination.name}: {copied / total:6.1%}", end="", flush=True)
        if total:
            print()


def prepare_resource(
    directory: Path, name: str, entry: dict[str, object]
) -> dict[str, object]:
    destination = directory / str(entry["id"])
    digests = validated_digests(destination, entry)
    if digests is None:
        compressed = destination.with_suffix(destination.suffix + ".download")
        expanded = destination.with_suffix(destination.suffix + ".part")
        compressed.unlink(missing_ok=True)
        expanded.unlink(missing_ok=True)
        download(str(entry["url"]), compressed)
        if entry["compression"] == "gzip":
            with gzip.open(compressed, "rb") as source, expanded.open("wb") as output:
                shutil.copyfileobj(source, output, length=CHUNK_SIZE)
        else:
            os.replace(compressed, expanded)
        compressed.unlink(missing_ok=True)
        digests = validated_digests(expanded, entry)
        if digests is None:
            expanded.unlink(missing_ok=True)
            raise SystemExit(
                f"downloaded resource failed locked size/MD5 validation: {name}"
            )
        os.replace(expanded, destination)
    return {
        "id": entry["id"],
        "resource_version": entry["resource_version"],
        "path": str(destination.resolve()),
        "size": destination.stat().st_size,
        **digests,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--resource-lock", type=Path, required=True)
    parser.add_argument("--resource-directory", type=Path, required=True)
    args = parser.parse_args()

    lock_path = args.resource_lock.resolve()
    directory = args.resource_directory.resolve()
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    directory.mkdir(parents=True, exist_ok=True)
    resources = {
        name: prepare_resource(directory, name, entry)
        for name, entry in lock["resources"].items()
    }
    metadata = {
        "schema_version": 1,
        "resource_lock_sha256": file_digest(lock_path, "sha256"),
        "resources": resources,
    }
    metadata_path = directory / "resources-metadata.json"
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(metadata_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
