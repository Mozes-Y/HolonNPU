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
- ABI source: `spec/holon_npu_abi.json`.
- Assertions: native named SystemVerilog `assert property` and
  `cover property`, enabled through Verilator `--assert`.
- Coverage: dedicated coverage build with Verilator structural/user coverage
  plus a typed C++ functional coverage gate.
- V2 architectural model: stdlib-only C++26 code under `sim/model/`, used as
  the future RTL differential reference.

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
  `spec/holon_npu_abi.json`.
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
- reset at idle and reset while work is in flight.

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
- The current product scope remains INT8 GEMM only.

## V2 Verification Planning

V2 verification must extend the current assertion, coverage, and deterministic
random strategy to a programmable NPU tile. The V2 plan does not relax any V1.5
gate; it adds new gate content once V2 RTL and ABI 3.0 exist.

Active V2 model coverage today:

- ISA class decode/disassemble checks using generated ISA constants.
- Frontend PC/state/fault progression for a minimal program.
- Local scratchpad loads/stores with bounds faults.
- Vector `i32` load/add/store execution with INT32 wraparound semantics.
- Explicit program-fault and vector-configuration fault paths.

Required V2 verification classes before RTL release:

- ABI 3.0 program descriptor generation and byte-checking from
  `spec/holon_npu_v2_abi.json`.
- Holon ISA encoding table uniqueness and reserved-space checks.
- Generated decoder and disassembler metadata checks.
- C++ architectural simulator for frontend state, decode, local memory, DMA
  ordering, vector state, and matrix micro-ops.
- Instruction decoder tests for frontend control, predicate, vector, matrix,
  DMA, CSR, sync, and system classes.
- Frontend lifecycle tests for boot, start, halt, resume, debug snapshot, done,
  fault, reset, and IRQ.
- Scratchpad bounds, DMA command, backpressure, and AXI response fault tests.
- Vector/helper golden-model tests for integer, predicate, tail, reduction,
  transpose, requant, clip, and saturation behavior.
- Matrix micro-op tests proving V1 GEMM behavior through the V2 frontend-issued
  path.
- Program-level constrained-random tests that generate deterministic kernels,
  record seeds, and compare RTL-visible behavior against the C++ architectural
  simulator.

Required V2 functional coverage classes:

- program descriptor success and validation failures;
- program compatibility failures for ISA version, program format, required
  capabilities, and operation classes;
- frontend lifecycle states and fault classes;
- every implemented ISA instruction class;
- predicate and vector-length tail behavior;
- integer/quant vector operation groups;
- DMA success, backpressure, and fault paths;
- matrix issue, completion, and fault paths;
- IRQ, halt, reset, and debug snapshot paths.
