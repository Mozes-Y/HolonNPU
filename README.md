# HolonNPU

HolonNPU is a programmable integer and quantized NPU tile implemented in
SystemVerilog. The current mainline executes the Holon program ISA through a
replaceable frontend implementation and exposes vector, matrix, DMA, local
memory, lifecycle, debug, and completion facilities through ABI 3.0.

The repository follows one product line. The former descriptor-driven GEMM
accelerator is preserved by the `v1.5` Git tag and is not built or maintained in
the current tree.

## Current Architecture

- Holon ISA 1.0 with fixed 32-bit instructions.
- ABI 3.0 program descriptors and AXI-Lite lifecycle control.
- Local program memory and data scratchpad loaded through AXI4.
- Reference frontend with control flow, scalar registers, CSR/debug, predicate,
  DMA, vector, matrix, synchronization, and system instruction classes.
- Integer/quant vector engine with VLA predicate semantics.
- B-weight-stationary INT8 matrix engine with INT32 accumulation.
- Frontend-issued DMA load/store commands and ordered completion records.
- Safe software reset through observable `RESETTING` and transaction drain.
- Interface-native product RTL; flattened wrappers exist only under `sim/rtl/`.

The execution path is:

1. Software creates a program image, argument block, and program descriptor.
2. Software writes the descriptor address and rings the AXI-Lite doorbell.
3. Hardware validates the descriptor and loads code and arguments.
4. The frontend executes Holon instructions and issues DMA/vector/matrix work.
5. The program stores results to system memory through DMA.
6. Hardware writes the optional completion record before exposing `DONE` or
   `FAULT`.

See [Architecture](docs/ARCHITECTURE.md), [ISA](docs/ISA.md), and
[Interface](docs/INTERFACE.md) for the normative contracts.

## Simulator-First Development

New architecture behavior does not enter RTL first. It must be implemented and
tested in one C++26 Holon semantic core, evaluated through the Holon gem5
SimObject, and approved by an architecture ADR before RTL work begins. The fast
runner and gem5 use the same semantics; gem5 is the only performance and
full-system model.

The simulation foundation uses the upstream `stable` branch, builds the
complete simulator and Holon EXTRAS in audited C++26 mode, and runs a RISC-V
bare-metal system through the same semantic core used by fast tests. gem5 owns
cycle accounting, memory latency, DMA integration, IRQ delivery, and structured
statistics. This is the existing accelerator baseline, not the self-hosted
destination. Active work is autonomous functional boot/execution, a complete
minimal Transformer, then a no-Host gem5 execution and performance model.
See the [Simulation Contract](docs/SIMULATION.md) for ownership and acceptance.

## Architecture Roadmap

| Generation | Evidence-driven direction |
| ---------- | ------------------------- |
| v2.x | Autonomous semantic execution, a complete minimal Transformer, then self-hosted gem5 timing without a Host/DmaDevice. |
| v3 | Explore Transformer and BF16 requirements after workload characterization. |
| v4 | Explore FP8 and block/MX scaling only after numerical and system-level evidence. |
| v5 | Explore contexts, IOMMU, queues, and multi-tile scaling when system workloads justify them. |

These are research directions, not current capabilities or frozen interfaces.
The authoritative requirements, gates, and non-goals are in the
[Roadmap](docs/ROADMAP.md).

## Requirements

- CMake 4.0 or newer
- Ninja
- Verilator with SystemVerilog assertion and coverage support
- A C23 compiler
- A C++26 compiler
- Python 3

The gem5 gate additionally requires SCons, gem5's host dependencies, and a GCC
15 RISC-V cross compiler.

## Build And Test

