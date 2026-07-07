# HolonNPU Roadmap

This document is the single source of truth for planned work. Code changes must
follow the active phase described here. Interface changes must be documented
before RTL or software starts depending on them.

## Engineering Discipline

Before each implementation phase:

1. Read `docs/ROADMAP.md` and `docs/PROGRESS.md`.
2. Identify the active phase, allowed edit scope, dependencies, and acceptance
   criteria.
3. Keep changes within the active phase. Do not pull future functionality into
   the current milestone.

After each completed phase:

1. Review the phase checklist.
2. Run the required build, test, and lint commands for that phase.
3. Update `docs/PROGRESS.md` with completed work, exact commands, results,
   remaining issues, and the next phase.
4. If the roadmap changes, update this file and `docs/DECISIONS.md` before
   changing code.

Definition of done for any implementation phase:

- Code and documents for the phase are complete.
- Required simulation, static checks, or tests pass, or failures are explicitly
  recorded with a blocking reason.
- Interfaces in code and documentation match.
- There are no unexplained warnings, TODOs, or known inconsistencies.

Prohibited for v1:

- Code changes without synchronized progress updates.
- Interface changes before documentation.
- Temporary bypasses that violate the architecture to satisfy a test.
- Vector engines, BF16, FP8, graph scheduling, convolution, full softmax,
  LayerNorm, or GELU implementations.

## Product Goal

v1.5 is the final V1-generation RISC-V controlled INT8 GEMM accelerator exposed
through an `AXI-Lite` control plane and `AXI4` DMA data movement. The matrix
core is a parameterized `16x16` B-weight-stationary systolic array with INT32
accumulation. It includes generated ABI artifacts, protocol assertions,
functional coverage gating, and deterministic constrained-random tile
verification without changing the ABI 2.0 values.

The implementation stack is fixed:

- RTL: SystemVerilog.
- Simulation: Verilator.
- Testbench: C++26.
- Build system: CMake 4.0 or newer.

## Version Roadmap

### v1: INT8 GEMM NPU

Scope:

- AXI-Lite register control.
- AXI4 descriptor fetch and tensor DMA.
- Signed INT8 A/B operands.
- Signed INT32 C output.
- Parameterized systolic array, with `16x16` as the target configuration.
- Single descriptor execution semantics.
- Minimal C driver API for RISC-V firmware integration.

Non-goals:

- Vector post-processing.
- BF16, FP8, MX formats, or other low precision floating formats.
- Multiple descriptor queues, multiple contexts, IOMMU integration, or multi-NPU
  tile scaling.

### v1.1: B-Weight-Stationary Matrix Engine

Scope:

- ABI 2.0 with descriptor `version=2`.
- B tile weights are stationary inside the matrix PE array.
- A tile values stream across the array as a wavefront.
- INT32 partial sums stream through K lanes and accumulate in a C tile buffer.
- Public capability naming uses `ARRAY_K` and `ARRAY_N`.

Non-goals:

- Compatibility with ABI 1.0 descriptors.
- Runtime-selectable matrix dataflow.
- Vector post-processing, BF16, FP8, multiple queues, or multi-context support.

### v1.3: Verification And ABI Source Hardening

Scope:

- ABI/register/descriptor definitions come from one JSON schema.
- RTL package, public C ABI headers, and interface documentation are generated
  and byte-checked from that schema.
- Protocol-first assertions cover valid-ready, AXI-Lite, AXI4, DMA, command,
  GEMM, and top-level invariants.
- Coverage builds collect Verilator structural coverage and enforce named
  functional coverpoints.
- GEMM and top-level tests include deterministic constrained-random tile shapes.

Non-goals:

- Public ABI value changes, register offset changes, descriptor layout changes,
  or RTL feature semantics changes.
- Structural coverage percentage thresholds.
- Vector post-processing, BF16, FP8, multiple queues, or multi-context support.

### v2: Programmable NPU Tile

Scope:

- ABI 3.0 program descriptor model.
- Replace the v1 hardcoded command/scheduler FSM with a programmable scalar
  frontend that runs microprograms.
- Define a pluggable frontend implementation boundary so the control engine can
  be replaced without changing the Holon ISA or vector/matrix engines.
- Define a Holon-owned vector/matrix ISA that is not RVV binary compatible and
  does not reserve RVC encoding holes.
- Treat the complete V2 program binary ISA as Holon-owned; frontend
  implementations are replaceable microarchitectures, not ABI owners.
- Add integer/quant vector/helper operations for masks, elementwise math,
  shifts, clip/saturate, requantization, reductions, transpose, and tile moves.
- Re-issue the v1 B-weight-stationary INT8 matrix engine through frontend
  micro-ops instead of descriptor-specific RTL scheduling.
- Use explicit scratchpad/local memory plus AXI4 DMA for all data movement.

Non-goals:

- BF16, FP8, cache coherence, IOMMU integration, multiple contexts, multiple
  descriptor queues, graph scheduling, or multi-NPU tile scaling.
- RVV binary compatibility or compressed RISC-V instruction compatibility for
  the NPU vector/matrix instruction stream.

### v3: Transformer Extensions

Scope:

- BF16 matrix datapath.
- Batched GEMM.
- Fused QKV projection.
- Attention score GEMM assistance.

### v4: Modern Low Precision Support

Scope:

- FP8 E4M3 and E5M2.
- Per-channel and per-block scale metadata.
- MX/block scaling metadata path.

### v5: System-Level Scaling

Scope:

- Multiple outstanding DMA transactions.
- Multiple descriptor queues.
- Multiple contexts.
- IOMMU/address translation interface.
- Multi-NPU tile expansion.

## v2 Phase Plan

### Phase V2.0: Architecture And Decision Freeze

Goal: freeze the programmable NPU tile direction before ABI 3.0 or RTL work.

Allowed edit scope:

- Roadmap, architecture, interface planning, verification planning, and
  decisions.
- New V2 planning documents such as ISA and program-descriptor drafts.
- No V1.5 ABI schema, generated files, RTL, or driver changes.

Deliverables:

- V2 architecture plan.
- Holon-owned NPU ISA draft.
- ABI 3.0 program descriptor draft.
- Frontend lifecycle and fault model draft.
- Local memory, DMA ordering, and synchronization draft.
- Verification strategy for frontend, ISA, vector, DMA, and matrix micro-op
  integration.
- Decision records for programmable frontend, custom ISA ownership, and explicit
  scratchpad/DMA memory model.

Acceptance criteria:

- `docs/ROADMAP.md` identifies V2 as a programmable NPU tile, not a post engine.
- `docs/ARCHITECTURE.md` links the V2 architecture plan without claiming current
  RTL implements it.
- `docs/V2_ISA.md` states that RVC/RVV encoding constraints are rejected to give
  first-class vector and matrix instructions clean opcode space.
- `docs/V2_INTERFACE.md` describes the planned ABI 3.0 program descriptor and
  lifecycle registers at a draft level.
- V2 drafts define program compatibility fields, local-memory ordering,
  frontend lifecycle transitions, and matrix micro-op architectural semantics.
- `docs/DECISIONS.md` records the V2 architectural commitments.

Dependencies:

- v1.5 release baseline complete.

Primary risks:

- Letting RISC-V scalar compatibility constrain the NPU vector/matrix ISA.
- Accidentally modifying V1.5 generated ABI artifacts before ABI 3.0 is
  formally implemented.

### Phase V2.1: ISA And ABI Schema

Goal: convert the V2 architecture plan into schema-owned ABI 3.0 artifacts and
machine-checkable ISA metadata.

Allowed edit scope:

- `spec/holon_npu_abi.json` and generator support for ABI 3.0.
- ISA metadata/checker files. The first ISA metadata source is
  `spec/holon_npu_isa.json`.
- Generated RTL/C/docs outputs.
- Focused ABI and decoder tests.

