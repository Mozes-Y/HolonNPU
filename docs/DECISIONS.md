# Architecture Decision Records

This log records decisions that affect architecture, ABI, verification strategy,
or long-term maintainability. Each decision must include context, decision,
rationale, alternatives, and impact.

## ADR-0001: Roadmap-First Development

Status: Accepted.

Date: 2026-06-26.

Context:

- The project starts from an empty repository and targets a multi-phase NPU
  implementation.
- Interface churn is expensive once RTL, software, and tests depend on shared
  ABI details.

Decision:

- `docs/ROADMAP.md` is the single source of truth for phase order, scope,
  deliverables, and acceptance criteria.
- `docs/PROGRESS.md` records actual completion, verification commands, results,
  remaining issues, and next steps after each phase.

Rationale:

- A roadmap-first workflow prevents accidental feature creep and makes phase
  gates explicit.
- Progress logging keeps verification results tied to the code state that
  produced them.

Alternatives:

- Build RTL first and document later. Rejected because ABI and architecture
  drift would be likely.
- Track progress only in commit history. Rejected because this workspace may not
  always be a Git repository and the roadmap needs local visibility.

Impact:

- Every implementation phase must start by reading the roadmap and progress log.
- Phase completion is not valid until documentation and verification records are
  updated.

## ADR-0002: v1 Scope Is INT8 GEMM Only

Status: Accepted.

Date: 2026-06-26.

Context:

- The long-term roadmap includes vector post-processing, BF16, FP8, transformer
  helpers, and system-level scaling.
- The first implementation needs a bounded target that can be verified with
  exact golden-model comparison.

Decision:

- v1 implements only signed INT8 GEMM with signed INT32 accumulation/output.
- v1 does not implement vector operations, BF16, FP8, graph scheduling,
  convolution, full softmax, LayerNorm, or GELU.

Rationale:

- INT8 GEMM gives a useful accelerator target with deterministic, exact
  checking.
- Keeping v1 narrow reduces ABI churn and verification risk.

Alternatives:

- Add vector or transformer helpers in v1. Rejected as premature because the
  GEMM datapath and control path must be proven first.
- Start with floating point. Rejected because INT8/INT32 is simpler to validate
  and aligns with the minimal systolic-array target.

Impact:

- Future features must enter through roadmap and decision-log updates before
  implementation.
- v1 verification can rely on integer golden models with exact equality.

## ADR-0003: Target Toolchain

Status: Accepted.

Date: 2026-06-26.

Context:

- The project needs reproducible RTL simulation and host-side tests.

Decision:

- Use SystemVerilog for RTL, Verilator for simulation, C++26 for testbenches,
  and CMake 4.0 or newer for the build.

Rationale:

- Verilator and C++ testbenches provide fast simulation and deterministic CI
  behavior.
- Modern CMake presets make configure/build/test flows explicit.

Alternatives:

- Use ad hoc Makefiles. Rejected because presets and CTest integration are
  required by the roadmap.
- Use older C++ standards. Rejected because the requested stack fixes C++26.

Impact:

- Phase 1 must validate the local toolchain.
- If the local environment lacks CMake 4.0+, Verilator, or a C++26 compiler,
  that limitation must be recorded in `docs/PROGRESS.md`.

## ADR-0004: v1 Uses A Single Descriptor Queue

Status: Accepted.

Date: 2026-06-26.

Context:

- The v1 control plane must be simple enough to verify before introducing
  multiple queues, contexts, or outstanding commands.
- The CPU can construct descriptors in memory and notify hardware with a
  doorbell register.

Decision:

- v1 supports one descriptor in flight.
- Software writes `DESC_ADDR_LO`, `DESC_ADDR_HI`, then `DOORBELL.START=1`.
- Hardware fetches exactly one 128-byte descriptor.
- Hardware rejects doorbell writes while busy.
- Software must not modify a submitted descriptor until hardware reaches done or
  error.

Rationale:

