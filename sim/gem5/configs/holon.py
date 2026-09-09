"""Autonomous Holon execution; no Host CPU, MMIO peripheral or driver."""
import argparse
import json
from pathlib import Path

import m5
from m5.objects import AddrRange, HolonNpu, NoncoherentXBar, Root, SimpleMemory, SrcClockDomain, System, VoltageDomain

parser = argparse.ArgumentParser()
parser.add_argument("--fixture", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--memory-latency", default="20ns")
parser.add_argument("--memory-bandwidth", default="4GiB/s")
parser.add_argument("--vector-lanes", type=int, default=16)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
reference = json.loads((args.fixture / "reference.json").read_text())
system = System()
system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=VoltageDomain())
system.mem_mode = "timing"
system.mem_ranges = [AddrRange(0x80000000, size=reference["memory_bytes"])]
system.membus = NoncoherentXBar(width=16, frontend_latency=3, forward_latency=4, response_latency=2)
system.system_port = system.membus.cpu_side_ports
system.memory = SimpleMemory(range=system.mem_ranges[0], latency=args.memory_latency, bandwidth=args.memory_bandwidth)
system.memory.port = system.membus.mem_side_ports
system.npu = HolonNpu(
    program_file=str(args.fixture / "program.bin"), data_file=str(args.fixture / "input.bin"),
    output_file=str(args.output / "memory.bin"), report_file=str(args.output / "execution.json"),
    memory_bytes=reference["memory_bytes"], vector_bytes=reference["vector_bytes"], vector_lanes=args.vector_lanes,
)
system.npu.memory = system.membus.cpu_side_ports
root = Root(full_system=False, system=system)
m5.instantiate()
event = m5.simulate(10**10)
m5.stats.dump()
if event.getCause() != "Holon stopped" or event.getCode() != 0:
    raise RuntimeError(f"autonomous execution failed: {event.getCause()} / {event.getCode()}")
actual = json.loads((args.output / "execution.json").read_text())
if actual["reason"] != "stopped" or actual["status"] or actual["traps"] != reference.get("traps", 0):
    raise RuntimeError(f"bad execution outcome: {actual}")
for field in ("pc", "retired"):
    if actual[field] != reference[field]:
        raise RuntimeError(f"architectural {field}: {actual[field]} != {reference[field]}")
if (args.output / "memory.bin").read_bytes() != (args.fixture / "expected.bin").read_bytes():
    raise RuntimeError("gem5 memory effects differ from verified guest execution")
print(f"Holon autonomous guest PASS: {actual}")
