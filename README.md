# HolonNPU

`HolonNPU` v1.5 is the final V1-generation SystemVerilog implementation of an
INT8 GEMM accelerator intended for RISC-V SoC integration. The public hardware
interface is an AXI-Lite control plane plus AXI4 master DMA. The matrix engine
is a parameterized `16x16` B-weight-stationary systolic array with signed INT8
inputs and signed INT32 outputs.

The v1.5 final V1 scope is intentionally narrow: one descriptor in flight, one
outstanding AXI4 burst per DMA engine, descriptor-driven GEMM, deterministic
Verilator simulation, protocol assertions, functional coverage gating, and a
minimal C driver ABI.

## Status

v1.5 is complete against the V1 project roadmap:

- SystemVerilog RTL for common infrastructure, control registers, DMA,
  descriptor command processing, tiled GEMM execution, and product top.
- C++26 Verilator testbenches registered through CTest, including deterministic
  constrained-random GEMM tile coverage.
- Protocol-first SystemVerilog assertions for valid-ready, AXI-Lite, AXI4, DMA,
  command, GEMM, and top-level invariants.
- Functional coverage gate plus generated Verilator structural coverage reports.
- C driver and generated public ABI headers.
- Roadmap, architecture, interface, verification, progress, and decision
  documentation under `docs/`.