- Single-descriptor semantics keep ordering, ownership, and completion behavior
  deterministic.
- Descriptor fetch over AXI4 validates the eventual DMA path without introducing
  queue management complexity.

Alternatives:

- Direct register programming of GEMM dimensions and addresses. Rejected because
  the v1 product goal requires descriptor-driven execution.
- Multiple descriptor queue entries. Rejected for v1 because it requires
  head/tail ownership rules, ordering guarantees, and more verification.

Impact:

- Phase 8 implements only one descriptor fetch/decode at a time.
- Phase 10 driver submit semantics are simple polling or interrupt-based wait
  for one active command.
- v5 may add multiple queues or contexts through a new ABI version or a backward
  compatible extension.

## ADR-0005: v1 Matrix Dataflow And Arithmetic

Status: Accepted.

Date: 2026-06-26.

Context:

- The matrix engine needs deterministic behavior for exact C++ golden-model
  comparison.
- The roadmap target is a parameterized `16x16` systolic array.

Decision:

- v1 uses output-stationary dataflow.
- A and B operands are signed INT8.
- Partial sums and output C elements are signed INT32.
- Accumulation uses two's-complement INT32 wraparound semantics.
- v1 performs plain GEMM only: no bias, scaling, activation, transposition,
  saturation, or accumulation with an existing C matrix.

Rationale:

- Output-stationary dataflow is a direct match for a systolic array that keeps C
  partial sums local to processing elements.
- Signed INT8 and signed INT32 allow exact hardware/software comparison.
- Excluding post-processing keeps v1 focused and reserves that work for v2.

Alternatives:

- Weight-stationary or row-stationary dataflow. Rejected for v1 because they
  would complicate the initial scratchpad and scheduler design.
- Saturating arithmetic. Rejected because INT32 wraparound is easier to match to
  hardware and C++ fixed-width integer behavior when implemented explicitly.
- Accumulate onto existing C memory. Rejected because it requires extra reads and
  changes DMA scheduling.

Impact:

- Phase 4 PE and array tests must check signed behavior and INT32 boundaries.
- Phase 5 scratchpad and Phase 9 integration must provide edge masks for partial
  tiles.
- v2 can add post-processing after v1 GEMM output.

## ADR-0006: v1 ABI Register And Descriptor Conventions

Status: Accepted.

Date: 2026-06-26.

Context:

- RTL, software, and tests need stable offsets, reset values, side effects, and
  descriptor fields before implementation starts.

Decision:

- The v1 ABI is defined by `docs/INTERFACE.md`.
- AXI-Lite registers are 32-bit and live in a 4 KiB aperture.
- System addresses are 64-bit.
- AXI4 data width is 128 bits.
- The GEMM descriptor is 128 bytes and 16-byte aligned.
- Tensor base addresses and row strides are 16-byte aligned.
- Non-16 dimensions are supported with padded strides and tile masks.
- The external interrupt is level-sensitive and based on enabled sticky status
  bits.

Rationale:

- 32-bit AXI-Lite registers are broadly compatible with RISC-V firmware.
- 64-bit addresses avoid early address-space limits.
- 128-bit DMA beats align with 16 INT8 operands or four INT32 outputs.
- A 128-byte descriptor leaves reserved space for backward-compatible metadata.
- Alignment constraints simplify v1 DMA while preserving arbitrary matrix
  dimensions through stride padding.

Alternatives:

- 32-bit system addresses. Rejected because it would constrain integration.
- Unaligned tensor accesses. Rejected for v1 to reduce DMA complexity.
- Variable-size descriptors. Rejected because fixed-size fetch/decode is easier
  to verify.

Impact:

- Phase 3 constants, Phase 6 registers, Phase 7 DMA, Phase 8 descriptor decode,
  Phase 9 integration, and Phase 10 software must match the frozen ABI.
- Any future incompatible change must either increment the ABI major version or
  introduce a backward-compatible capability flag.

