# HolonNPU

`HolonNPU` is a roadmap-first SystemVerilog implementation of a v1.1 INT8 GEMM
accelerator intended for RISC-V SoC integration. The public hardware interface
is an AXI-Lite control plane plus AXI4 master DMA. The matrix engine is a
parameterized `16x16` B-weight-stationary systolic array with signed INT8
inputs and signed INT32 outputs.

The v1.1 scope is intentionally narrow: one descriptor in flight, one outstanding
AXI4 burst per DMA engine, descriptor-driven GEMM, deterministic Verilator
simulation, and a minimal C driver ABI.

## Status

v1.1 implementation is complete against the project roadmap:

- SystemVerilog RTL for common infrastructure, control registers, DMA,
  descriptor command processing, tiled GEMM execution, and product top.
- C++26 Verilator testbenches registered through CTest.
- C driver and shared public ABI headers.
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
```

## Requirements

- CMake 4.0 or newer.
- Ninja.
- Verilator.
- C++26-capable compiler.
- C11-capable compiler for the driver library.
- Python 3 for ABI consistency checks.

The current local verification environment used CMake 4.3.4, Verilator 5.048,
and GCC 15.3.0.

## Repository Layout

```text
docs/       Roadmap, architecture, ABI, verification, progress, and ADRs.
include/    Public C ABI headers for registers and descriptors.
rtl/        Product/core SystemVerilog RTL.
sim/        Verilator C++26 testbenches and simulation-only SV harnesses.
sw/         Minimal C driver.
tests/      Host-side driver tests.
tools/      Project verification utilities.
```

SystemVerilog files under `sim/rtl/` are Verilator/C++ harnesses only. They are
not product RTL and must not be used as internal architecture boundaries.

Start with `docs/GETTING_STARTED.md` if you want a guided Chinese walkthrough
of the architecture, code layout, local build flow, testbenches, and GitHub
Actions CI.

## Architecture

The v1.1 product top is `rtl/integration/npu_top.sv`.

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

## ABI Summary

The v1.1 ABI 2.0 contract is frozen in `docs/INTERFACE.md` and mirrored by:

- `rtl/common/npu_pkg.sv`
- `include/holon_npu_regs.h`
- `include/holon_npu_desc.h`

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
python3 tools/check_abi_consistency.py
```

It is also registered as the `abi_consistency` CTest.

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

Build and run one specific test:

```sh
cmake --build --preset debug --target npu_top_tb
ctest --preset regression -R npu_top --verbose
```

Use `--target <test-target>` for focused builds and `ctest -R <test-name>` for
focused test runs. The presets stay intentionally small; CMake targets and CTest
filters handle the one-off cases.

`CMakePresets.json` is intentionally retained only to pin the build directories,
build types, Ninja generator, and the three stable test entry points. It is not
used for subsystem shortcuts or version-specific build targets.

## Software Driver

The minimal driver lives in `sw/` and uses the public headers in `include/`.
It supports:

- Device binding and capability reads.
- GEMM descriptor construction with validation.
- Descriptor submit and status polling.
- Timeout-based wait.
- Error-code and performance-counter reads.
- Clear operations for done, error, and performance counters.

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
- Public product-top descriptor fetch, GEMM execution, IRQ/status, read error,
  write error, read-error recovery, and reset-in-flight recovery.
- Host-side C driver API behavior.

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