The release gate is:

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug -j 2 --output-on-failure
ctest --preset lint -j 2 --output-on-failure
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression -j 2 --output-on-failure
cmake --preset coverage
cmake --build --preset coverage --parallel 2
ctest --preset coverage -j 2 --output-on-failure
python3 tools/check_coverage.py --build-dir build/coverage
```

## V2 Implementation

V2 is being implemented as a programmable NPU tile rather than a GEMM
post-processing extension. The direction is ABI 3.0 program descriptors, a
replaceable frontend implementation running a stable Holon-owned program ISA,
integer/quant vector and helper engines, explicit scratchpad/DMA memory, and
frontend-issued matrix micro-ops reusing the V1 matrix engine.

The current V2 implementation includes machine-checkable ISA/ABI metadata,
generated ABI artifacts, a C++26 architectural simulator, a public C++26
program builder/runtime, and an interface-native ABI 3.0 product top. The RTL
loads program descriptors, code, and arguments; executes Holon frontend-control,
predicate, vector, quant/helper, matrix, DMA, sync, and system instructions;
and exposes lifecycle, fault, debug, IRQ, and performance state through
AXI-Lite. DMA, vector, and matrix engines share local memory through explicit
request/response interfaces and arbiters.

The integer/quant vector path supports signed and unsigned 8-, 16-, and 32-bit
operations, predication, saturation, requantization, reductions, select,
gather, zip/unzip, and 4x4 transpose. The matrix path issues tile-level
micro-ops to the reused B-weight-stationary systolic array. Program completion
records are written to system memory and acknowledged before terminal MMIO
state and IRQ become visible. Program-level RTL tests differential-check
architectural results against the C++26 simulator.

Planning documents:

- `docs/V2_ARCHITECTURE.md`
- `docs/V2_ISA.md`
- `docs/V2_INTERFACE.md`

Machine-checkable V2 metadata now includes:

- ISA source: `spec/holon_npu_isa.json`
- Program ABI source: `spec/holon_npu_v2_abi.json`
- Generated ISA header/reference: `include/holon_npu_isa.h`,
  `docs/V2_ISA_REFERENCE.md`
- Generated program ABI header/reference: `include/holon_npu_program.h`,
  `docs/V2_INTERFACE_REFERENCE.md`
- C++26 public program runtime: `include/holon_npu_runtime.hpp`,
  `sw/holon_npu_runtime.cpp`
- C++26 architectural simulator: `sim/model/`

The released `npu_top` remains the v1.5 ABI 2.0 top. The V2 implementation is
available as `npu_v2_top`; it is under release hardening and has not replaced
the v1.5 release tag.

## Requirements

- CMake 4.0 or newer.
- Ninja.
- Verilator.
- C++26-capable compiler.
- C23-capable compiler for the driver library and generated ABI headers.
- Python 3 for ABI generation, static checks, and coverage checks.

The current local verification environment used CMake 4.3.4, Verilator 5.048,
and GCC 15.3.0.

## Repository Layout

```text
docs/       Roadmap, architecture, ABI, verification, progress, and ADRs.
include/    Generated public C23 ABI headers and the C++26 V2 program runtime.
rtl/        Product/core SystemVerilog RTL.
sim/        Verilator C++26 testbenches, V2 model code, and simulation-only SV harnesses.
spec/       ABI/register/descriptor and ISA schemas used to generate shared outputs.
sw/         C23 driver and C++26 V2 program runtime implementation.
tests/      Host-side driver, runtime, and architectural-model tests.
tools/      Project verification utilities.
```

SystemVerilog files under `sim/rtl/` are Verilator/C++ harnesses only. They are
not product RTL and must not be used as internal architecture boundaries.

Start with `docs/GETTING_STARTED.md` if you want a guided Chinese walkthrough
of the architecture, code layout, local build flow, testbenches, and GitHub
Actions CI.

## Released V1 Architecture

The released v1.5 product top is `rtl/integration/npu_top.sv`. The V2 product
top under release hardening is `rtl/integration/npu_v2_top.sv` and is described
in `docs/V2_ARCHITECTURE.md`.

Major blocks:

- AXI-Lite control registers: descriptor address, doorbell, status, IRQ,
  clear, soft reset, and performance counters.
- Command processor: fetches one 128-byte descriptor over AXI4, validates the
  ABI, and issues one GEMM command.
- AXI4 DMA: 128-bit read and write DMA with 16-byte alignment and at most
  16-beat incremental bursts.
- Scratchpad and tiling datapath: loads A rows, loads B rows into stationary PE
  weights, masks tail dimensions, accumulates streamed C partials, and writes
  active C chunks.
- Matrix engine: B-weight-stationary `16x16` systolic array with signed INT8
  operands and signed INT32 wraparound psum accumulation.

See `docs/ARCHITECTURE.md` and `docs/INTERFACE.md` for the authoritative design
and ABI details.

## Released V1 ABI Summary

The v1.5 ABI 2.0 contract is defined in one schema:

- `spec/holon_npu_abi.json`

The schema generates these checked-in outputs:

- `rtl/common/npu_pkg.sv`
- `include/holon_npu_regs.h`
- `include/holon_npu_desc.h`
- `docs/INTERFACE.md`

Generated files are reviewable artifacts, but they must not be edited by hand.
Change the schema and regenerate instead.

Key properties:

- AXI-Lite register aperture: 4 KiB.
- AXI-Lite register width: 32 bits.
- AXI4 address width: 64 bits.
- AXI4 data width: 128 bits.
- Descriptor size: 128 bytes.
- Descriptor version: 2.
- Descriptor alignment: 16 bytes.
- Tensor base and row-stride alignment: 16 bytes.
- Operation: signed INT8 A/B GEMM to signed INT32 C.
- Output semantics: plain GEMM, no bias, activation, requantization, or
  accumulation with existing C.

Run the ABI checker with:

```sh
python3 tools/gen_abi.py --check
```

It is also registered as the `abi_generate_check` CTest.

## Build And Test

Configure:

```sh
cmake --preset debug
```

Build all debug targets:

```sh
cmake --build --preset debug --parallel 2
```

Run the fast local debug test subset:

```sh
ctest --preset debug -j 2 --output-on-failure
```

Run RTL lint tests:

```sh
ctest --preset lint -j 2 --output-on-failure
```

Build and run optimized full regression:

```sh
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression -j 2 --output-on-failure
```

Build and run the coverage gate:

```sh
cmake --preset coverage
cmake --build --preset coverage --parallel 2
ctest --preset coverage -j 2 --output-on-failure
python3 tools/check_coverage.py --build-dir build/coverage
```

Build and run one specific test:

```sh
cmake --build --preset debug --target npu_top_tb
ctest --preset regression -R npu_top --verbose
```

Use `--target <test-target>` for focused builds and `ctest -R <test-name>` for
focused test runs. The presets stay intentionally small; CMake targets and CTest
filters handle the one-off cases.

`CMakePresets.json` is intentionally retained only to pin the build directories,
build types, Ninja generator, and the stable debug/lint/regression/coverage test
entry points. It is not used for subsystem shortcuts or version-specific build
targets.

## Software Driver

The current C23 driver in `sw/` targets the V2 ABI 3.0 product path. It binds
MMIO, reads ISA/engine capabilities, validates and builds program descriptors,
submits programs, controls halt/resume/debug-step/reset, waits for lifecycle
completion, and reads fault snapshots, IRQ state, and performance counters.
The V1.5 GEMM ABI remains available through its generated headers and release
tag, but the current driver does not expose the old GEMM-submit API.

The driver does not manage cache maintenance, physical address allocation, IRQ
registration, or OS integration. Firmware or platform code must provide those
system services.

## Verification Coverage

The CTest suite covers:

- Common FIFO, skid buffer, and register slice behavior.
- PE and systolic-array golden-model comparison.
- Scratchpad, tile masks, and tiling datapath behavior.
- AXI-Lite reset, access, error, AW/W skew, IRQ, clear, and soft reset behavior.
- AXI4 read/write DMA burst splitting, alignment rejection, and error handling.
- Descriptor fetch, decode, validation, and deterministic descriptor fuzzing.
- Integrated GEMM for `1x1x1`, `16x16x16`, `17x19x23`, and `64x64x64`.
- Deterministic constrained-random GEMM tile shapes, including sub-16,
  exact-16, M/N/K tails, mixed tails, multi-tile shapes, and 64-sized anchors.
- Public product-top descriptor fetch, GEMM execution, IRQ/status, read error,
  write error, read-error recovery, and reset-in-flight recovery.
- Host-side C driver API behavior.
- V2 ABI/ISA generation, descriptor compatibility, frontend lifecycle, local
  memory/DMA ordering, vector/helper semantics, CSR/debug reads, and matrix
  micro-op faults.
- Deterministic random vector RTL/model differential programs and signed INT8
  matrix tiles with padded strides.
- Public runtime example images and runtime-generated `17x19x23`/`64x64x64`
  tiled GEMM programs through the integrated V2 RTL tile.

Assertions are native SystemVerilog properties enabled by default in debug,
regression, and coverage builds with Verilator's `--assert` option. The
`npu_assert_fail` CTest is marked `WILL_FAIL` so CI proves assertions are
active. C++ testbenches use a typed `coverage_point` registry and RAII
`test_run` runtime; coverage builds use Verilator CMake `COVERAGE`, pass
`--tb-coverage-root`, and emit raw, merged, info, annotated, required/hit, and
functional summary artifacts under `build/coverage/coverage/`.

## Known v1 Limits

- One descriptor in flight.
- One outstanding AXI4 read burst and one outstanding AXI4 write burst per DMA
  engine.
- Descriptor, tensor base, and row-stride values must be 16-byte aligned.
- No vector/post-processing engine in v1.
- No BF16, FP8, graph scheduling, convolution, full softmax, LayerNorm, or GELU
  implementation in v1.
- No IOMMU, address translation, multiple queues, multiple contexts, or multi-NPU
  tile scaling in v1.

Future work must be added through `docs/ROADMAP.md` and
`docs/DECISIONS.md` before implementation.

## License

All rights reserved. See `LICENSE`.
