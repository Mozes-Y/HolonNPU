#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


def read_functional_points(functional_dir: Path) -> set[str]:
    points: set[str] = set()
    for path in sorted(functional_dir.glob("*.txt")):
        for line in path.read_text(encoding="utf-8").splitlines():
            point = line.strip()
            if point:
                points.add(point)
    return points


def read_expected_tests(manifest: Path) -> set[str]:
    if not manifest.is_file():
        raise RuntimeError(f"coverage test manifest is missing: {manifest}")
    tests = {
        line.strip()
        for line in manifest.read_text(encoding="utf-8").splitlines()
        if line.strip()
    }
    if not tests:
        raise RuntimeError(f"coverage test manifest is empty: {manifest}")
    return tests


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


def coverage_field(metadata: str, field: str) -> str | None:
    marker = f"\x01{field}\x02"
    start = metadata.find(marker)
    if start < 0:
        return None
    start += len(marker)
    end = metadata.find("\x01", start)
    return metadata[start:] if end < 0 else metadata[start:end]


def parse_coverage_data(path: Path) -> tuple[dict[str, tuple[int, int]], dict[str, int]]:
    type_counts: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    user_counts: dict[str, int] = defaultdict(int)
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("C '"):
            continue
        metadata_with_prefix, separator, count_text = line.rpartition("' ")
        if not separator or not count_text.isdigit():
            continue
        metadata = metadata_with_prefix[3:]
        point_type = coverage_field(metadata, "t")
        if point_type is None:
            continue
        count = int(count_text)
        type_counts[point_type][1] += 1
        if count > 0:
            type_counts[point_type][0] += 1
        if point_type == "user":
            name = coverage_field(metadata, "o")
            if name is not None:
                user_counts[name] += count
    return ({name: (counts[0], counts[1]) for name, counts in type_counts.items()}, dict(user_counts))


def required_sva_covers(repo_root: Path) -> set[str]:
    names: set[str] = set()
    pattern = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*):\s+cover\s+property\b")
    for path in sorted((repo_root / "rtl").rglob("*.sv")):
        names.update(pattern.findall(path.read_text(encoding="utf-8")))
    return names


def check_structural_baseline(
    metrics: dict[str, tuple[int, int]], baseline_path: Path
) -> tuple[bool, list[str]]:
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    lines: list[str] = []
    passed = True
    for metric, threshold in baseline.items():
        covered, total = metrics.get(metric, (0, 0))
        if total == 0:
            lines.append(f"{metric}: unavailable (required >= {threshold}%)")
            passed = False
            continue
        percentage = 100.0 * covered / total
        lines.append(f"{metric}: {percentage:.1f}% ({covered}/{total}), required >= {threshold}%")
        if percentage < float(threshold):
            passed = False
    return passed, lines


def main() -> int:
    parser = argparse.ArgumentParser(description="Check HolonNPU coverage artifacts.")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--expected-test", action="append", default=[])
    parser.add_argument(
        "--baseline",
        type=Path,
        default=Path("spec/holon_npu_coverage_baseline.json"),
    )
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

    try:
        manifest_tests = read_expected_tests(coverage_dir / "expected_tests.txt")
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    command_tests = set(args.expected_test)
    if command_tests and command_tests != manifest_tests:
        print("coverage test manifest does not match the configured test set", file=sys.stderr)
        return 1

    expected_raw = {f"{name}.dat" for name in manifest_tests}
    actual_raw = {path.name for path in raw_files}
    if actual_raw != expected_raw:
        missing_raw = sorted(expected_raw - actual_raw)
        stale_raw = sorted(actual_raw - expected_raw)
        if missing_raw:
            print("missing raw coverage files:", file=sys.stderr)
            for name in missing_raw:
                print(f"  {name}", file=sys.stderr)
        if stale_raw:
            print("unexpected or stale raw coverage files:", file=sys.stderr)
            for name in stale_raw:
                print(f"  {name}", file=sys.stderr)
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

    metrics, user_counts = parse_coverage_data(coverage_dir / "merged.dat")
    required_covers = required_sva_covers(repo_root)
    missing_covers = sorted(name for name in required_covers if user_counts.get(name, 0) == 0)
    if missing_covers:
        print("unhit RTL cover properties:", file=sys.stderr)
        for name in missing_covers:
            print(f"  {name}", file=sys.stderr)
        return 1

    baseline_path = args.baseline
    if not baseline_path.is_absolute():
        baseline_path = repo_root / baseline_path
    structural_passed, structural_lines = check_structural_baseline(metrics, baseline_path)
    if not structural_passed:
        print("structural coverage baseline failed:", file=sys.stderr)
        for line in structural_lines:
            print(f"  {line}", file=sys.stderr)
        return 1

    summary_path = coverage_dir / "functional_summary.txt"
    summary_path.write_text(
        "\n".join(
            [
                f"raw coverage files: {len(raw_files)}",
                f"functional points hit: {len(hit_points)}",
                f"functional points required: {len(required_points)}",
                f"RTL cover properties hit: {len(required_covers)}",
                "structural coverage:",
                *[f"  {line}" for line in structural_lines],
                "",
                *sorted(hit_points),
                "",
            ]
        ),
        encoding="utf-8",
    )

    print(
        f"Coverage check passed: {len(raw_files)} raw files, "
        f"{len(required_points)} functional points and "
        f"{len(required_covers)} RTL cover properties hit."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