Deliverables:

- Program descriptor fields in the ABI schema.
- Frontend lifecycle, capability, fault, and debug registers.
- Holon ISA encoding table with instruction classes for frontend control,
  predicate, vector, matrix, DMA, CSR, and synchronization.
- Program compatibility fields for Holon ISA version, program format, required
  capabilities, and required operation classes.
- Memory ordering and lifecycle fields required by the V2 interface draft.
- Program-image local load and argument scratchpad-copy semantics before
  frontend execution.
- Generated ABI documentation and public C23 headers for ABI 3.0.
- Static checker that verifies ISA encoding table uniqueness, reserved-space
  policy, metadata completeness, coverage labels, fault model, and semantic
  hook names.
- C++ architectural simulator skeleton for decoder, local-memory, DMA-ordering,
  vector-state, and matrix micro-op differential tests.

Acceptance criteria:

- `tools/gen_abi.py --check` passes with ABI 3.0 outputs.
- ISA encoding checks reject duplicate or overlapping instruction classes.
- Public headers expose program descriptor and capability constants without
  project-owned C macros.
- Program descriptor compatibility failures are testable before frontend
  execution starts.
- Frontend execution starts only after code and argument visibility rules are
  satisfied.

Dependencies:

- Phase V2.0 complete.

Primary risks:

- Over-specifying instruction encodings before frontend and vector datapath
  requirements are validated.
- Mixing V1 GEMM descriptor compatibility into the new program descriptor model.

### Phase V2.2: Frontend Boundary

Goal: introduce the replaceable frontend implementation boundary and a minimal
test frontend.

Deliverables:

- `npu_frontend_if` with program memory, local memory, CSR/fault, and engine
  issue channels.
- Reference test frontend capable of booting, issuing simple engine commands,
  and reporting completion/fault.
- Frontend lifecycle control through ABI 3.0 registers.
- Precise halt/resume/reset/fault priority semantics.

Acceptance criteria:

- Program descriptor launches the test frontend.
- Boot, halt, fault, IRQ, and debug snapshot paths are tested.
- Product RTL remains interface-native; test-only wrappers remain under
  `sim/rtl/`.

### Phase V2.3: Scratchpad And DMA Command Fabric

Goal: make local memory and DMA programmable resources under frontend control.

Deliverables:

- Program/data scratchpad arbitration.
- Frontend-issued DMA command queues.
- AXI4 DMA integration with existing protocol assertions.
- SPM bounds and DMA fault reporting.

Acceptance criteria:

- Deterministic DMA programs move data between system memory and scratchpad.
- Invalid SPM ranges and DMA response errors report documented frontend faults.
- Functional coverage includes DMA command success, backpressure, and fault
  paths.

### Phase V2.4: Integer/Quant Vector And Helper Engine

Goal: implement the first V2 vector/helper compute engine.

Deliverables:

- Vector register file and predicate/mask state.
- VLA-style vector length configuration.
- Integer elementwise, compare, select, shift, clip/saturate, requant,
  reduction, transpose, and tile-move operations.
- Frontend-control CSR, address generation, synchronization, and branch/control
  support.
- C++ golden model and deterministic constrained-random vector tests.

Acceptance criteria:

- Vector tests cover i8/u8/i16/u16/i32/u32, masks, tails, reductions, and
  requant edge cases.
- Program-level tests run vector kernels from a program descriptor.
- Coverage gates include all required vector instruction classes.

### Phase V2.5: Matrix Engine Re-Issue

Goal: reuse the V1 matrix engine as a frontend-issued matrix resource.

Deliverables:

- Matrix micro-op issue interface.
- Firmware-controlled tile traversal for INT8 GEMM.
- Refactored matrix scheduler boundary so hardcoded descriptor-specific
  scheduling is no longer the control model.
- Matrix micro-op operands for local A/B/C addresses, active M/N/K shape, edge
  masks, accumulator control, completion event, and fault result.

Acceptance criteria:

