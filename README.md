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

## Requirements

- CMake 4.0 or newer
- Ninja
- Verilator with SystemVerilog assertion and coverage support
- A C23 compiler
- A C++26 compiler
- Python 3

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

Build or run one test without adding presets:

```bash
cmake --build --preset debug --target npu_top_tb
ctest --preset debug -R '^npu_top$' --verbose
```

`CMakePresets.json` intentionally contains only debug, regression, and coverage
build trees plus debug, lint, regression, and coverage test entry points.

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
- a C++ architectural model for ISA behavior;
- directed AXI 4 KiB boundary and soft-reset drain tests;
- event-driven typed functional coverage;
- nonzero named RTL `cover property` checks;
- line, branch, toggle, and expression coverage baselines.

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
| `sim/model/` | C++26 architectural model. |
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
