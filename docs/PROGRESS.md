# HolonNPU Progress

This file records the current project state and latest verification result.
Detailed historical logs live in Git history and `CHANGELOG.md`.

## Current Status

- Last updated: 2026-07-06.
- Current baseline: v1.5 final V1-generation release baseline complete.
- Product datapath: v1.1 B-weight-stationary INT8 GEMM with ABI 2.0.
- Build baseline: CMake 4.0+, Ninja, C23 driver/API headers, C++26
  Verilator testbenches.
- Source-of-truth baseline: `spec/holon_npu_abi.json` generates
  `rtl/common/npu_pkg.sv`, public C ABI headers, and `docs/INTERFACE.md`.
- RTL boundary baseline: product RTL uses SystemVerilog interfaces internally;
  flattened wrappers are simulation-only harnesses under `sim/rtl/`.

## Completed Engineering Baseline

- Roadmap-first governance and documentation structure are in place.
- Public ABI, descriptor layout, register map, status/error codes, interrupts,
  and software API contract are generated or documented.
- Common RTL infrastructure, AXI-Lite control, AXI4 DMA, command processor,
  tiled GEMM accelerator, product top, and C driver are implemented.
- Matrix engine uses B-weight-stationary PE weights, A wavefront input, streamed
  psum output, and signed INT32 accumulation.
- Native SystemVerilog assertions and coverpoints protect key valid-ready,
  AXI-Lite, AXI4, control, DMA, command, GEMM, and top-level invariants.
- Functional coverage is typed in C++ and gated through coverage manifests;
  Verilator structural coverage artifacts are generated for inspection.
- CMake is target-centric for source ownership, uses `FILE_SET HEADERS` for
  public headers, and separates build, lint, test, regression, and coverage.

## Latest Verification Matrix

The latest completed local gate passed with:

| Area | Command | Result |
| ---- | ------- | ------ |
| Presets | `python3 -m json.tool CMakePresets.json` | Passed |
| ABI source | `python3 tools/gen_abi.py --check` | Passed |
| RTL boundaries | `python3 tools/check_rtl_interface_usage.py` | Passed |
| Macro policy | `python3 tools/check_macro_policy.py` | Passed |
| Whitespace | `git diff --check` | Passed |
| Debug build | `cmake --preset debug && cmake --build --preset debug --parallel 2` | Passed |
| Lint target | `cmake --build --preset debug --target lint --parallel 2` | Passed |
| Debug tests | `ctest --preset debug --output-on-failure` | Passed `14/14` |
| Lint tests | `ctest --preset lint --output-on-failure` | Passed `8/8` |
| Regression build | `cmake --preset regression && cmake --build --preset regression --parallel 2` | Passed |
| Regression tests | `ctest --preset regression --output-on-failure` | Passed `24/24` |
| Coverage build | `cmake --preset coverage && cmake --build --preset coverage --parallel 2` | Passed |
| Coverage tests | `ctest --preset coverage --output-on-failure` | Passed `25/25` |
| Coverage gate | `python3 tools/check_coverage.py --build-dir build/coverage` | Passed: 11 raw files, 55 functional points |

## Active Limitations

- GEMM only; no convolution, vector engine, BF16, FP8, graph scheduler, or
  post-processing engine is implemented.
- One descriptor may be active at a time.
- AXI4 DMA supports one outstanding read or write burst per engine.
- Tensor and descriptor accesses must satisfy the documented alignment
  requirements.
- Structural coverage reports are produced, but only functional coverage points
  are hard-gated today.
- RTL is validated through Verilator simulation and lint; no FPGA/ASIC
  synthesis flow is part of the current gate.

## Next Engineering Step

- Keep current documentation concise and authoritative.
- For any public ABI or architecture change, update the schema/roadmap/decision
  log before editing RTL or software.
- Future feature work should start at the roadmap level and must not add
  vector, BF16, FP8, multi-queue, multi-context, or scheduler behavior without a
  new accepted decision.