## ADR-0007: Common Clock, Reset, And Valid-Ready Convention

Status: Accepted.

Date: 2026-06-26.

Context:

- Common RTL infrastructure is the foundation for later control, DMA, command,
  scratchpad, and matrix modules.
- Inconsistent reset and handshake conventions would create integration bugs
  that are difficult to diagnose.

Decision:

- v1 uses a single clock domain.
- Internal non-AXI RTL uses `clk_i` and active-low asynchronous reset `rst_ni`.
- AXI-facing interfaces use `aclk_i` and active-low `aresetn_i`.
- Common stream modules use valid-ready handshake semantics.
- A transfer occurs on a rising clock edge when `valid && ready` is true.
- Sources hold `valid` and `data` stable while stalled.
- Reset clears internally stored valid bits and resets externally observable
  storage to zero where applicable.

Rationale:

- Active-low reset naming matches the external AXI reset convention while
  keeping internal module ports consistent.
- Valid-ready flow control is simple enough for focused unit tests and scales to
  DMA, command, and datapath blocks.
- A single clock domain avoids CDC concerns in v1.

Alternatives:

- Active-high internal reset. Rejected to avoid reset polarity conversion at AXI
  boundaries.
- Multiple clock domains in v1. Rejected because CDC verification is outside the
  initial GEMM accelerator scope.
- Ad hoc enable/data protocols. Rejected because common valid-ready utilities
  are required by the roadmap.

Impact:

- Phase 3 common modules and later v1 RTL must use these names and semantics.
- Any future additional clock domain requires a new decision record and CDC
  verification plan.

## ADR-0008: Phase 5 Scratchpad And Ping-Pong Tile Flow

Status: Accepted.

Date: 2026-06-26.

Context:

- The matrix engine needs local A, B, and C tile storage before DMA and command
  integration exist.
- Partial edge tiles must be represented consistently before full tiling and
  writeback are implemented.

Decision:

- Phase 5 introduces a reusable banked scratchpad primitive with explicit
  out-of-range error flags.
- A and B tile buffers use 8-bit entries.
- C accumulation/output buffers use 32-bit entries.
- The initial test geometry uses two ping-pong banks and 256 entries per bank.
- Tile masks are generated independently for M, N, and K remaining counts.
- The initial scheduler exposes load, compute, store, and done states and
  toggles the ping-pong bank after store completion.

Rationale:

- Explicit range flags make bank/address overflow testable before DMA exists.
- Separate A/B and C wrappers keep data width differences visible.
- A two-bank ping-pong model is enough to validate stale-data avoidance and
  future overlap without implementing multiple outstanding DMA in v1.
- Independent M/N/K masks cover non-16 edge tiles cleanly.

Alternatives:

- A monolithic scratchpad with implicit widths. Rejected because A/B and C have
  different element widths and need focused tests.
- Single-buffer scheduling. Rejected because it would not exercise ping-pong
  ownership transitions.
- Fully integrated DMA-driven tiling in Phase 5. Rejected because AXI4 DMA is
  scheduled for Phase 7.

Impact:

- Phase 7 DMA and Phase 9 integration should reuse or preserve the bank/error
  and mask behavior tested in Phase 5.
- Future capacity changes must keep external module semantics compatible or
  update tests and decisions first.

## ADR-0009: Phase 6 AXI-Lite Control Plane Semantics

Status: Superseded by ADR-0015 for final v1 AXI-Lite channel timing.

Date: 2026-06-26.

Context:

- The external register ABI was frozen in Phase 2.
- The command processor is not implemented until Phase 8, but the control plane
  still needs testable busy, done, error, IRQ, and performance counter behavior.

Decision:

- Phase 6 implements the full documented AXI-Lite register map in
  `npu_control_regs.sv`.
- The original Phase 6 slave accepted writes only when AW and W were presented
  together. ADR-0015 supersedes this for final v1: AW and W must be accepted
  independently and joined internally before write side effects occur.
