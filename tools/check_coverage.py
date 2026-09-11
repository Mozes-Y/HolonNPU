#!/usr/bin/env python3
"""Clean and verify GCC coverage of the current C++ implementation."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import subprocess
import time
from pathlib import Path

from check_repository import ROOT, compilation_units


def read_json(path: Path):
    return json.loads(path.read_text())


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def source_digest(root: Path, build: Path) -> str:
    files = [build / "compile_commands.json", build / "coverage-config.json", root / "CMakeLists.txt"]
    for directory in ("sim", "sw", "include", "tests", "spec"):
        files.extend(p for p in (root / directory).rglob("*") if p.suffix in {".cpp", ".hpp", ".c", ".h", ".S", ".ld", ".py", ".json"})
    digest = hashlib.sha256()
    for path in sorted(files):
        # The measured threshold is not part of an executable's semantics.
        if path.name == "coverage_baseline.json":
            continue
        digest.update(str(path).encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()


def prepare(root: Path, build: Path, config: dict, units: dict) -> None:
    for obj, args in units.values():
        if "--coverage" not in args or not obj.with_suffix(".gcno").is_file():
            raise ValueError(f"missing instrumented build: {obj}")
    output = build / "coverage"
    if output.is_symlink():
        raise ValueError("coverage output must not be a symlink")
    if output.exists():
        shutil.rmtree(output)
    for data in (build / "CMakeFiles").rglob("*.gcda"):
        data.unlink()
    if config["toolchain"] == "ON":
        probes = build / "scalar-toolchain"
        if probes.is_symlink():
            raise ValueError("toolchain output must not be a symlink")
        if probes.exists():
            shutil.rmtree(probes)
    output.mkdir()
    write_json(output / "run.json", {
        "start_ns": time.time_ns(), "digest": source_digest(root, build),
        "units": sorted(str(obj.with_suffix(".gcda")) for obj, _ in units.values()),
        "compiler": config["compiler"], "version": config["version"],
    })
    print(f"Prepared clean C++ coverage for {len(units)} translation units.")


def validate_run(build: Path, run: dict, expected: set[Path], digest: str) -> None:
    if run["digest"] != digest or set(map(Path, run["units"])) != expected:
        raise ValueError("coverage source/configuration changed; rebuild and rerun the full preset")
    actual = set((build / "CMakeFiles").rglob("*.gcda"))
    if actual != expected:
        raise ValueError(f"coverage data mismatch: missing={sorted(map(str, expected-actual))}, extra={sorted(map(str, actual-expected))}")
    if any(path.stat().st_mtime_ns < run["start_ns"] for path in actual):
        raise ValueError("stale coverage counters")


def owned_source(root: Path, path: Path) -> bool:
    if not path.is_relative_to(root):
        return False
    relative = path.relative_to(root).as_posix()
    return (relative.startswith(("sim/semantic/", "sw/", "include/")) and
            path.suffix in {".cpp", ".hpp"} and not path.name.endswith("_metadata.hpp"))


def measure(root: Path, build: Path, config: dict, units: dict) -> dict:
    output = build / "coverage"
    run = read_json(output / "run.json")
    expected = {obj.with_suffix(".gcda") for obj, _ in units.values()}
    validate_run(build, run, expected, source_digest(root, build))
    if config["toolchain"] == "ON":
        for name in ("compiler.txt", "mixed-disassembly.txt", "execution-c/execution.txt", "execution-cpp/execution.txt"):
            path = build / "scalar-toolchain" / name
            if not path.is_file() or path.stat().st_mtime_ns < run["start_ns"]:
                raise ValueError(f"missing current toolchain evidence: {name}")
    lines, branches, functions = {}, {}, {}
    raw = output / "raw"
    raw.mkdir(exist_ok=True)
    for source, (obj, _) in sorted(units.items()):
        result = subprocess.run([config["gcov"], "--json-format", "--stdout",
                                 "--branch-probabilities", "--branch-counts", str(obj.with_suffix(".gcno"))],
                                capture_output=True, text=True, check=True)
        if result.stderr.strip():
            raise ValueError(f"gcov diagnostics for {source}: {result.stderr}")
        report = json.loads(result.stdout)
        if report["gcc_version"].split(".")[0] != config["version"].split(".")[0]:
            raise ValueError("gcov report/compiler version mismatch")
        write_json(raw / (source.name + ".json"), report)
        for file in report["files"]:
            path = (Path(report["current_working_directory"]) / file["file"]).resolve()
            if not owned_source(root, path):
                continue
            name = path.relative_to(root).as_posix()
            for line in file["lines"]:
                key = (name, line["line_number"])
                lines[key] = lines.get(key, 0) + line["count"]
                for i, branch in enumerate(line.get("branches", [])):
                    key = (name, line["line_number"], line.get("function_name", ""), i)
                    branches[key] = branches.get(key, 0) + branch["count"]
            for function in file["functions"]:
                key = (name, function["name"])
                functions[key] = functions.get(key, 0) + function["execution_count"]
    implementation = {p.relative_to(root).as_posix() for p in units if owned_source(root, p)}
    if not implementation <= {name for name, _ in lines}:
        raise ValueError("missing implementation source coverage")
    summary = {"compiler": config["compiler"], "version": config["version"],
               "source_digest": run["digest"], "translation_units": len(units), "metrics": {}}
    for metric, counts in (("lines", lines), ("branches", branches), ("functions", functions)):
        total, hit = len(counts), sum(count > 0 for count in counts.values())
        if not total:
            raise ValueError(f"empty {metric} coverage")
        summary["metrics"][metric] = {"hit": hit, "total": total, "percent": 100 * hit / total}
    annotated = output / "annotated"
    if annotated.exists():
        shutil.rmtree(annotated)
    for name in sorted({name for name, _ in lines}):
        target = annotated / (name + ".txt")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("".join(
            f"{str(lines.get((name, number), '-')):>10} | {number:4} | {text}\n"
            for number, text in enumerate((root / name).read_text().splitlines(), 1)))
    write_json(output / "summary.json", summary)
    text = "\n".join(f"{name}: {m['hit']}/{m['total']} ({m['percent']:.2f}%)" for name, m in summary["metrics"].items())
    (output / "summary.txt").write_text(text + "\n")
    print(text)
    return summary


def baseline_failures(summary: dict, baseline: dict) -> list[str]:
    thresholds = baseline["minimum_percent"]
    if set(thresholds) != {"lines", "functions"} or any(not 0 < value <= 100 for value in thresholds.values()):
        raise ValueError("baseline must specify nonzero line/function thresholds")
    return [f"{name} coverage below {minimum}%" for name, minimum in thresholds.items()
            if summary["metrics"][name]["percent"] < minimum]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--prepare", action="store_true")
    mode.add_argument("--record-baseline", action="store_true")
    args = parser.parse_args()
    try:
        build = args.build_dir.resolve()
        config = read_json(build / "coverage-config.json")
        if Path(config["source"]).resolve() != ROOT or not (build / "CMakeCache.txt").is_file():
            raise ValueError("not a configured HolonNPU coverage build")
        units = compilation_units(build)
        if args.prepare:
            prepare(ROOT, build, config, units)
            return 0
        summary = measure(ROOT, build, config, units)
        path = ROOT / "spec/coverage_baseline.json"
        if args.record_baseline:
            proposed = {name: math.floor(summary["metrics"][name]["percent"]) for name in ("lines", "functions")}
            if path.exists() and any(proposed[k] < v for k, v in read_json(path)["minimum_percent"].items()):
                raise ValueError("refusing to lower the measured baseline")
            write_json(path, {"minimum_percent": proposed})
        errors = baseline_failures(summary, read_json(path))
        if errors:
            raise ValueError("; ".join(errors))
        print(f"C++ coverage gate passed ({len(units)} current data files); this is not ISA functional coverage.")
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        print(f"Coverage failed: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
