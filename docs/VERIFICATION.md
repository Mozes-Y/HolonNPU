# HolonNPU Verification Plan

This document defines simulation strategy, required tests, coverage goals, and
debug workflow. Phase 0 establishes structure. Each phase must add concrete
commands and tests as implementation grows.

## Tooling

- Build orchestration: CMake 4.0 or newer.
- RTL simulator: Verilator.
- Testbench language: C++26.
- Test runner: CTest.
- Wave debug: Verilator VCD/FST support when enabled by build options.

## Global Verification Rules

- Every test must be deterministic.
- Randomized tests must record seeds.
- Golden models must use exact signed integer arithmetic for v1.
- Tests must fail loudly on protocol errors, mismatched results, unexpected
  warnings, and timeout.
- Phase completion requires command results in `docs/PROGRESS.md`.

## Phase Test Matrix

| Phase | Required Verification |
| ----- | --------------------- |
| 0 | Documentation self-check. |
| 1 | CMake configure, build, and CTest smoke test. |
| 2 | ABI consistency review; document tables complete. |
| 3 | Verilator lint for common RTL; focused common-module tests. |
| 4 | PE and systolic array golden-model tests. |
| 5 | Scratchpad address, bank, tile-mask, and schedule tests. |
| 6 | AXI-Lite register read/write/reset/status tests. |
| 7 | DMA burst, response, memory-model, and error injection tests. |
| 8 | Descriptor fetch/decode/failure tests. |
| 9 | End-to-end GEMM deterministic and randomized tests. |
| 10 | Driver build and host-side API tests. |
| 11 | Regression, reset-in-flight, descriptor fuzz, lint, and coverage checks. |

## Coverage Goals

v1 functional coverage targets:

- Status transitions: idle, busy, done, and error.
- Descriptor validation: valid, bad version, bad opcode, bad size, bad flags.
- GEMM dimensions: `1x1`, `16x16`, non-multiple edge case, and `64x64`.
- Operand classes: positive, negative, zero, min/max INT8, and mixed signs.
- DMA behavior: single burst, multiple bursts, cross-tile access, read error,
  and write error.
- Reset behavior: reset at idle and reset while work is in flight.

## Debug Workflow

1. Reproduce with a deterministic CTest command.
2. Record the failing seed or descriptor.
3. Re-run with wave dumping enabled.
4. Identify whether the failure is interface, command, DMA, scratchpad, matrix,
   or software.
5. Add or update the smallest focused regression that catches the issue.
6. Record any known limitation in `docs/PROGRESS.md`.

## Initial Commands

