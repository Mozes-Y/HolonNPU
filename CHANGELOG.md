# Changelog

All notable project-level release changes are recorded here.

## Unreleased

### Added

- Added V2 programmable NPU tile architecture planning.
- Added a Holon-owned V2 ISA draft that rejects RVV/RVC encoding constraints
  for the complete V2 program ISA.
- Added a V2 ABI 3.0 program descriptor interface draft.
- Added V2 roadmap phases and decision records for the replaceable frontend
  implementation, explicit scratchpad/DMA memory model, and frontend-issued
  matrix micro-op direction.

## v1.5 - 2026-07-06

### Changed

- Upgraded the matrix engine to v1.1 B-weight-stationary dataflow.
- Raised the ABI contract to 2.0 and descriptor `version=2`.
- Renamed public array capability constants from M/N rows to `ARRAY_K` and
  `ARRAY_N`.
- Renamed the project identity to HolonNPU.
- Renamed the public C API, ABI headers, driver targets, tests, documentation,
  and CI branding to use `holon_npu_*` / `HOLON_NPU_*` names.
- Kept internal RTL `npu_*` names unchanged because they describe generic
  hardware blocks rather than the project brand.
- Refactored product RTL cores to use SystemVerilog interfaces internally and
  limited flattened wrappers to C++/Verilator test harnesses.
- Added an RTL interface-usage checker to CTest and regression.
- Split build and test presets, pinned Ninja, renamed the aggregate lint target
  to `lint`, and moved full regression to a RelWithDebInfo build tree.
- Moved simulation-only SystemVerilog wrappers, test tops, and smoke tops under
  `sim/rtl/` so `rtl/` contains only product/core RTL.
- Added `spec/holon_npu_abi.json` as the single ABI/register/descriptor source
  and generated `npu_pkg.sv`, public C headers, and `docs/INTERFACE.md`.
- Removed the redundant ABI checker wrapper and registered `gen_abi.py --check`
  directly as the ABI generation check.
- Added protocol-first assertions for valid-ready, AXI-Lite, AXI4, control,
  DMA, command, GEMM, and top-level invariants.
- Added an expected-fail assertion smoke test so CI proves assertions are
  enabled.
- Added a coverage preset, Verilator structural coverage collection, and a
  functional coverage gate.
- Refactored C++ coverage support into a stdlib-only typed test runtime with
  `coverage_point` enum values, a constexpr registry, explicit CLI coverage
  configuration, and direct Verilator raw coverage writing.
- Adopted C23 generated ABI constants, native macro-free SVA assertions and
  coverpoints, target-centric CMake source ownership, and conservative default
  CTest preset parallelism.
- Added deterministic constrained-random GEMM/tile shape tests for the GEMM
  accelerator and product top.
- Slimmed progress, decision, and verification documentation around the current
  authoritative baseline and fixed stale post-review documentation references.

## v1 - 2026-06-27

Initial roadmap-complete release.

### Added

- Roadmap-first project governance documentation.
- SystemVerilog RTL for the v1 INT8 GEMM NPU.
- AXI-Lite control plane, descriptor command processor, AXI4 DMA, tiled GEMM
  datapath, scratchpad buffers, and product top.
- Parameterized `16x16` systolic array with signed INT8 input and signed INT32
  output.
- Public C ABI headers and minimal C driver.
- Verilator/C++26 testbenches and CTest integration.
- ABI consistency checker across RTL package constants and public C headers.
- Aggregate lint and regression targets.

### Fixed During Release Hardening

- AXI-Lite write channel handling now accepts AW-before-W and W-before-AW.
- AXI read DMA drains errored bursts through `RLAST` before reporting
  `ERR_AXI_READ`.
- Terminal done/error outputs are restart-epoch aware so clear-and-resubmit
  flows do not observe stale backend terminal state.
- Public C clear-mask constants now match the frozen RTL/documented ABI.
- Product-top tests now cover `16x16x16`, `64x64x64`, descriptor-read error
  recovery, GEMM read/write errors, reset-in-flight recovery, and AXI-Lite
  write-channel skew.

### Known Limits

- v1 supports one descriptor in flight and one outstanding AXI4 burst per DMA
  engine.
- v1 requires 16-byte aligned descriptors, tensor bases, and row strides.
- v1 implements signed INT8 GEMM to signed INT32 output only.
- Vector post-processing, BF16, FP8, graph scheduling, convolution, full
  softmax, LayerNorm, GELU, multiple queues, multiple contexts, and address
  translation are out of scope for v1.
