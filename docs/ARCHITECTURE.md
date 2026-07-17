# HolonNPU Architecture

This document is the current product architecture contract. HolonNPU is a
programmable NPU tile: software submits a program, a replaceable frontend
implementation executes the Holon ISA, and interface-native engines perform
DMA, vector, and matrix work.

The `v1.5` tag preserves the former descriptor-driven GEMM product. It is not a
second architecture inside the current source tree.

## Product Boundary

`npu_top` is the synthesizable SoC pin boundary. It flattens AXI-Lite and AXI4
signals only at that boundary and immediately creates `npu_axi_lite_if` and
`npu_axi4_if` instances. All product interconnect below it uses SystemVerilog
interfaces and modports.

The current product graph is:

```text
Host AXI-Lite
    -> control registers and lifecycle
Host AXI4 memory
    <-> descriptor/program/argument loader
    <-> frontend-issued DMA
    <-> completion writer

loader -> local program memory + data scratchpad
local program memory -> reference frontend
reference frontend -> DMA / vector / matrix / sync issue interfaces
DMA + vector + matrix + scalar frontend <-> data scratchpad
frontend terminal event -> completion writer -> lifecycle/IRQ
```

Flattened Verilator access is provided only by wrappers under `sim/rtl/`.
Those wrappers are verification infrastructure and are not product modules.

## Control And Loading

The host programs the 64-bit program descriptor address through AXI-Lite and
rings `DOORBELL.START`. The control plane enters `LOADING`; the loader then:

1. reads and validates the 128-byte ABI 3.0 program descriptor;
2. checks ABI/ISA versions, program format, capabilities, operation classes,
   reserved fields, alignment, and local-memory bounds;
3. copies the program image into local program memory;
4. copies the argument block into data scratchpad;
5. starts the frontend only after all accepted AXI and local writes complete.

Descriptor alignment remains 16 bytes, program alignment 4 bytes, and
argument/completion alignment 16 bytes. AXI masters split bursts at every 4 KiB
boundary; legal ABI alignment never substitutes for AXI boundary compliance.

## Frontend Contract

Holon owns the complete program ISA. A frontend may be replaced only if the new
implementation preserves the same program binaries and architectural behavior.
The stable `npu_frontend_if` boundary includes:

- program-memory fetch request/response;
- lifecycle start, halt, resume, debug-step, and safe-reset controls;
- PC, terminal state, fault code, and retired-instruction reporting;
- DMA, vector, matrix, and synchronization issue/result channels;
- local scalar data-memory access.

The reference frontend implements scalar registers, branches, program flow,
predicate/vector configuration, CSR/debug access, engine issue, and precise
fault/retirement behavior. It is not RISC-V binary compatible and does not make
RISC-V or RVV part of the product contract.

## Local Memory

HolonNPU uses explicit local memory rather than coherent caches:

| Resource | Producer/consumer | Purpose |
| -------- | ----------------- | ------- |
| Program memory | loader / frontend fetch | Holon instructions. |
| Data scratchpad | loader, DMA, frontend, vector, matrix | Arguments, tensors, temporaries, command records. |
| Vector state | vector engine | Vector and predicate architectural state. |
| Matrix state | matrix engine | Stationary weights and INT32 accumulators. |

Dedicated request/response interfaces carry local-memory addresses, data,
strobes, errors, and backpressure. Arbitration preserves an accepted read owner
until its response. Product RTL exposes no host test-read probe; program-level
tests observe results through architectural DMA stores to simulated system
memory.

## DMA And Ordering

The frontend issues one in-order DMA command at a time. `dma.load` copies words
from system memory to scratchpad; `dma.store` copies scratchpad words to system
memory. The implementation supports backpressure on every AXI channel and:

- aligned 32-bit transfers packed into the 128-bit system bus;
- INCR bursts with at most 16 beats;
- automatic split at 4 KiB boundaries;
- complete read-error drain through `RLAST`;
- terminal write response handling;
- local-memory bounds and AXI response faults.

Instructions retire only after their engine result. `sync.wait_dma`,
`sync.fence.local`, and `sync.fence.dma` remain explicit architectural ordering
points so future queued or concurrent implementations can preserve the same
program semantics.

## Vector Engine

The vector engine implements integer and quantized VLA execution over reported
hardware VL and predicate resources. The implemented classes include:

- i8/u8/i16/u16/i32/u32 element types;
- load/store, configuration, and predicate state;
- add, subtract, min, max, compare, select, and shifts;
- widening/fixed-point helpers, clipping, saturation, narrowing, and requant;
- reductions and data-movement helpers defined by ISA metadata.

Inactive lanes preserve the ISA-defined destination behavior and cannot cause
out-of-range local-memory effects. Arithmetic, rounding, saturation, and fault
semantics are shared with the C++ architectural model.

## Matrix Engine

The matrix resource reuses the B-weight-stationary INT8 datapath:

- physical array dimensions are `ARRAY_K x ARRAY_N`;
- B weights remain stationary in PE registers;
- A operands propagate horizontally;
- partial sums propagate vertically;
- INT32 accumulation uses two's-complement wraparound.

The frontend issues `matrix.gemm` using a scratchpad command record containing
local A/B/C addresses, active M/N/K, accumulator selection, and
clear/accumulate/store modes. Firmware owns tile traversal. Internal wavefront
timing is not ISA-visible. Illegal shapes, addresses, accumulator IDs, and modes
produce `MATRIX_ISSUE` faults.

## Completion And Lifecycle

The lifecycle is:

```text
IDLE -> LOADING -> RUNNING -> DONE
                    |  |       |
                    |  +-> HALTED
                    +----> FAULT
any non-RESETTING state --SOFT_RESET--> RESETTING -> IDLE
DONE/FAULT --CLEAR_TERMINAL--> IDLE
```

`DONE` and `FAULT` are sticky. Software must issue `CLEAR_TERMINAL` before a new
doorbell. If a nonzero completion address is supplied, the 32-byte completion
record is fully acknowledged on AXI before terminal status or IRQ becomes
visible. A record beginning at address `...FF0` is written as two transactions
on adjacent pages.

Software reset is not an asynchronous internal reset. Acceptance immediately
exposes `RESETTING`, stops frontend fetch and new issue, and drains accepted AXI
and local-memory work. Already asserted VALID and payload remain stable until
handshake. Internal architectural state, terminal state, IRQ, and performance
counters clear only after every component reports quiescent. External
`aresetn` is the only reset allowed to abort transfers immediately.

## ABI And ISA Ownership

- `spec/holon_npu_abi.json` owns ABI 3.0.
- `spec/holon_npu_isa.json` owns ISA 1.0 and operation-class numbering.
- The ABI schema references ISA operation classes symbolically; generation
  derives capability masks and ISA version from both schemas.
- Generated RTL/C/reference files carry a do-not-edit banner.

See `docs/INTERFACE.md` and `docs/ISA.md` for software-visible semantics.

## Deliberate Limits

- One active program.
- One active command and one active AXI transaction per DMA engine.
- No cache coherence, IOMMU, multiple contexts, or graph scheduler.
- No BF16 or FP8.
- No compatibility aliases for retired product symbols.