Phase 1 must establish these commands:

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug -j 2
```

The `debug` test preset is a quick local path. Lint and full regression are
separate CTest presets so build and test scheduling remain distinct.

## Phase 2 ABI Review Checklist

Phase 2 is a documentation and ABI freeze gate. The review must confirm:

- `docs/INTERFACE.md` contains no placeholder values.
- Every AXI-Lite register has offset, width, access, reset value, and
  description.
- Every nontrivial AXI-Lite register has field bit ranges and reset values.
- Descriptor size, alignment, endian convention, opcode, flags, dimensions,
  addresses, strides, and reserved fields are defined.
- Status bits and error codes have exact values.
- Interrupt set, clear, enable, and line assertion behavior are defined.
- AXI4 address width, data width, burst limits, outstanding limits, alignment,
  and response mapping are defined.
- `docs/DECISIONS.md` records descriptor queue semantics, B-weight-stationary
  dataflow, ABI 2.0, and INT8/INT32 arithmetic.

## Phase 3 Common RTL Checklist

Phase 3 verification must confirm:

- Verilator lint passes for common package, interfaces, FIFO, skid buffer,
  register slice, and common smoke top.
- `tools/check_rtl_interface_usage.py` confirms that `npu_vr_if`,
  `npu_axi_lite_if`, and `npu_axi4_if` are used by product RTL cores, that core
  modules do not expose flattened AXI/valid-ready bus groups, and that
  simulation-only SV harnesses live under `sim/rtl/` instead of product source
  sets or `rtl/`.
- C++ tests instantiate the common smoke top through Verilator.
- Package constants match the frozen ABI values from `docs/INTERFACE.md`.
- FIFO tests cover reset, push, full backpressure, rejected push, ordered pop,
  and drain behavior.
- Skid buffer tests cover transparent pass-through, capture under downstream
  backpressure, consume/refill, and drain behavior.
- Register slice tests cover reset, capture, stall, consume/refill, and drain
  behavior.

## Phase 4 Matrix Core Checklist

Phase 4 verification must confirm:

- Verilator lint passes for the PE, parameterized array, and array simulation
  harness.
- PE tests cover reset, B weight loading, masked compute, psum pass-through,
  positive operands, negative operands, zero multiplication, and INT32
  wraparound boundary behavior.
- Array tests collect streamed C partial outputs and compare every active output
  against a C++ golden model.
- Array tests cover `1x1x1`, `16x16x16`, and `17x19x23` GEMM shapes.
- Array tests confirm inactive masked rows and columns remain invalid and zero.

## Phase 5 Scratchpad And Tiling Checklist

Phase 5 verification must confirm:

- Verilator lint passes for reusable scratchpad infrastructure, A/B tile buffer,
  C buffer, tile mask, ping-pong controller, product A-wavefront scratchpad,
  and tiling test top.
- Tile masks cover zero remaining elements, full `16x16x16` tiles, dimensions
  larger than one tile, and the `17x19x23` tail case of `1x3x7`.
- Valid reusable A/B/C buffer accesses read back expected data without range
  errors.
- Illegal bank and address accesses raise error flags and do not report ready.
- Load, compute, store, done, and bank toggle schedule states are observable.
- A masked tail compute path checks the `1x3x7` edge tile against a C++ golden
  model using product v1.1 B-stationary PE weights, streamed psum outputs, and
  Phase 5 masks.
- Documentation must distinguish reusable Phase 5 modules from the product
  active v1.1 path: `npu_gemm_tile_scratchpad.sv` supplies A wavefront/masks and
  psum timing, B rows load into PE weight registers, and C partial sums
  accumulate in the GEMM scheduler.

## Phase 6 AXI-Lite Control Checklist

Phase 6 verification must confirm:

- Verilator lint passes for the AXI-Lite control register RTL.
- Read-only reset registers match `docs/INTERFACE.md`.
- Writable descriptor address and IRQ enable registers read back expected values.
- AXI-Lite writes are tested with AW/W arriving together, AW before W, and W
  before AW.
- Reads from write-only pulse registers return zero.
- Read-only writes, unmapped accesses, reserved-bit writes, unsupported partial
  pulse-control writes, and doorbell writes while busy return `SLVERR`.
- A valid doorbell sets busy and emits one `command_start_o` pulse.
- Backend done and error test inputs drive the documented sticky status,
  interrupt, error-code, and performance counter behavior.
- `CLEAR`, `IRQ_STATUS` RW1C, and `CONTROL.SOFT_RESET` clear the documented
  state.
- `CONTROL.SOFT_RESET` and `CLEAR.PERF` produce backend-visible pulses.

## Phase 7 AXI4 DMA Checklist

Phase 7 verification must confirm:

- Verilator lint passes for read DMA, write DMA, and the DMA simulation
  harnesses.
- C++ memory-model tests exercise single-burst read and write transfers.
- C++ memory-model tests exercise multi-burst read and write transfers that
  cross a 256-byte burst boundary.
- Read and write burst addresses, lengths, sizes, and data contents match the
  expected 128-bit beat stream.
- Read response errors report `ERR_AXI_READ`.
- Read response errors on non-final beats keep `RREADY` asserted until `RLAST`
  so the owning AXI read channel can be released.
- Write response errors report `ERR_AXI_WRITE`.
- Unaligned addresses, zero byte counts, and non-16-byte-multiple byte counts
  are rejected before AXI traffic and report `ERR_UNSUPPORTED_ALIGNMENT`.

## Phase 8 Command Processor Checklist

Phase 8 verification must confirm:

- Verilator lint passes for the command processor and command simulation
  harness.
- A valid descriptor fetches as one 128-byte AXI4 read and issues exactly one
  decoded GEMM command.
- Decoded M, N, K, A/B/C base addresses, row strides, and descriptor flags match
  `docs/INTERFACE.md`.
- Invalid descriptor size, version, opcode, flags, dimensions, tensor alignment,
  row-stride constraints, and reserved fields produce documented error codes.
- Unaligned descriptor base address reports `ERR_UNSUPPORTED_ALIGNMENT`.
- AXI read errors during descriptor fetch report `ERR_AXI_READ`.
- Invalid descriptors do not assert the GEMM command issue interface.

## Phase 9 Integrated GEMM Checklist

Phase 9 verification must confirm:

- Verilator lint passes for the integrated GEMM accelerator.
- The C++ memory model drives the accelerator AXI4 read and write channels.
- `npu_top_tb` verifies the public AXI-Lite doorbell to descriptor fetch to GEMM
  writeback path through `npu_top.sv`.
- Product-internal connections use `npu_axi_lite_if`, `npu_axi4_if`, and
  `npu_vr_if`; flattened wrappers are limited to C++/Verilator test harnesses
  and the external `npu_top.sv` product pin boundary.
- Top-level tests cover AXI4 read arbitration between descriptor fetch and GEMM
  tensor reads.
- Top-level tests cover AXI-Lite AW-before-W and W-before-AW write timing at the
  public `npu_top.sv` boundary.
- Invalid descriptors fetched through the public top propagate documented error
  status, error code, and IRQ status to AXI-Lite registers.
- Descriptor fetch read errors through the public top drain the errored AXI read
  burst, enter `ERR_AXI_READ`, and recover after `CLEAR.ERROR` plus a later
  descriptor submission.
- GEMM tensor read response errors and C write response errors propagate
  `ERR_AXI_READ` and `ERR_AXI_WRITE` through public status registers.
- End-to-end GEMM tests cover `1x1x1`, `16x16x16`, `17x19x23`, and
  `64x64x64`.
- Signed INT8 randomized tests run with recorded deterministic seeds.
- Every logical INT32 C output matches the C++ golden model exactly.
- Edge writeback does not overwrite beyond the active C chunks for the final N
  tile.
- Completion IRQ and datapath performance counters update on successful
  completion.
- Stage observability covers load A, load B, compute, store, and done states for
  wave-debug correlation.

## Phase 10 Software Driver Checklist

Phase 10 verification must confirm:

- The C driver library builds through CMake.
- Shared ABI headers define register offsets, status bits, clear masks, hardware
  error codes, descriptor constants, and descriptor flags exactly as documented.
- The clear mask exposes only `CLEAR.DONE`, `CLEAR.ERROR`, and `CLEAR.PERF`;
  interrupt causes can also be cleared by writing `IRQ_STATUS` directly.
- `holon_npu_gemm_desc_t` is exactly 128 bytes and all documented descriptor field
  offsets are statically checked.
- Descriptor construction fills all required fields and zeros all reserved
  fields.
- Descriptor construction rejects invalid flags, zero dimensions, unsupported
  dimensions, unaligned tensor addresses, unaligned row strides, and too-short
  row strides before modifying hardware.
- Submit rejects unaligned descriptor physical addresses and busy hardware
  before writing descriptor registers or doorbell.
- Poll, wait, error, clear, and performance-counter API paths are covered by a
  host-side driver test.

## Phase 11 Hardening Checklist

Phase 11 verification must confirm:

- Randomized GEMM tests remain deterministic and record their seeds.
- Reset-in-flight testing resets active GEMM execution and verifies recovery
  with a later command.
- AXI read and write error injection reaches terminal error states with
  documented error codes.
- Terminal done/error outputs from a previous descriptor do not corrupt a later
  clear-and-resubmit execution epoch.
- Descriptor fuzzing covers multiple deterministic invalid descriptor seeds and
  expected validation failures.
- A single `lint` build target aggregates all RTL lint targets.
- Regression build and test presets run the full v1 verification suite.
- Known limitations are documented in `docs/PROGRESS.md`.
- ABI consistency checking compares RTL package constants against public C
  headers during CTest.
- RTL interface-usage checking runs during CTest and regression to keep the
  core interface-native and `sim/rtl/` harnesses test-only.

## v1 Release Checklist

The v1 release gate passes only when:

- `docs/ROADMAP.md` and `docs/PROGRESS.md` agree that Phase 0 through Phase 11
  are complete.
- `cmake --preset debug` configures successfully.
- `cmake --build --preset debug --parallel 2` builds all simulation and
  software targets.
- `ctest --preset debug -j 2` passes the fast local subset.
- `rtl_interface_usage` passes as part of `ctest --preset debug`.
- `ctest --preset lint -j 2` passes with no critical warnings.
- `cmake --preset regression` configures a RelWithDebInfo regression tree.
- `cmake --build --preset regression --parallel 2` builds optimized regression
  targets without running tests as part of the build step.
- `ctest --preset regression -j 2` passes the full test matrix.
- The source/document marker scan finds no unfinished placeholder entries in
  checked source and documentation paths.
- Root release documentation includes `README.md`, `CHANGELOG.md`, and an
  explicit license file or license decision.
- `docs/PROGRESS.md` records known limitations and no unexplained failures.