Configure and build a development tree:

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug --output-on-failure
```

Run RTL lint separately:

```bash
ctest --preset lint --output-on-failure
```

Run the optimized full regression:

```bash
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression --output-on-failure
```

Run the instrumented coverage gate:

```bash
cmake --preset coverage
cmake --build --preset coverage --parallel 2
ctest --preset coverage --output-on-failure
python3 tools/check_coverage.py --build-dir build/coverage
```

Build and run the simulation foundation. The outer build and the upstream SCons
build both use up to 16 workers:

```bash
cmake --preset gem5
cmake --build --preset gem5 --parallel 16
ctest --preset gem5 --output-on-failure
```

The fast gem5 gate audits every effective C++ translation unit, records the
exact upstream `stable` SHA and overlay hash, and runs timing plus RISC-V
bare-metal vector, matrix, DMA, completion, IRQ, fault, reset, and
separate-process idle-checkpoint scenarios.
Linux full-system support is a separate nightly/release gate and requires the
locked resource set and a matching guest kernel/module bundle. The preparation
tools verify every downloaded artifact and build the matching Linux 6.8.12
kernel with up to 16 workers; the simulation itself never queries the online
gem5 resource catalog. A passing Ubuntu 24.04/Linux 6.8.12 baseline is recorded
in [Progress](docs/PROGRESS.md). See the
[Simulation Contract](docs/SIMULATION.md#linux-full-system-gate) for the
complete commands.

Build or run one test without adding presets:

```bash
cmake --build --preset debug --target npu_top_tb
ctest --preset debug -R '^npu_top$' --verbose
```

`CMakePresets.json` intentionally contains only debug, regression, coverage,
and gem5 build trees plus their corresponding test entry points.

## Schema-Generated Contracts

The public ABI and ISA are not manually duplicated:

- `spec/holon_npu_abi.json` owns ABI 3.0 registers, descriptors, status,
  faults, capabilities, and completion records.
- `spec/holon_npu_isa.json` owns ISA encodings, instruction classes, operation
  classes, field layouts, and semantic metadata.
- `tools/gen_abi.py` and `tools/gen_isa.py` generate RTL packages, C23/C++
  headers, and reference documentation.

Check generated outputs with:

```bash
python3 tools/gen_abi.py --check
python3 tools/gen_isa.py --check
python3 tools/check_isa.py
```

Edit schemas, not generated files.

## Verification

The release gate combines:

- native SystemVerilog assertions for AXI, valid-ready, lifecycle, bounds, and
  engine invariants;
- deterministic C++26 module and program tests;
- one C++26 semantic core shared by the direct runner and gem5;
- directed AXI 4 KiB boundary and soft-reset drain tests;
- event-driven typed functional coverage;
- nonzero named RTL `cover property` checks;
- line, branch, toggle, and expression coverage baselines.

Future architecture work requires autonomous semantic execution, whole-workload
correctness, and no-Host gem5 timing/memory-system evidence before RTL. Existing
RISC-V device/system tests verify the accelerator migration baseline only.

Coverage artifacts are recreated for every coverage run. The checker rejects
missing and stale raw files, missing functional events, unhit RTL cover
properties, and structural regressions below
`spec/holon_npu_coverage_baseline.json`.

## Repository Layout

| Path | Responsibility |
| ---- | -------------- |
| `spec/` | Canonical ABI, ISA, and coverage baseline metadata. |
| `rtl/` | Current synthesizable product RTL and interfaces. |
| `sim/rtl/` | Simulation-only flattened wrappers and test tops. |
| `sim/semantic/` | C++26 architecture semantics and direct runner. |
| `sim/gem5/` | External gem5 SimObject, timing, RISC-V systems, and simulation-only guests. |
| `sim/` | Verilator testbenches and typed coverage runtime. |
| `include/` | Generated public contracts and public C++ runtime API. |
| `sw/` | C23 driver and C++26 program runtime implementation. |
| `tests/` | Host-side model, runtime, and driver tests. |
| `tools/` | Generation, structure, policy, and coverage checks. |
| `docs/` | Current architecture, interface, verification, decisions, and roadmap. |

## Engineering Rules

- `master` contains one canonical product architecture.
- Product identifiers and targets do not carry architecture-version prefixes.
- Product RTL uses SystemVerilog interfaces internally.
- Simulation wrappers never become product interconnect.
- Public contract changes begin in schema and documentation.
- New architecture behavior is semantic-core and gem5 validated before RTL.
- New RTL must be reachable from `npu_top`; simulation-only consumers do not
  justify product RTL.
- C23 and C++26 project code does not use project-defined behavior macros.

## Current Limits

- One active program and one in-order command at a time.
- One active AXI transaction per DMA engine.
- Explicit scratchpad/DMA memory management; no coherent cache or IOMMU.
- Integer and quantized data paths only; no BF16 or FP8.
- No multiple contexts, graph scheduler, or multi-tile scaling.

## License

See [LICENSE](LICENSE).
