# Changelog

All notable project-level release changes are recorded here.

## Unreleased

### Added

- Added shared RV32 scalar effect evaluation: all integer arithmetic/branches,
  typed 32-bit physical load/store requests, load completion, CSR/FENCE enables,
  and precise exception/machine-control requests. Existing machine arithmetic
  uses the evaluator; full RV32 boot and routing remain next.
- Added one shared M-mode scalar hart state: schema-derived machine CSRs,
  precise trap/MRET/WFI, interrupts, counters and token-checked memory/fence
  completion. Existing machine arithmetic uses its commit path. No new RTL,
  public capability, physical router or second interpreter is introduced.

- Added schema-generated RV32IM/Zicsr/MRET/WFI decode metadata, typed mixed
  32/64-bit framing, scalar operand extraction/disassembly, exhaustive immediate
  tests, and upstream assembler/compiler cross-checking. This is a semantic
  frontend foundation, not new RTL capability or RV32 program execution.
- Preserved unchanged generated ISA file timestamps so model-stage metadata
  updates do not force unrelated RTL recompilation.
- Added validated autonomous cold boot and budgeted `run_program` execution on
  the semantic machine without a descriptor/device; shared external-memory
  service, mapped 64-bit address tests, 64 vector programs, and tiled GEMM
  writeback checks use the existing ISA semantics.
- Added the C++26 `holon_npu::semantic` core, typed two-phase operation protocol,
  direct runner, and transactional token/error tests.
- Added the upstream `stable` gem5 `DmaDevice`, cycle-accounted timing model,
  RISC-V bare-metal board/guest, Linux full-system board/driver/workload, locked
  offline resources, immutable build metadata, and full-translation-unit C++26
  audit.
- Added zero-stall vector/matrix timing comparisons between Verilator module
  tests and the gem5 timing calculator.
- Added a passing locked Ubuntu 24.04/Linux 6.8.12 full-system baseline with a
  matching simulation driver, DMA/IRQ workload, and retained provenance.
- Added a typed 13-event semantic evidence registry whose events are observed
  only at successful invariant and scoreboard checks.
- Added an idle/quiescent gem5 checkpoint gate that captures and restores in
  separate processes, verifies descriptor/IRQ/cycle state, and submits another
  Holon program after restore.

### Changed

- Selected single-hart M-mode bare-metal execution for the target scalar core,
  with standard trap/CSR/MRET and no U/S mode, MMU, or OS.
- Selected a unified 32-bit physical address space with direct scalar access
  to mapped scratchpad/system memory and explicit DMA for bulk tensor movement;
  execution and memory-map implementation follow the decode foundation.
- Accepted RV32IM + Zicsr scalar compatibility, ILP32, and no C extension for
  the self-hosted target; documented coordinated vector/matrix ISA redesign
  with 32-bit scalar and fixed 64-bit Holon instructions in reclaimed non-RVC
  space, without changing VLA/predication principles. Released RTL/ABI
  capability values remain unchanged; operand fields and detailed execution contracts
  remain under review.
- Made self-hosted functional execution, complete Transformer correctness, and
  autonomous gem5 timing the simulation sequence. Existing Host integration is
  a migration baseline, not the destination (ADR-0058).
- Prevented program completion-token reuse across cold boot and reset; rejected
  boot images and execution-budget exhaustion preserve architectural state.
- Made semantic, gem5 device/timing, RISC-V system, and explicit architecture
  review evidence mandatory before future architecture behavior enters RTL.
- Restored the v2.x through v5 research roadmap without presenting candidate
  BF16, FP8, scaling, context, IOMMU, or multi-tile work as current capability.
- Decoupled the gem5 preset from the Verilator toolchain and configured the
  upstream SCons build for 16-way parallelism without source-tree artifacts.
- Hardened gem5 build/test subprocesses against Python bytecode artifacts and
  fixed versioned GCC 15 toolchain derivation for Linux guest module builds.
- Made scheduled, tag, and explicit CI jobs build the matching Linux kernel
  with 16 workers and run the locked Ubuntu full-system gate.
- Removed raw-integer semantic address overloads and made DMA setup latency part
  of the actual gem5 event timeline rather than statistics-only accounting.
- Made RISC-V guest compiler discovery resilient to versioned toolchain upgrades
  by preferring the stable unversioned compiler driver and repairing stale CMake
  cache entries.

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