- GEMM launched through a V2 microprogram matches the V1 C++ golden model.
- Matrix tests cover fixed and randomized tile shapes through the new issue
  path.
- The V1 systolic array dataflow remains B-weight-stationary.

### Phase V2.6: Firmware, Driver, And Release Hardening

Goal: provide a minimal V2 software stack and release-quality verification.

Deliverables:

- Program image layout and kernel ABI.
- C23 submit/wait/status/error/perf driver APIs for program descriptors.
- Example GEMM, activation, requant, reduce, and transpose kernels.
- ISA decoder, frontend exception, SPM bounds, DMA fault, vector, matrix, and
  integration coverage gates.

Acceptance criteria:

- Debug, lint, regression, and coverage presets pass.
- Functional coverage includes frontend lifecycle, ISA decode, vector classes,
  DMA faults, matrix issue, IRQ, and fault paths.
- Program-level differential tests compare RTL-visible execution against the
  C++ architectural simulator.
- V2 known limits are documented before release.

## v1 Phase Plan

### Phase 0: Governance Bootstrap

Goal: establish roadmap-first engineering discipline before implementation.

Deliverables:

- `docs/ROADMAP.md`
- `docs/PROGRESS.md`
- `docs/DECISIONS.md`
- Initial `docs/ARCHITECTURE.md`
- Initial `docs/INTERFACE.md`
- Initial `docs/VERIFICATION.md`

Acceptance criteria:

- The roadmap defines v1 through v5.
- Every v1 phase has deliverables and verification criteria.
- `docs/PROGRESS.md` records Phase 0 completion.

Dependencies:

- None.

Primary risks:

- Later phases ignoring the documented gate process.
- Interface changes being made in RTL before being documented.

### Phase 1: Project Skeleton

Goal: create the modern CMake, Verilator, and C++26 project skeleton.

Allowed edit scope:

- Build files.
- Top-level source directories.
- Minimal smoke RTL and C++ testbench only.
- Documentation updates for actual progress.

Deliverables:

- Root `CMakeLists.txt`.
- `CMakePresets.json`.
- `rtl/`.
- `sim/`.
- `sw/`.
- `include/`.
- `tests/`.
- Basic Verilator-backed simulation target.

Acceptance criteria:

- `cmake --preset debug` configures the project.
- `cmake --build --preset debug` builds the empty/smoke simulation target.
- `ctest --preset debug` runs and passes a basic smoke test.

Dependencies:

- Phase 0 complete.
- Local toolchain includes CMake 4.0+, Verilator, and a compiler with C++26
  support.

Primary risks:

- Host toolchain lacks CMake 4.0 or a C++26-capable compiler.
- Verilator package integration differs across environments.

### Phase 2: Architecture And ABI Freeze For v1

Goal: freeze v1 external interfaces before depending on them in RTL/software.

Allowed edit scope:

- Documentation for architecture, interface, verification, and decisions.
- Shared ABI headers or generated metadata only if needed to lock field values.

Deliverables:

- AXI-Lite register map.
- GEMM descriptor layout.
- Status codes, error codes, and interrupt semantics.
- Capability register definitions.
- Initial software API contract.

Acceptance criteria:

- `docs/INTERFACE.md` defines offsets, field widths, reset values, access types,
  and side effects.
- `docs/DECISIONS.md` records descriptor queue semantics, matrix dataflow, and
  INT8/INT32 arithmetic choices.
- Later RTL must match this ABI.

Dependencies:

- Phase 1 complete.

Primary risks:

- ABI fields may be underspecified and force later churn.
- Descriptor alignment and endianness constraints may be missed.

### Phase 3: Common RTL Infrastructure

Goal: create reusable SystemVerilog building blocks.

Allowed edit scope:

- Common packages, interfaces, and utility modules.
- Minimal tests for those common modules.
- Verification documentation updates.

Deliverables:

