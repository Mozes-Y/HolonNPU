#!/usr/bin/env python3
"""Run verified autonomous guest programs and timing sensitivity gates."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path


def run_guest(args, fixture, run, latency="20ns", lanes=16, bandwidth="4GiB/s"):
    run.mkdir()
    with (run / "simout").open("w") as stdout, (run / "simerr").open("w") as stderr:
        subprocess.run([
            str(args.gem5.resolve()), f"--outdir={run}", str(args.config.resolve()),
            "--fixture", str(fixture), "--output", str(run),
            "--memory-latency", latency, "--vector-lanes", str(lanes),
            "--memory-bandwidth", bandwidth,
        ], stdout=stdout, stderr=stderr, check=True, timeout=180)
    reference = json.loads((fixture / "reference.json").read_text())
    result = json.loads((run / "execution.json").read_text())
    if result["reason"] != "stopped" or result["status"] or result["traps"] != reference.get("traps", 0):
        raise ValueError(f"{run.name}: unexpected execution outcome {result}")
    stats = {}
    for line in (run / "stats.txt").read_text().splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].startswith("system.npu.holon."):
            stats[fields[0].removeprefix("system.npu.holon.")] = float(fields[1])
    domains = ("frontend", "local", "vector", "matrix", "memory_setup", "sync", "memory_wait")
    ticks = sum(stats[f"elapsedTicks::{domain}"] for domain in domains)
    if ticks != result["ticks"] or stats["elapsedTicks::memory_wait"] != stats["memoryTicks"]:
        raise ValueError(f"{run.name}: elapsed-time ledger does not reconcile with execution")
    return result, stats


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gem5", type=Path, required=True)
    parser.add_argument("--exporter", type=Path, required=True)
    parser.add_argument("--memory-exporter", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.name != "autonomous":
        raise ValueError("output must be a dedicated directory named autonomous")
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    fixture = output / "fixture"
    subprocess.run([str(args.exporter.resolve()), f"--export={fixture}"], check=True)
    results = {}
    cases = (
        ("baseline", "20ns", 16, "4GiB/s"),
        ("slow_memory", "100ns", 16, "4GiB/s"),
        ("narrow_vector", "20ns", 1, "4GiB/s"),
        ("backpressure", "20ns", 16, "64MiB/s"),
    )
    for name, latency, lanes, bandwidth in cases:
        run = output / name
        result, stats = run_guest(args, fixture, run, latency, lanes, bandwidth)
        for key, expected in {"retired": 993, "matrixMacs": 552, "bytes": 16384, "transactions": 256}.items():
            if stats.get(key) != expected:
                raise ValueError(f"{name}: {key}={stats.get(key)}, expected {expected}")
        if stats.get("activeLanes", 0) <= 0 or stats.get("memoryTicks", 0) <= 0:
            raise ValueError(f"{name}: missing engine/memory activity")
        results[name] = {**result, "memory_ticks": stats["memoryTicks"], "vector_cycles": stats["resourceCycles::vector"], "retries": stats["retries"]}
    baseline = results["baseline"]
    if results["slow_memory"]["cycles"] <= baseline["cycles"] or results["slow_memory"]["memory_ticks"] <= baseline["memory_ticks"]:
        raise ValueError("memory latency sensitivity did not affect execution")
    if results["narrow_vector"]["cycles"] <= baseline["cycles"] or results["narrow_vector"]["vector_cycles"] <= baseline["vector_cycles"]:
        raise ValueError("vector width sensitivity did not affect execution")
    if results["backpressure"]["retries"] <= baseline["retries"] or results["backpressure"]["cycles"] <= baseline["cycles"]:
        raise ValueError("constrained bandwidth did not exercise retry and execution stalls")
    (output / "sensitivity.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))
    memory_fixture = output / "memory-fixture"
    subprocess.run([str(args.memory_exporter.resolve()), f"--export={memory_fixture}"], check=True)
    memory_results = {}
    for offset in (0xff0, 0xff3, 0xffc):
        fixture = memory_fixture / str(offset)
        reference = json.loads((fixture / "reference.json").read_text())
        result, stats = run_guest(args, fixture, output / f"memory-{offset:x}", bandwidth="64MiB/s")
        for key in ("retired", "traps", "bytes", "transactions"):
            if stats.get(key) != reference[key]:
                raise ValueError(f"memory-{offset:x}: {key}={stats.get(key)}, expected {reference[key]}")
        if stats["retries"] <= 0:
            raise ValueError(f"memory-{offset:x}: constrained memory did not exercise retry")
        memory_results[f"{offset:x}"] = {**result, "bytes": stats["bytes"], "transactions": stats["transactions"], "retries": stats["retries"]}
    (output / "memory-boundaries.json").write_text(json.dumps(memory_results, indent=2) + "\n")
    print(json.dumps(memory_results, indent=2))
    print("Autonomous gem5 Transformer, timing sensitivity and memory boundaries PASS")


if __name__ == "__main__":
    main()
