# HolonNPU Progress

This file records the current project state and latest verification result.
Detailed historical logs live in Git history and `CHANGELOG.md`.

## Current Status

- Last updated: 2026-07-07.
- Current baseline: v1.5 final V1-generation release baseline complete; V2.0
  programmable NPU tile architecture definition is complete and committed.
- Product datapath: v1.1 B-weight-stationary INT8 GEMM with ABI 2.0.
- Planned V2 direction: ABI 3.0 program descriptors, replaceable frontend
  implementation, stable Holon-owned program ISA, integer/quant vector/helper
  engine, explicit scratchpad/DMA memory model, and frontend-issued matrix
  micro-ops.
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
- V2 architecture documents define the programmable NPU tile direction, complete
  Holon ISA ownership, ABI 3.0 program descriptor draft, local-memory ordering,
  frontend lifecycle, matrix micro-op contract, and V2 verification expansion.
- V2.1 has started with machine-checkable Holon ISA metadata in
  `spec/holon_npu_isa.json`, generated public ISA header/reference docs, and
  ISA metadata/generated-source checks.
- V2.1 now also includes machine-checkable ABI 3.0 program metadata in
  `spec/holon_npu_v2_abi.json`, generated public program ABI
  header/reference docs, and V2 ABI schema/generated-source checks. The current
  product RTL still uses the V1.5 ABI 2.0 schema until the V2 loader/control
  plane migration.
- V2.2 has started with a stdlib-only C++26 architectural simulator foundation
  under `sim/model/`. The initial model covers ISA class decode/disassemble,
  ABI 3.0 program descriptor validation, program image/argument loading,
  frontend PC/state/fault progression, local scratchpad, in-order DMA
  load/store events, predicate inactive-lane behavior, vector register state,
  `i32` vector load/store/add/sub/min/max/compare/shift, matrix
  `i8*i8->i32` micro-op effects, deterministic random vector programs, and
  reusable C++ program-builder assembly helpers.

## Latest Verification Matrix

The latest completed V2 metadata/model gate passed with:

| Area | Command | Result |
| ---- | ------- | ------ |
| ABI source | `python3 tools/gen_abi.py --check` | Passed |
| ISA source | `python3 tools/gen_isa.py --check` | Passed |
| ISA metadata | `python3 tools/check_isa.py` | Passed |
| V2 ABI source | `python3 tools/gen_v2_abi.py --check` | Passed |
| V2 ABI schema | `python3 tools/check_v2_abi.py` | Passed |
| Macro policy | `python3 tools/check_macro_policy.py` | Passed |
| RTL boundaries | `python3 tools/check_rtl_interface_usage.py` | Passed |
| Whitespace | `git diff --check` | Passed |
| V2 doc whitespace | trailing-whitespace scan for `docs/V2_*.md` | Passed |
| V2 stale terms | grep for legacy scalar/vector boundary wording | Passed: no hits |
| V2 required terms | grep for V2 ISA/ABI/lifecycle/ordering/model terms | Passed |
| Debug build | `cmake --preset debug && cmake --build --preset debug --parallel 2` | Passed |
| Focused V2 ABI/ISA tests | `ctest --preset debug -R 'v2_abi\|isa\|holon_npu_driver' --output-on-failure` | Passed `5/5` |
| V2 model build | `cmake --build --preset debug --target holon_npu_v2_model_test --parallel 2` | Passed |
| V2 model test | `ctest --preset debug -R holon_npu_v2_model --output-on-failure` | Passed `1/1`, including ABI 3.0 descriptor loading, DMA events, predicate vector ALU compare/shift, deterministic random vector programs, program-builder assembly helpers, and matrix micro-op effects |
| Debug tests | `ctest --preset debug --output-on-failure` | Passed `19/19` |
| Lint tests | `ctest --preset lint --output-on-failure` | Passed `8/8` |
| Regression build | `cmake --preset regression && cmake --build --preset regression --parallel 2` | Passed |
| Regression tests | `ctest --preset regression --output-on-failure` | Passed `29/29` |
| Coverage build | `cmake --preset coverage && cmake --build --preset coverage --parallel 2` | Passed |
| Coverage tests | `ctest --preset coverage --output-on-failure` | Passed `30/30` |
| Coverage gate | `python3 tools/check_coverage.py --build-dir build/coverage` | Passed: 11 raw files, 55 functional points |

The latest completed V1.5 release gate passed with:

| Area | Command | Result |
| ---- | ------- | ------ |
| Presets | `python3 -m json.tool CMakePresets.json` | Passed |
| ABI source | `python3 tools/gen_abi.py --check` | Passed |
| RTL boundaries | `python3 tools/check_rtl_interface_usage.py` | Passed |
| Macro policy | `python3 tools/check_macro_policy.py` | Passed |
| Whitespace | `git diff --check` | Passed |
| Debug build | `cmake --preset debug && cmake --build --preset debug --parallel 2` | Passed |
| Lint target | `cmake --build --preset debug --target lint --parallel 2` | Passed |
| Debug tests | `ctest --preset debug --output-on-failure` | Passed `16/16` |
| Lint tests | `ctest --preset lint --output-on-failure` | Passed `8/8` |
| Regression build | `cmake --preset regression && cmake --build --preset regression --parallel 2` | Passed |
| Regression tests | `ctest --preset regression --output-on-failure` | Passed `26/26` |
| Coverage build | `cmake --preset coverage && cmake --build --preset coverage --parallel 2` | Passed |
| Coverage tests | `ctest --preset coverage --output-on-failure` | Passed `27/27` |
| Coverage gate | `python3 tools/check_coverage.py --build-dir build/coverage` | Passed: 11 raw files, 55 functional points |

## Active Limitations

- GEMM only in current RTL; no V2 frontend RTL, vector RTL, ABI 3.0 product RTL
  control plane, BF16, FP8, graph scheduler, or post-processing engine is
  implemented. The V2 C++ model is a simulator foundation, not RTL.
- One descriptor may be active at a time.
- AXI4 DMA supports one outstanding read or write burst per engine.
- Tensor and descriptor accesses must satisfy the documented alignment
  requirements.
- Structural coverage reports are produced, but only functional coverage points
  are hard-gated today.
- RTL is validated through Verilator simulation and lint; no FPGA/ASIC
  synthesis flow is part of the current gate.

## Next Engineering Step

- Continue V2 Phase V2.2 by expanding the C++ architectural simulator with
  additional vector operation classes and richer matrix edge cases.
- V2 RTL control-plane work must not begin until the frontend implementation
  boundary, ISA metadata, program descriptor schema, lifecycle state machine,
  and memory ordering rules remain green under the static checks.
