#!/usr/bin/env python3
"""Validate the isolated upstream gem5 checkout and apply the C++26 overlay."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


OFFICIAL_REMOTES = {
    "https://github.com/gem5/gem5.git",
    "git@github.com:gem5/gem5.git",
}


def run(source: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(source), *args],
        check=check,
        text=True,
        capture_output=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--patch", type=Path, required=True)
    parser.add_argument("--state", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    patch = args.patch.resolve()
    remote = run(source, "remote", "get-url", "origin").stdout.strip()
    if remote not in OFFICIAL_REMOTES:
        raise SystemExit(f"gem5 origin is not official: {remote}")

    run(source, "fetch", "--prune", "origin", "stable")
    run(source, "reset", "--hard", "origin/stable")
    # Keep SCons' disposable build tree so repeated stable-branch checks remain
    # incremental while still removing unrelated untracked source files.
    run(source, "clean", "-fd", "-e", "build/")

    check = run(source, "apply", "--check", str(patch), check=False)
    if check.returncode == 0:
        run(source, "apply", str(patch))
    else:
        reverse = run(source, "apply", "--reverse", "--check", str(patch), check=False)
        if reverse.returncode != 0:
            raise SystemExit(
                "C++26 overlay no longer applies to upstream stable; review the upstream change\n"
                + check.stderr
            )

    args.state.parent.mkdir(parents=True, exist_ok=True)
    args.state.write_text(
        json.dumps(
            {
                "remote": remote,
                "branch": "stable",
                "commit": run(source, "rev-parse", "HEAD").stdout.strip(),
                "dirty_after_overlay": bool(run(source, "status", "--porcelain").stdout.strip()),
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