- Read-only writes, unmapped accesses, invalid reserved-bit writes, unsupported
  partial pulse-control writes, and doorbells while busy return `SLVERR`.
- Doorbell acceptance emits `command_start_o` and sets busy.
- Backend test inputs represent later command/datapath done and error events.
- `STATUS.IDLE` means the control plane is not busy, including sticky done/error
  terminal states.

Rationale:

- A small, deterministic slave is enough to validate the ABI before command and
  DMA integration.
- Explicit backend test inputs let Phase 6 verify status and IRQ transitions
  without implementing Phase 8.
- Returning `SLVERR` for illegal writes exposes software bugs early.

Alternatives:

- Ignore illegal writes. Rejected because `docs/INTERFACE.md` documents clear
  error responses for these cases.
- Wait until the command processor exists to test status transitions. Rejected
  because Phase 6 acceptance requires busy/done/error behavior.
- Keep the original coupled AW/W Phase 6 behavior in final v1. Rejected by
  ADR-0015 because real AXI-Lite masters and interconnects may skew write
  address and write data channels.

Impact:

- Phase 8 command processor should consume `command_start_o` and
  `command_desc_addr_o`.
- Later integration must preserve the documented status, IRQ, and illegal-access
  behavior.

## ADR-0010: Phase 7 AXI4 DMA Alignment And Burst Policy

Status: Accepted.

Date: 2026-06-26.

Context:

- The v1 ABI fixes 128-bit AXI4 data beats, 16-byte descriptor/tensor alignment,
  and a maximum burst payload of 256 bytes.
- Full byte-lane edge handling belongs inside tile/scratchpad logic, not as
  arbitrary unaligned AXI accesses in the first DMA implementation.

Decision:

- Phase 7 DMA requests require a 16-byte aligned address and a nonzero byte
  count that is a multiple of 16.
- Read and write DMA split larger requests into `INCR` bursts of at most 16
  beats.
- Each DMA engine has one outstanding burst at a time.
- Read `SLVERR`/`DECERR` responses report `ERR_AXI_READ`.
- Write `SLVERR`/`DECERR` responses report `ERR_AXI_WRITE`.
- `EXOKAY` is treated as successful even though v1 never generates exclusive
  accesses.

Rationale:

- Whole-beat DMA keeps burst generation and memory-model verification simple.
- The 16-beat cap matches the frozen maximum 256-byte burst payload.
- Rejecting unsupported alignment before AXI traffic prevents ambiguous partial
  writes or reads.

Alternatives:

- Support arbitrary unaligned DMA in v1. Rejected because it would add byte-lane
  merge/split behavior before integration needs it.
- Allow multiple outstanding DMA bursts in v1. Rejected because v5 is the
  planned phase for multiple outstanding transactions.

Impact:

- Phase 8 descriptor fetch can use a single aligned 128-byte read.
- Phase 9 tensor DMA must provide padded, aligned row accesses or update the ABI
  through a new decision record first.

## ADR-0011: Phase 8 Descriptor Validation And Command Issue

Status: Accepted.

Date: 2026-06-26.

Context:

- Phase 2 froze the v1 descriptor ABI.
- Phase 7 provides aligned AXI4 read DMA that can fetch one 128-byte descriptor.
- Full GEMM execution is scheduled for Phase 9, so Phase 8 must stop at
  validated command issue.

Decision:

- The command processor fetches exactly one 128-byte descriptor with the read DMA.
- Descriptor decode is little-endian and follows `docs/INTERFACE.md`.
- Validation order is deterministic:
  size, version, opcode, flags, dimensions, alignment/stride, reserved fields.
- Valid descriptors assert a valid/ready GEMM command interface.
- Invalid descriptors assert `error_o` with the documented error code and do not
  issue a command.

Rationale:

- Deterministic validation order makes failure tests stable.
- A valid/ready command interface lets Phase 9 consume commands without changing
  the descriptor ABI.