- `npu_pkg.sv`.
- AXI-Lite interface.
- AXI4 interface.
- Valid-ready stream interface.
- Common FIFO, skid buffer, and register slice.
- Reset and clocking convention.

Acceptance criteria:

- Verilator lint passes for common RTL.
- C++ smoke test instantiates the minimal RTL top.
- All public common modules have focused tests.

Dependencies:

- Phase 2 ABI freeze.

Primary risks:

- Interfaces becoming too narrow for later DMA or command processor needs.
- Reset behavior diverging between modules.

### Phase 4: PE And Systolic Array

Goal: implement the minimal matrix engine core.

Allowed edit scope:

- PE RTL.
- Systolic array RTL.
- Golden-model backed matrix engine tests.
- Verification documentation updates.

Deliverables:

- Signed INT8 processing element.
- Parameterized `ARRAY_K x ARRAY_N` systolic array.
- Signed INT32 accumulator.
- Valid/mask propagation.
- Non-full tile boundary handling.

Acceptance criteria:

- PE unit tests cover positive operands, negative operands, zero, and overflow
  boundary behavior.
- Array tests cover `1x1`, `16x16`, and `17x19x23`.
- C++ golden model matches every output exactly.

Dependencies:

- Phase 3 common RTL infrastructure.

Primary risks:

- Sign extension mistakes.
- Off-by-one valid propagation through the array.
- Incomplete tile masks corrupting edge outputs.

### Phase 5: Scratchpad And Tiling Datapath

Goal: establish on-chip buffering and tile data movement.

Allowed edit scope:

- Tile buffers and scratchpad abstraction.
- Tiling controller.
- Tests for tile address and mask behavior.

Deliverables:

- A tile buffer.
- B tile buffer.
- C accumulator/output buffer.
- Banked scratchpad abstraction.
- Ping-pong buffer control.
- Tile mask generation.

Acceptance criteria:

- No bank address overflow in simulation.
- Tile load, compute, and store scheduling is observable in traces/tests.
- Non-multiples of 16 in M, N, and K compute correctly.

Dependencies:

- Phase 4 matrix engine.

Primary risks:

- Bank conflicts or address aliasing.
- Ping-pong state transitions causing stale data reuse.

### Phase 6: AXI-Lite Control Plane

Goal: allow a CPU to control the NPU through registers.

Allowed edit scope:

- AXI-Lite slave.
- Register file.
- Control/status tests and driver helpers.

Deliverables:

- AXI-Lite slave.
- Register file.
- Doorbell/start register behavior.
- Status, done, and error handling.
- IRQ enable, status, and clear behavior.
- Initial performance counters.

Acceptance criteria:

- C++ AXI-Lite driver can read and write every documented register.
- Reset values match `docs/INTERFACE.md`.
- Busy, done, and error state transitions are correct.
- Illegal writes return a documented error response or are explicitly ignored as
  documented.

Dependencies:

- Phase 3 common RTL infrastructure.
- Phase 2 ABI freeze.

Primary risks:

- Register side effects are ambiguous.
- Interrupt status clear semantics are inconsistent.

### Phase 7: AXI4 DMA And Memory Model

Goal: implement NPU-initiated system memory access.

Allowed edit scope:

- AXI4 read DMA.
- AXI4 write DMA.
- C++ simulated memory model.
- DMA tests.

Deliverables:

- AXI4 read DMA.
- AXI4 write DMA.
- Burst generation.
- Response/error handling.
- C++ simulated memory model.

Acceptance criteria:

- Single-burst, multi-burst, and cross-tile access cases pass.
- AXI errors transition the design into error status.
- Any v1 unaligned-access limitation is documented and tested.

Dependencies:

- Phase 3 common RTL infrastructure.
- Phase 2 ABI freeze.

Primary risks:

- Burst length, boundary, or alignment mistakes.
- Error response paths not covered by happy-path tests.

### Phase 8: Command Processor

Goal: move execution from direct register start to descriptor-driven operation.

Allowed edit scope:

- Descriptor fetch/decode.
- Command issue logic.
- Descriptor validation tests.

