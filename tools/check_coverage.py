#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def read_functional_points(functional_dir: Path) -> set[str]:
    points: set[str] = set()
    for path in sorted(functional_dir.glob("*.txt")):
        for line in path.read_text(encoding="utf-8").splitlines():
            point = line.strip()
            if point:
                points.add(point)
    return points


def run_verilator_coverage(raw_files: list[Path], coverage_dir: Path, repo_root: Path) -> None:
    tool = shutil.which("verilator_coverage")
    if tool is None:
        raise RuntimeError("verilator_coverage was not found in PATH")

    merged = coverage_dir / "merged.dat"
    info = coverage_dir / "merged.info"
    annotate_dir = coverage_dir / "annotated"
    summary = coverage_dir / "summary.txt"

    subprocess.run([tool, "--write", str(merged), *map(str, raw_files)], check=True, cwd=repo_root)
    subprocess.run([tool, "--write-info", str(info), str(merged)], check=True, cwd=repo_root)
    annotate_dir.mkdir(parents=True, exist_ok=True)
    with summary.open("w", encoding="utf-8") as out:
        subprocess.run(
            [tool, "--annotate", str(annotate_dir), str(merged)],
            check=True,
            cwd=repo_root,
            stdout=out,
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Check HolonNPU coverage artifacts.")
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    coverage_dir = args.build_dir / "coverage"
    raw_dir = coverage_dir / "raw"
    functional_dir = coverage_dir / "functional"
    required_dir = functional_dir / "required"
    hit_dir = functional_dir / "hit"
    coverage_dir.mkdir(parents=True, exist_ok=True)

    raw_files = sorted(raw_dir.glob("*.dat"))
    if not raw_files:
        print(f"no Verilator coverage data found in {raw_dir}", file=sys.stderr)
        return 1

    required_points = read_functional_points(required_dir)
    if not required_points:
        print(f"no required functional coverage manifests found in {required_dir}", file=sys.stderr)
        return 1

    hit_points = read_functional_points(hit_dir)
    missing = sorted(required_points - hit_points)
    if missing:
        print("missing functional coverage points:", file=sys.stderr)
        for point in missing:
            print(f"  {point}", file=sys.stderr)
        return 1

    try:
        run_verilator_coverage(raw_files, coverage_dir, repo_root)
    except subprocess.CalledProcessError as exc:
        print(f"verilator_coverage failed: {exc}", file=sys.stderr)
        return exc.returncode
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    summary_path = coverage_dir / "functional_summary.txt"
    summary_path.write_text(
        "\n".join(
            [
                f"raw coverage files: {len(raw_files)}",
                f"functional points hit: {len(hit_points)}",
                f"functional points required: {len(required_points)}",
                "",
                *sorted(hit_points),
                "",
            ]
        ),
        encoding="utf-8",
    )

    print(
        f"Coverage check passed: {len(raw_files)} raw files, "
        f"{len(required_points)} required functional points hit."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
