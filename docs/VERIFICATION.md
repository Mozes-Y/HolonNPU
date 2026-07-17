# HolonNPU Verification

This document defines the current verification contract. Historical phase
checklists are intentionally not repeated here; the active release gate is the
source of truth.

## Verification Stack

- Build orchestration: CMake 4.0+ with Ninja.
- RTL simulation and lint: Verilator.
- Testbench language: C++26.
- C driver/API language: C23.
- Test runner: CTest presets.
- ABI sources: `spec/holon_npu_abi.json` for v1.5 and
  `spec/holon_npu_v2_abi.json` for V2.
- Assertions: native named SystemVerilog `assert property` and
  `cover property`, enabled through Verilator `--assert`.
- Coverage: dedicated coverage build with Verilator structural/user coverage
  plus a typed C++ functional coverage gate.
- V2 architectural model: stdlib-only C++26 code under `sim/model/`, used by
  program-level RTL differential tests.

## Required Gates

Fast local gate:

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug -j 2 --output-on-failure
ctest --preset lint -j 2 --output-on-failure
```

Release gate:

```sh
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression -j 2 --output-on-failure
cmake --preset coverage
cmake --build --preset coverage --parallel 2
ctest --preset coverage -j 2 --output-on-failure
python3 tools/check_coverage.py --build-dir build/coverage
```

Static checks that must stay green:

```sh
python3 -m json.tool CMakePresets.json
python3 tools/gen_abi.py --check
python3 tools/gen_isa.py --check
python3 tools/check_isa.py
python3 tools/gen_v2_abi.py --check
python3 tools/check_v2_abi.py
python3 tools/check_rtl_interface_usage.py
python3 tools/check_macro_policy.py
git diff --check
```

## ABI And Interface Consistency

- ABI, register, descriptor, error, and capability values are generated from
  the versioned schemas; Holon ISA encodings come from
  `spec/holon_npu_isa.json`.
- `tools/gen_abi.py --check` regenerates outputs into a temporary directory and
  byte-compares them with checked-in files.
- Product RTL must use `npu_vr_if`, `npu_axi_lite_if`, and `npu_axi4_if`
  internally.
- Test-only flattened SystemVerilog harnesses must live under `sim/rtl/` and
  must not be used as product architecture boundaries.

## Assertion Strategy

Assertions are written directly as native SVA in the interfaces and modules
they protect. Verilator `--assert` is enabled by default for debug,
regression, coverage, and lint paths.

Required assertion coverage:

- valid-ready payload stability under backpressure;
- AXI-Lite and AXI4 payload stability under `VALID && !READY`;
- control legal status, IRQ consistency, and busy-doorbell rejection;
- DMA burst profile, one-outstanding behavior, error drain, and terminal
  response behavior;
- command validation preventing invalid descriptors from issuing GEMM work;
- GEMM stage legality, tile bounds, and no writeback before all K tiles finish;
- top read-arbiter ownership stability until `RLAST`.
- V2 lifecycle legality, loader bounds, precise frontend retirement, local
  memory ownership, engine issue/result handshakes, completion-record ordering,
  and terminal visibility only after AXI write response.

`npu_assert_fail` is intentionally marked `WILL_FAIL` in CTest so the gate
proves assertions are compiled and active.

## Coverage Strategy

Coverage uses one dedicated `coverage` preset because Verilator coverage changes
generated model code.

- Functional coverage points are `enum class coverage_point` values in the
  C++ test runtime registry.
- CTest passes `--tb-coverage-root` explicitly for coverage tests.
- Each test emits required/hit functional manifests.
- `tools/check_coverage.py` checks manifests and merges Verilator raw coverage.
- Verilator line, toggle, FSM, and user coverage reports are generated for
  review, but structural percentage thresholds are not enforced yet.

Required functional coverage classes:

- descriptor success and validation failures;
- control status, IRQ, clear, and error transitions;
- DMA single-burst, multi-burst, read error, and write error paths;
- GEMM fixed shapes including `1x1x1`, `16x16x16`, `17x19x23`, and
  `64x64x64`;
- deterministic constrained-random GEMM and top-level tile shapes, including
  M/N/K tails and multi-tile cases;
- reset at idle and reset while work is in flight;
- V2 descriptor compatibility, frontend lifecycle/control flow, vector tails
  and helper classes, DMA/local-memory faults, matrix issue, program flags,
  completion ordering, CSR/debug reads, halt/debug, and IRQ policy;
- exact metadata-owned ISA class/instruction coverage names;
- deterministic random vector RTL/model differential programs, signed INT8
  matrix tiles with padded strides, and runtime-generated multi-tile GEMM.

## Debug Workflow

1. Reproduce with the narrowest CTest command, usually `ctest --preset debug -R
   <test> --verbose`.
2. Record the failing seed, descriptor, dimensions, and relevant base
   addresses.
3. If needed, rebuild with wave dumping support and inspect the smallest module
   boundary that shows divergence.
4. Fix the focused issue and add the smallest regression that would have caught
   it.
5. Update `docs/PROGRESS.md` only when the current gate result or known
   limitations change.

## Current Known Limits

- Functional coverage is hard-gated; structural coverage percentages are
  reported but not thresholded.
- Verilator is the active RTL verification backend.
- The V2 first implementation remains integer/quant only and uses one active
  program plus one active transaction per DMA engine.

## V2 Verification Contract

V2 extends the V1.5 gates rather than replacing them. The required reference is
the C++26 architectural simulator plus the public program runtime; test programs
use generated ISA constants or `program_builder`, never private encodings.

Model and host tests cover:

- ABI 3.0 descriptor compatibility, image/argument loading, and local bounds;
- wrapped descriptor, code, argument, completion, and DMA address rejection,
  plus joint argument/stack allocation bounds;
- generated decode/disassemble metadata and precise frontend PC/fault state;
- frontend control flow, DMA ordering, predicate state, and vector-length tails;
- signed/unsigned i8/i16/i32 ALU, saturation, select, gather, zip/unzip,
  transpose, reductions, and requantization;
- INT8-to-INT32 matrix clear/accumulate/store semantics and issue faults;
- public example programs for vector add, ReLU, reduction, requantization,
  transpose, and matrix GEMM.

RTL module and integration tests cover:

- lifecycle, doorbell policy, halt/resume/debug-step, sticky IRQ, terminal
  clear, descriptor flags, counters, and reset;
- descriptor/program/argument AXI reads, compatibility faults, and local writes;
- local-memory arbitration and DMA/vector/matrix ownership under backpressure;
- executable frontend-control, predicate, vector/helper, matrix, DMA,
  CSR/debug, sync, and system instructions with precise retirement and faults;
  sync operations cross an explicit backpressured issue handshake before
  retirement;
- completion-record done/fault contents, AXI write failure, and product-top
  ordering that withholds terminal MMIO/IRQ until `BRESP`;
- program-level output comparison with the architectural simulator;
- public runtime example images executed unchanged in simulator and RTL;
- public runtime-generated `17x19x23` and `64x64x64` matrix tile traversal,
  plus deterministic random single-tile matrix/vector differential tests.

Coverage is metadata-driven. Every implemented ISA instruction carries a
required coverage point, while the typed C++ registry gates lifecycle,
compatibility, engine, completion, and fault scenarios. Structural coverage is
reported separately and remains advisory until a threshold policy is adopted.