Deliverables:

- Descriptor fetch.
- Descriptor decode.
- GEMM command issue.
- Single descriptor queue semantics.
- Descriptor version, opcode, flag, and size checks.

Acceptance criteria:

- CPU can start GEMM by writing descriptor base and doorbell only.
- Illegal opcode, version, or size transitions to documented error state.
- `docs/INTERFACE.md` and RTL field definitions match exactly.

Dependencies:

- Phase 6 AXI-Lite control plane.
- Phase 7 AXI4 DMA.

Primary risks:

- ABI drift between docs, software, tests, and RTL.
- Descriptor fetch races with CPU writes.

### Phase 9: Integrated GEMM Accelerator

Goal: connect the complete v1 datapath.

Allowed edit scope:

- Integration top.
- Load/compute/store scheduler.
- End-to-end GEMM tests.
- Performance counter integration.

Deliverables:

- AXI4 load for A and B.
- Scratchpad tile buffering.
- Systolic compute path.
- C writeback.
- Completion IRQ.
- Performance counter updates.

Acceptance criteria:

- `1x1`, `16x16`, `17x19x23`, and `64x64` GEMM tests pass.
- Signed INT8 randomized tests pass across multiple seeds.
- C++ INT32 golden model matches every output exactly.
- Wave dumps allow each major stage to be located.

Dependencies:

- Phases 4 through 8 complete.

Primary risks:

- Integration deadlocks.
- Incorrect tile ordering or C writeback addressing.
- Performance counters becoming unreliable under errors or reset.

### Phase 10: Software Driver

Goal: provide a minimal CPU-side software interface.

Allowed edit scope:

- C headers and driver source under `sw/` and shared ABI includes.
- Host-side driver tests.

Deliverables:

- `holon_npu_regs.h`.
- `holon_npu_desc.h`.
- `holon_npu_driver.c`.
- `holon_npu_driver.h`.
- Host-side driver test.

Acceptance criteria:

- Driver builds.
- Descriptor layout is shared with RTL/C++ tests or statically checked against a
  common ABI definition.
- Submit, wait, status, error, and performance counter flows are complete.

Dependencies:

- Phase 2 ABI freeze.
- Phase 8 command processor.
- Phase 9 integrated accelerator for end-to-end validation.

Primary risks:

- C struct packing mismatch.
- Driver polling and interrupt paths diverge.

### Phase 11: Verification Hardening

Goal: increase confidence beyond happy-path operation.

Allowed edit scope:

- Regression tests.
- Error/fuzz/reset tests.
- Coverage and lint targets.
- Documentation of limitations and release checklist.

Deliverables:

- Randomized GEMM tests.
- Reset-in-flight tests.
- AXI error injection.
- Descriptor fuzz tests.
- Lint target and regression preset.
- Regression preset.

Acceptance criteria:

- `ctest --preset debug` passes.
- Lint has no critical warnings.
- Known limitations are documented in `docs/PROGRESS.md`.
- v1 release checklist passes.

Dependencies:

- Phase 9 integrated accelerator.
- Phase 10 software driver.

Primary risks:

- Random tests are non-reproducible without seed logging.
- Functional coverage may miss error state transitions if coverpoints are not
  made explicit.

## v1.1 Phase Plan

### Phase 12: B-Weight-Stationary Matrix Engine

Goal: replace the v1 matrix dataflow with a B-weight-stationary engine and
publish ABI 2.0.

Allowed edit scope:

- Architecture, interface, verification, progress, and decision docs.
- Shared ABI constants and driver/test ABI expectations.
- PE, systolic-array, scratchpad wavefront, GEMM scheduler, and related tests.

Deliverables:

- ABI 2.0 constants with descriptor `version=2`.
- Public `ARRAY_K`/`ARRAY_N` capability naming.
- PE-local B weight registers.
- K-by-N systolic array with A wavefront input and vertical psum flow.
- C tile accumulator buffer used by GEMM writeback.
- Updated PE, array, tiling, GEMM, top, driver, lint, and regression tests.