- Keeping Phase 8 limited to fetch/decode avoids mixing integration behavior into
  the command processor.

Alternatives:

- Decode descriptor fields directly in the integrated GEMM scheduler. Rejected
  because descriptor ABI validation should be isolated and testable.
- Allow partially valid descriptors to issue with defaults. Rejected because
  reserved fields and unsupported encodings must fail loudly.

Impact:

- Phase 9 should consume the decoded command interface rather than re-reading or
  re-decoding descriptors.
- Future descriptor extensions must update ABI documentation, decision records,
  and validation tests before RTL behavior changes.

## ADR-0012: Phase 9 Integrated Tiled GEMM Scheduler

Status: Accepted.

Date: 2026-06-26.

Context:

- Phase 8 already validates descriptors and issues decoded GEMM command fields.
- Phase 7 DMA engines require aligned, whole 128-bit beat transfers.
- The v1 ABI supports arbitrary M/N/K dimensions through padded, 16-byte-aligned
  row strides and tile masks.

Decision:

- Phase 9 implements `npu_gemm_accelerator.sv` as a decoded-command consumer
  with a valid/ready command interface.
- The accelerator uses one shared read DMA for A and B tile loads, one write DMA
  for C writeback, local `16x16` A/B tile buffers, and the Phase 4 systolic
  array.
- The scheduler iterates output tiles in M then N order and accumulates all K
  tiles before writing C.
- C writeback emits only the active 16-byte chunks required by each output row
  and zeros inactive lanes in the final chunk.
- A `stage_o` signal exposes major load, compute, store, done, and error stages
  for tests and wave debug.

Rationale:

- Consuming decoded commands preserves the Phase 8 descriptor boundary and keeps
  descriptor validation separately testable.
- Whole-beat tile row DMA matches the v1 alignment policy without adding
  partial-beat AXI behavior.
- Writing only active C chunks prevents edge tiles from overwriting the next row
  when `N < 16` or on the final N tile.
- Stage observability satisfies the Phase 9 debug requirement without changing
  the external ABI.

Alternatives:

- Re-decode descriptors inside the GEMM accelerator. Rejected because it would
  duplicate Phase 8 validation and increase ABI drift risk.
- Write a full 16-column C tile for every active row. Rejected because valid
  descriptors may use a C row stride smaller than 64 bytes when `N < 16`.
- Add multiple outstanding DMA requests. Rejected because v5 is the planned
  phase for multi-outstanding data movement.

Impact:

- Phase 10 software can rely on the existing descriptor ABI while host tests
  validate the decoded command path through the integrated accelerator.
- Later top-level integration must arbitrate descriptor fetch reads and tensor
  reads, or sequence them so only one read master owns the AXI4 read channel at
  a time.
- Future post-processing remains out of scope for v1 and must be added through
  roadmap and decision-log updates.

## ADR-0013: Phase 10 C Driver ABI Sharing And Validation

Status: Accepted.

Date: 2026-06-27.

Context:

- Phase 10 must provide a minimal RISC-V-facing C driver.
- The register map and descriptor layout are frozen in `docs/INTERFACE.md`.
- Firmware should catch software-side argument mistakes before ringing the
  hardware doorbell.

Decision:

- Phase 10 adds shared ABI headers under `include/` for register constants and
  descriptor layout.
- The C driver lives under `sw/` and exposes init, capability, descriptor build,
  submit, poll, wait, error, clear, and performance APIs.
- `holon_npu_gemm_desc_t` uses the natural C layout plus compile-time size and
  offset checks instead of a packed layout.
- The driver validates descriptor flags, dimensions, tensor alignment, row
  strides, descriptor physical address alignment, and busy status before
  software-triggered MMIO side effects.
- Host-side driver tests use a volatile register aperture to verify API behavior
  without introducing a new RTL top in Phase 10.

Rationale:

- Shared headers give firmware and host tests one software-visible ABI source.
- Static descriptor checks catch C struct layout drift at build time.
- Natural layout keeps 64-bit fields naturally aligned while still matching the
  frozen 128-byte descriptor ABI.
- Early software validation avoids submitting descriptors that Phase 8 hardware
  would reject and makes driver failures easier to diagnose.

Alternatives:

- Duplicate constants inside the driver source. Rejected because it increases
  ABI drift risk.
- Use a packed descriptor struct. Rejected because the natural field order
  already matches the ABI and avoids unnecessary unaligned 64-bit fields.
- Test the driver only through the RTL control-plane testbench. Rejected for
  Phase 10 because the driver API can be tested directly with a register
  aperture, while full system-level coupling belongs in later integration or
  hardening work.

Impact:

- Phase 11 hardening should include these driver tests in the regression path.
- Any future ABI change must update `docs/INTERFACE.md`, the shared headers,
  static layout checks, and driver tests together.

## ADR-0014: Phase 11 Deterministic Hardening And Regression Gate

Status: Accepted.

Date: 2026-06-27.

Context:

- Phase 11 must harden v1 beyond happy-path tests.
- Randomized tests must be reproducible and their seeds must be visible.
- The project already has focused RTL lint tests and CTest-backed simulation
  targets.

Decision:

- Phase 11 extends the existing command processor and GEMM integration tests
  with deterministic hardening cases instead of adding nondeterministic test
  runners.
- Descriptor fuzzing uses fixed seed indices and maps each generated mutation to
  a documented expected error code.
- Integrated GEMM hardening covers reset while work is in flight, read response
  error injection, and write response error injection.
- `v1_lint` aggregates all v1 RTL lint targets.
- `v1_regression` and the `regression` presets run the full v1 verification
  suite.

Rationale:

- Deterministic hardening makes failures reproducible without external seed
  capture infrastructure.
- Extending existing benches keeps each failure close to the block under test.
- A single lint/regression entry point makes the release gate less error-prone.

Alternatives:

- Use nondeterministic fuzzing by default. Rejected because failures would be
  harder to reproduce and would violate the verification rules.
- Add a separate coverage framework in v1. Deferred because the current v1 gate
  needs deterministic lint and functional hardening first; future coverage
  instrumentation can be added without changing the ABI.

Impact:

- Any future randomized test must either use fixed seeds or print the failing
  seed before returning failure.
- Release validation should use both the debug preset and regression preset.

## ADR-0015: v1 Product Top And Review Hardening Corrections

Status: Accepted.

Date: 2026-06-27.

Context:

- A post-implementation review found that module-level tests existed, but the
  public AXI-Lite + descriptor + GEMM path did not have a product-level top.
- The same review found AXI-Lite write-channel coupling, insufficient
  soft-reset/counter wiring, broadcast-style array feeding, scratchpad
  integration gaps, and ABI duplication risks.

Decision:

- Add `npu_top.sv` as the v1 product top that connects AXI-Lite control,
  descriptor command processing, GEMM execution, IRQ/status, soft reset,
  performance-counter clear, AXI4 read arbitration, and AXI4 writeback.
- Make the AXI-Lite control slave accept AW and W independently, with focused
  tests for AW-before-W and W-before-AW.
- Treat `npu_control_regs.sv` as the architectural performance-counter owner:
  `PERF_CYCLES` counts descriptor-in-flight cycles and `PERF_BUSY_CYCLES`
  counts backend-active cycles.
- Define the systolic-array boundary contract as A-left/B-top propagation with
  explicit boundary-valid signals, and run a full 47-cycle wavefront for each
  `16x16x16` tile.
- Add `npu_gemm_tile_scratchpad.sv` as the integrated GEMM A/B tile storage
  module instead of keeping A/B tile arrays local to the scheduler.
- Extend `npu_pkg.sv` with ABI register, descriptor, opcode, and flag constants,
  and add `tools/check_abi_consistency.py` to compare RTL constants against the
  public C headers in CTest.

