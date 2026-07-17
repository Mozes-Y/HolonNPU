# Changelog

All notable project-level release changes are recorded here.

## Unreleased

No changes yet.

## v2.0 - 2026-07-18

### Added

- Added the programmable Holon ISA 1.0 frontend, ABI 3.0 program lifecycle,
  local program/data memory, frontend-issued DMA, integer/quant vector engine,
  matrix micro-op engine, completion records, C++26 architectural model, and
  public C23/C++26 software runtime.
- Added canonical ABI and ISA schemas that generate RTL packages, public
  headers, and reference documentation.
- Added native SVA, deterministic program/model differential tests, directed
  AXI page-boundary and reset-drain tests, typed functional event coverage,
  named RTL cover gating, exact raw-artifact checking, and structural coverage
  baselines.

### Changed

- Converged `master` to one canonical programmable NPU product line; removed the
  descriptor-driven GEMM product path, ABI 2.0 headers/schema, unused RTL,
  compatibility aliases, and architecture-versioned names. Historical source
  remains available from tag `v1.5`.
- Replaced GEMM descriptor submission with ABI 3.0 program descriptors and
  Holon program execution.
- Made ISA operation classes authoritative in the ISA schema and derived ISA
  version/capability masks in the ABI generator.
- Made all AXI masters split transactions at 4 KiB boundaries, including
  descriptor, code, argument, DMA, and completion traffic.
- Changed software reset to observable `RESETTING` with safe AXI/local-memory
  quiescence and added `holon_npu_wait_idle()`.
- Fixed `SYSTEM_FAULT` to always report `EXPLICIT_PROGRAM_FAULT`; nonzero
  reserved immediates now produce `ILLEGAL_INSTRUCTION`.
- Removed the product scratchpad test-read path. Program-level tests observe
  results through architectural DMA stores.
- Replaced test-exit coverage declarations with monitor/scoreboard evidence and
  clean-run manifests.

## v1.5 - 2026-07-06

Final release of the descriptor-driven INT8 GEMM generation.

### Added And Changed

- B-weight-stationary `16x16` systolic array with INT8 inputs and INT32 output.
- ABI 2.0 GEMM descriptor, AXI-Lite control, AXI4 DMA, tiled scratchpad path,
  C driver, and golden-model integration tests.
- Interface-native product RTL with test-only flattened wrappers under
  `sim/rtl/`.
- Schema-generated ABI artifacts, native SVA, C++26 typed coverage runtime,
  deterministic random tile tests, and debug/regression/coverage presets.
- C23 macro-free public constants and target-centric CMake 4.0 build structure.

### Known Limits

- One descriptor and one active AXI transaction per DMA direction.
- Explicit scratchpad movement; no coherence, IOMMU, vector engine, or floating
  point.

## v1 - 2026-06-27

Initial integrated INT8 GEMM accelerator release with AXI-Lite control, AXI4
DMA, descriptor execution, software driver, Verilator tests, lint, and release
documentation.