Acceptance criteria:

- ABI checker passes with ABI 2.0 and `ARRAY_K` constants.
- PE and array tests prove B-weight-stationary weight load, psum flow, masking,
  signed arithmetic, and INT32 wraparound.
- Integrated GEMM and public top tests match the C++ INT32 golden model exactly.
- `ctest --preset debug`, `ctest --preset lint`, and regression presets pass.
- Current documentation contains no stale dataflow or old M-row capability
  contract.

Dependencies:

- v1 Phase 11 complete.

Primary risks:

- Off-by-one wavefront timing in A injection or psum output collection.
- Tail K rows retaining stale stationary weights.
- C accumulator update races at K tile boundaries.

### Phase 13: Interface-Native Core Boundary

Goal: make SystemVerilog interfaces the canonical internal RTL connection
contract and keep flattened wrappers limited to C++/Verilator test harnesses.

Allowed edit scope:

- Common valid-ready primitives.
- AXI-Lite control core.
- AXI4 read/write DMA cores.
- Command processor, GEMM accelerator, and product top integration.
- CMake source-set hygiene, architecture/verification docs, and guardrail
  tooling.

Deliverables:

- `npu_fifo`, `npu_skid_buffer`, and `npu_register_slice` use `npu_vr_if`.
- DMA, command, GEMM, control, and top product cores use
  `npu_axi4_if`, `npu_axi_lite_if`, and `npu_vr_if` modports.
- Flattened `*_test_wrapper.sv` files exist only for C++/Verilator test access
  and are excluded from product source sets.
- `npu_top.sv` remains the product pin boundary and internally instantiates
  interface-native `npu_top_core`.
- `tools/check_rtl_interface_usage.py` is part of CTest and regression.

Acceptance criteria:

- `python3 tools/check_rtl_interface_usage.py` passes.
- `ctest --preset debug` includes and passes `rtl_interface_usage`.
- Common, DMA, control, command, GEMM, and top tests still pass with unchanged
  C++ flattened access.
- Product/core RTL does not instantiate `*_test_wrapper` modules.
- No public C ABI, descriptor ABI, register map, or error semantics change.

Dependencies:

- Phase 12 B-weight-stationary v1.1 implementation.

Primary risks:

- Verilator C++ harnesses still require stable flattened top-level pins.
- Interface modport direction mistakes can produce subtle integration failures.
- Product source sets can accidentally absorb test wrappers if CMake hygiene is
  not checked automatically.

### Phase 14: Build/Test Preset Separation

Goal: make build presets compile only, make CTest presets own test execution,
and keep the build/test command surface small.

Allowed edit scope:

- CMake presets, minimal CTest labels, CI workflow, and build/verification
  documentation.

Deliverables:

- Separate Debug and RelWithDebInfo regression configure presets.
- Ninja generator pinned in presets.
- Build presets only for `debug` and `regression` at this phase; v1.3 later
  adds the separate `coverage` build tree because Verilator coverage
  instrumentation cannot share the normal debug/regression trees.
- Test presets only for `debug`, `lint`, and `regression` at this phase; v1.3
  later adds `coverage` for the same instrumentation reason.
- Minimal CTest labels: `fast`, `lint`, `slow`, and `static`.
- Focused builds use CMake's native `--target`; focused test runs use CTest's
  native `-R` and `--verbose`.
- No build target runs CTest directly.
- No build target name includes an architecture version.

Acceptance criteria:

- `ctest --preset debug -N` lists only the fast local subset.
- `ctest --preset lint -N` lists only lint tests.
- `ctest --preset regression -N` lists the full matrix.
- `cmake --build --preset regression --parallel 2` builds optimized targets
  without running CTest.
- `cmake --build --preset debug --target lint --parallel 2` builds the
  aggregate lint target.
- `cmake --build --preset debug --target npu_top_tb` builds one selected test.
- `ctest --preset regression -R npu_top --verbose` runs one selected test with
  normal CTest output.