Rationale:

- A product top is the only meaningful v1 integration boundary for software
  and SoC attachment.
- Independent AXI-Lite channel acceptance is required for interoperability with
  real AXI-Lite masters and interconnects.
- Public counters must be observable through the register file and must have
  semantics that distinguish total descriptor latency from backend activity.
- A boundary-fed systolic contract matches the documented matrix-engine
  architecture and catches propagation/tail-mask errors that a broadcast MAC
  grid would hide.
- An explicit scratchpad module keeps tile storage inside the datapath
  ownership boundary and allows future ping-pong or banking improvements
  without changing GEMM command semantics.
- Automated ABI checks reduce drift risk without introducing a code generator
  into the v1 build.

Alternatives:

- Keep module-only verification and document the missing product top as a
  limitation. Rejected because it contradicts the v1 product goal.
- Keep broadcast MAC feeding and revise the docs. Rejected because the roadmap
  explicitly requires a systolic array.
- Generate all headers and RTL packages from one schema in v1. Deferred because
  the consistency checker closes the current drift risk with less build-system
  complexity.

Impact:

- `ctest --preset debug` includes `npu_top` and `abi_consistency`.
- Future ABI edits must update `npu_pkg.sv`, public C headers, docs, and tests
  together.
- Future matrix-engine changes must preserve or explicitly revise the
  boundary-valid systolic contract.

## ADR-0016: AXI Error Drain And Terminal Epoch Restart Semantics

Status: Accepted.

Date: 2026-06-27.

Context:

- A release-readiness review found that a non-final AXI4 read response error
  could leave the shared top-level read owner stuck because the read DMA dropped
  `RREADY` before the external burst reached `RLAST`.
- Integration testing also showed that sticky terminal outputs from a previous
  command or DMA transfer can be sampled as if they belonged to a newly started
  descriptor unless restart handshakes define a new execution epoch.

Decision:

- `npu_axi4_read_dma.sv` drains the remainder of an errored read burst by
  keeping `RREADY` asserted until `RLAST`, suppressing output beats, and only
  then reporting `ERR_AXI_READ`.
- DMA, command, and GEMM terminal outputs are suppressed during the restart
  handshake that launches the next execution epoch.
- `npu_top.sv` masks stale GEMM terminal status while descriptor fetch/decode is
  active for a new command.
- Public top-level verification covers descriptor-read error drain and recovery,
  GEMM tensor read error propagation, GEMM write response error propagation,
  reset-in-flight recovery, `16x16x16`, `64x64x64`, and top-level AXI-Lite AW/W
  skew.

Rationale:

- AXI4 slaves may continue returning beats after an error response; the v1
  single-outstanding read arbiter must consume through `RLAST` to release
  ownership deterministically.
- Terminal `done/error` status is intentionally sticky for software
  observability, but sticky status from an old command must not be interpreted
  as the terminal state of the next command.
- Encoding these semantics in RTL and tests gives software a reliable
  clear-and-resubmit flow after documented error states.

Alternatives:

- Treat the first read-error beat as an immediate abort and require system reset
  for recovery. Rejected because it can strand a shared AXI read channel.
- Require software to use `CONTROL.SOFT_RESET` after every error before
  resubmission. Rejected because `CLEAR.ERROR` is documented as sufficient to
  clear terminal error state.
- Make all terminal outputs one-cycle pulses. Rejected because sticky module
  terminal outputs simplify focused module tests and debug; restart-time
  suppression preserves observability without confusing new epochs.

Impact:

- Read DMA error tests must confirm all beats through `RLAST` are accepted after
  an injected non-final read response error.
- Top-level tests must include clear-and-resubmit recovery after descriptor
  fetch read errors.
- Future multi-outstanding DMA or multi-queue work must revisit owner release
  and terminal epoch tracking explicitly.

## ADR-0017: Formal Project Rename To HolonNPU

Status: Accepted.

Date: 2026-06-28.

Context:

- The upstream repository and project identity were renamed from `my_npu` to
  `HolonNPU`.
- The v1 public C API used the old project prefix in header filenames, macros,
  types, functions, CMake targets, tests, and ABI consistency tooling.
- RTL module names, RTL filenames, and `NPU_*` SystemVerilog package constants
  describe generic hardware blocks and ABI fields rather than the project brand.

Decision:

- Use `HolonNPU` as the formal project and documentation display name.
- Rename the public C API and ABI headers from `my_npu_*` / `MY_NPU_*` to
  `holon_npu_*` / `HOLON_NPU_*`.
- Do not keep compatibility aliases for the former public C API names.
- Keep internal RTL module names, RTL filenames, and SystemVerilog `NPU_*`
  constants unchanged.
- Update CMake project metadata, driver targets, CTest names, CI branding,
  documentation, examples, and the ABI checker to the new public name.

Rationale:

- The public C API is still pre-stabilization beyond v1 source release scope, so
  a clean breaking rename is simpler and less ambiguous than carrying duplicate
  legacy names.
- Avoiding aliases prevents firmware and tests from silently mixing old and new
  API surfaces.
- Keeping RTL `npu_*` names preserves stable hardware-internal terminology and
  avoids noisy RTL churn that does not improve the external contract.
- Preserving historical progress entries with their original command names keeps
  the project log accurate instead of rewriting past verification records.

Alternatives:

- Keep the old public C API as compatibility aliases. Rejected because it would
  expand the supported surface before the API has external compatibility
  obligations.
- Rename every RTL module and file to include `holon_npu`. Rejected because the
  existing RTL names describe block function cleanly and are not old brand
  leakage.
- Rewrite historical progress records to remove all former names. Rejected
  because the progress log is an audit trail of commands that actually ran.

Impact:

- Firmware and host code must include `holon_npu_regs.h`,
  `holon_npu_desc.h`, and `holon_npu_driver.h`.
- Firmware and host code must use `HOLON_NPU_*` macros and `holon_npu_*`
  public types/functions.
- `tools/check_abi_consistency.py` compares RTL `NPU_*` constants against C
  `HOLON_NPU_*` constants.
- The existing `v1` git tag remains unchanged and is not moved or recreated.

## Resolved Phase 2 Decisions

- AXI-Lite register offsets, access modes, reset values, and side effects are
  frozen in `docs/INTERFACE.md`.
- GEMM descriptor binary layout, alignment, versioning, and endian assumptions
  are frozen in `docs/INTERFACE.md`.
- Descriptor queue semantics are recorded in ADR-0004.
- Interrupt status, enable, and clear behavior are frozen in
  `docs/INTERFACE.md`.
- Capability register fields are frozen in `docs/INTERFACE.md`.
- Error/status code values are frozen in `docs/INTERFACE.md`.
- Software API names and return-level semantics are defined in
  `docs/INTERFACE.md`.
- Output-stationary dataflow and INT8/INT32 arithmetic are recorded in ADR-0005.
- Clock, reset, and valid-ready conventions are recorded in ADR-0007.
- Scratchpad, tile-mask, and ping-pong flow conventions are recorded in
  ADR-0008.
- AXI-Lite control-plane semantics are recorded in ADR-0009.
- AXI4 DMA alignment and burst policy is recorded in ADR-0010.
- Descriptor validation and command issue behavior is recorded in ADR-0011.
- Integrated tiled GEMM scheduling is recorded in ADR-0012.
- C driver ABI sharing and validation policy is recorded in ADR-0013.
- Deterministic v1 hardening and regression policy is recorded in ADR-0014.
- Product-top corrective hardening, systolic boundary feeding, scratchpad
  integration, and ABI consistency checking are recorded in ADR-0015.
- AXI read-error drain and terminal epoch restart semantics are recorded in
  ADR-0016.
- The formal HolonNPU project rename and public C API breaking rename are
  recorded in ADR-0017.
