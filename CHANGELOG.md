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
- Added machine-checkable Holon V2 ISA metadata, a generated public ISA header,
  a generated ISA reference document, and ISA metadata/source checks.
- Added a generated SystemVerilog ISA constants package for RTL consumers of
  the Holon V2 ISA metadata.
- Added machine-checkable V2 ABI 3.0 program descriptor metadata, generated
  public program ABI header/reference docs, and V2 ABI schema/source checks.
- Added the first V2 C++26 architectural simulator foundation with decode,
  local scratchpad, vector register state, vector arithmetic/compare/shift
  execution, and focused simulator tests.
- Added deterministic constrained-random V2 vector program tests against C++
  reference semantics.
- Added a reusable V2 C++ program builder for architectural simulator tests.
- Added V2 simulator matrix micro-op architectural effects for INT8 GEMM
  clear/accumulate behavior, event emission, and issue faults.
- Added generated V2 RTL ABI constants and the first ABI 3.0 AXI-Lite
  control/lifecycle RTL skeleton with focused Verilator tests.
- Added the first V2 program loader RTL slice for AXI4 descriptor fetch,
  ABI/ISA compatibility validation, descriptor field extraction, and focused
  Verilator tests.
- Extended the V2 program loader to read program image and argument blocks with
  32-bit AXI4 narrow bursts and emit local program/data write streams.
- Added a focused V2 control-plane integration RTL slice connecting AXI-Lite
  lifecycle control to the program loader, with Verilator and lint coverage.
- Added the initial V2 local program/data memory RTL boundary behind the loader,
  including local readback tests and control-plane integration coverage.
- Added the first V2 reference frontend integration slice, including local
  program-memory fetch, `system.exit`, `system.fault`, illegal-instruction
  faulting, and restart-epoch terminal-event gating.
- Extended V2 ISA metadata with implemented `system.exit`, `system.fault`,
  `dma.load`, and `dma.store` instruction opcodes plus generated field-layout
  constants.
- Added the first frontend-issued V2 DMA load/store fabric, including AXI4 read
  bursts, AXI4 write bursts, scratchpad writeback/readout, backpressure,
  local-bounds fault, AXI read/write fault, and frontend-tile program coverage.
- Added metadata-owned V2 sync ordering instructions for `sync.wait_dma`,
  `sync.fence.local`, and `sync.fence.dma`, with C++ model execution, RTL
  frontend retirement, frontend-tile tests, and functional coverage.
- Added the first V2 data scratchpad port arbiter with a local read
  valid-ready interface, loader/client write arbitration, host/client read
  round-robin routing, RTL assertions, and functional coverage.
- Added metadata-owned V2 vector config/memory/ALU instruction opcodes and the
  first standalone vector engine RTL slice for type-orthogonal configuration,
  contiguous local load/store, add/sub/min/max, compare, shift, and vector
  fault coverage.
- Connected the reference frontend to the vector engine through a backpressured
  issue/result contract, added DMA/vector scratchpad arbitration, and added a
  program-level vector test differential-checked against the C++26 model.
- Split the V2 integer-vector and quant-vector capability bits and restricted
  capability/operation-class reset values to functionality present in the
  current RTL integration.
- Made vector opcodes type-orthogonal: vector configuration now selects VL,
  element width, and signedness for generic memory and ALU operations across
  signed/unsigned i8, i16, and i32 elements.
- Added a dedicated interface-native local-memory write contract with byte
  strobes and explicit responses, plus narrow-element RTL/model tests.
- Added metadata-owned predicate ptrue/load instructions, explicit predicate
  selection for vector ALU and memory operations, inactive-lane preservation,
  masked stores, ABI capability reporting, and RTL/model differential tests.
- Completed the Holon frontend-control path with scalar register arithmetic,
  aligned local load/store, branches, precise faults, halt/resume, and debug
  stepping.
- Completed the integer/quant helper path with saturating arithmetic, select,
  gather, zip/unzip, 4x4 transpose, reductions, and fixed-point requantization.
- Added the V2 matrix micro-op engine around the existing B-weight-stationary
  systolic array, including clear/accumulate/store modes and issue faults.
- Added the ABI 3.0 product top, AXI write arbitration, and ordered 32-byte
  completion records that precede terminal MMIO state and IRQ visibility.
- Defined all program descriptor flag semantics for terminal IRQ policy,
  counter reset, and fault debug snapshots.
- Added the public C++26 `holon_npu_runtime` program builder and example vector
  add, ReLU, reduction, requantization, transpose, and INT8 GEMM programs.
- Added executable CSR/debug reads for PC, retired count, image size, and active
  local-memory size, with precise invalid-selector faults in model and RTL.
- Made ISA metadata coverage machine-checkable against the typed C++ registry
  and rejected implemented ISA classes without instructions.
- Added a validated public tiled-GEMM planner that emits firmware-owned M/N/K
  matrix micro-op traversal, including K-tile accumulation.
- Added deterministic random vector RTL/model differential programs, random
  signed INT8 matrix tiles with padded strides, unchanged public runtime-example
  execution in RTL, and integrated `17x19x23`/`64x64x64` tiled GEMM tests.
- Expanded V2 module, integration, lint, regression, and functional coverage
  gates across control, loader, local memory, frontend, DMA, vector, matrix,
  completion, and product top behavior.

### Changed

- Replaced the initial immediate-limited DMA instruction slice with
  register-addressed 64-bit system and 32-bit local addressing.
- Strengthened the RTL ownership gate so every product or simulation
  SystemVerilog source has exactly one consumed semantic CMake source target.
- Routed sync instructions through the explicit frontend issue handshake and
  retired them only after acknowledgement.
- Unified RTL, C23 driver, and C++26 model validation for wrapped 64-bit memory
  ranges and joint argument/stack local-memory allocation.

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