Dependencies:

- Phase 13 interface-native core boundary.

Primary risks:

- Too many presets can become more confusing than the target/test names they
  wrap.
- Test parallelism is explicit at call sites with `ctest -j`, not hidden in
  presets.

### Phase 15: Test-Only RTL Harness Separation

Goal: keep product RTL directories free of Verilator/C++ harness files and make
simulation-only SystemVerilog ownership explicit.

Allowed edit scope:

- File layout, CMake source sets, RTL interface-usage guardrails, and
  architecture/verification documentation.

Deliverables:

- `rtl/` contains only product/core RTL and common protocol definitions.
- `sim/rtl/<subsystem>/` contains all `*_test_wrapper.sv`, `*_test_top.sv`, and
  smoke top harnesses.
- CMake product source sets reference only product RTL.
- Verilator test/lint source sets explicitly reference `sim/rtl/` harnesses.
- `tools/check_rtl_interface_usage.py` enforces the ownership boundary.

Acceptance criteria:

- `find rtl -name '*test*' -o -name '*smoke_top.sv'` finds no test harnesses.
- `find sim/rtl -type f | sort` lists all simulation-only SV harnesses.
- `python3 tools/check_rtl_interface_usage.py` passes.
- Debug, lint, and regression CTest presets pass with unchanged test names.
- No public C ABI, descriptor ABI, register map, RTL behavior, CI entry, or
  preset changes.

Dependencies:

- Phase 13 interface-native core boundary.
- Phase 14 minimal build/test preset system.

Primary risks:

- Accidentally moving product RTL into `sim/rtl/`.
- Accidentally allowing product CMake source sets to reference harness files.

## v1.3 Phase Plan

### Phase 16: Verification And ABI Single Source Hardening

Goal: remove manually synchronized ABI definitions and add internal
self-checking verification mechanisms that catch protocol and coverage gaps
early.

Allowed edit scope:

- ABI schema, generator, generated ABI outputs, and ABI check tooling.
- Native SVA assertions/coverpoints and local protocol/design invariants.
- Coverage preset, typed C++ test runtime, and coverage checker.
- Deterministic constrained-random GEMM/top test generation.
- CI, roadmap, architecture, verification, decision, progress, and getting
  started documentation.

Deliverables:

- `spec/holon_npu_abi.json` as the sole ABI/register/descriptor schema.
- `tools/gen_abi.py` generating `npu_pkg.sv`, public C headers, and
  `docs/INTERFACE.md`.
- `tools/gen_abi.py --check` registered directly as the ABI generation CTest.
- Native SVA assertions and coverpoints in protocol interfaces and key
  control/DMA/command/GEMM/top modules.
- Expected-fail assertion smoke test proving assertions are active.
- `coverage` configure/build/test preset, typed C++ `test_run` runtime,
  `coverage_point` registry, and `tools/check_coverage.py`.
- 64 GEMM accelerator constrained-random tile cases and 16 product-top
  constrained-random tile cases.

Acceptance criteria:

- `python3 -m json.tool spec/holon_npu_abi.json` passes.
- `python3 tools/gen_abi.py --check` passes.
- `python3 tools/check_rtl_interface_usage.py` passes.
- Debug, lint, regression, and coverage CTest presets pass.
- `python3 tools/check_coverage.py --build-dir build/coverage` reports all
  required functional coverage points hit.
- No public ABI value, register offset, descriptor layout, or RTL feature
  semantic changes are introduced.

Dependencies:

- Phase 15 test-only RTL harness separation.

Primary risks:

- Generated outputs can drift if schema changes are not checked in CI.
- Assertions can reveal latent testbench protocol violations as well as design
  bugs.
- Coverage artifacts require a separate build tree and can slow CI if expanded
  without discipline.

## Current Phase

The current baseline is v1.5 final V1-generation complete. New feature work
must add a new roadmap phase and decision entry before implementation.
