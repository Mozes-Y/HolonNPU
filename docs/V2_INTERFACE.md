# HolonNPU V2 Interface Contract

This document defines the hand-written behavioral contract for V2 ABI 3.0.
`docs/INTERFACE.md` remains the generated V1.5 ABI 2.0 release contract; V2 uses
its own schema and generated reference while it proceeds through release
hardening.

Machine-checkable V2 ABI metadata lives in:

- `spec/holon_npu_v2_abi.json`

Generated V2 ABI reference artifacts:

- `include/holon_npu_program.h`
- `docs/V2_INTERFACE_REFERENCE.md`

## ABI Direction

V2 replaces the V1 GEMM descriptor with a program descriptor. The host submits a
program image and argument block. The first V2 release loads the program image
into local program memory and copies the argument block into data scratchpad
before the frontend executes the Holon ISA program and drives DMA,
vector/helper, and matrix engines.

Current ABI properties:

- ABI major version: 3.
- Little-endian multi-byte fields.
- AXI-Lite control plane remains the host-visible MMIO interface.
- AXI4 remains the system-memory DMA interface.
- Program descriptors must be aligned and sized by schema-defined constants.
- One program may be active in the first V2 release.
- V1 GEMM descriptor compatibility is not required.
- Program compatibility must be checked before frontend start when possible.

## Program Descriptor Fields

The ABI 3.0 descriptor schema defines:

| Field | Purpose |
| ----- | ------- |
| `size_bytes` | Descriptor size validation. |
| `version` | Required ABI major version, expected to be 3. |
| `holon_isa_major` | Required Holon ISA major version. |
| `holon_isa_minor` | Minimum Holon ISA minor version expected by the program. |
| `program_format` | Program image format and frontend profile identifier. |
| `required_caps_lo` | Required base capability bits. |
| `required_caps_hi` | Required extended capability bits. |
| `required_op_classes` | Required vector, matrix, DMA, predicate, and sync classes. |
| `flags` | IRQ, performance, debug, and lifecycle options. |
| `code_addr` | System-memory address of the program image. |
| `code_size_bytes` | Program image byte size. |
| `entry_pc` | Frontend entry PC relative to loaded program memory or defined base. |
| `arg_addr` | System-memory address of the kernel argument block. |
| `arg_size_bytes` | Argument block byte size. |
| `completion_addr` | Optional system-memory completion/status record. |
| `local_mem_bytes` | Requested local data scratchpad allocation, including arguments and reserved stack/control bytes. |
| `program_mem_bytes` | Requested program memory footprint. |
| `stack_bytes` | Optional region reserved at the top of local data memory; `arg_size_bytes + stack_bytes` must not exceed `local_mem_bytes`. |
| `reserved` | Must be zero and checked by hardware. |

The exact layout, widths, and reset/required values are schema-owned. RTL and
software consume the generated contract directly.

Compatibility check rules:

- `version` must match ABI major version 3.
- `holon_isa_major` must match an implemented major version.
- `holon_isa_minor` must be less than or equal to the implemented minor version
  for the same major version.
- `program_format` must identify a supported Holon program image format.
- Required capabilities and operation classes must be a subset of hardware
  capability registers.
- Compatibility failures are descriptor validation faults and must not start
  frontend execution.

Capability registers describe implemented hardware, not the eventual V2 ISA
roadmap. The current RTL advertises program descriptors, local program memory,
argument scratchpad copy, in-order DMA, integer vector, quant vector, and matrix
micro-op support. Its implemented operation classes are frontend control,
predicate, vector, quantization, matrix, DMA, CSR/debug, sync, and system. `VECTOR_CAP0`
reports max VL 16, one iterative execution lane, and one predicate register.
`MATRIX_CAP0` reports a 16x16 tile, 32-bit accumulation, and 8-bit operands.

Current RTL implementation note: `npu_v2_program_loader_core` fetches the
128-byte descriptor over AXI4, performs the compatibility/alignment/bounds
checks above, then reads the program image and argument block through 32-bit AXI4
narrow bursts. It emits typed valid-ready local write streams that
`npu_v2_local_memory_core` stores into local program memory and data scratchpad.

`npu_v2_control_plane_core` is the integration boundary between the
AXI-Lite lifecycle registers and the program loader. A doorbell starts descriptor
loading through the loader, loader completion moves lifecycle state to
`RUNNING`, loader/local-memory faults become lifecycle `FAULT` before frontend
execution, and loaded local words are exposed through frontend-facing local
read ports.

`npu_v2_frontend_tile_core` extends that slice with the reference frontend,
DMA issue consumer, and integer vector engine. The frontend's metadata-owned
`dma.load` and `dma.store` instructions drive the DMA issue channel,
`npu_v2_dma_fabric_core` performs system-to-scratchpad AXI4 reads and
scratchpad-to-system AXI4 writes, and `npu_v2_data_port_arbiter_core` arbitrates
loader/client data writes plus host/debug and engine/client valid-ready data
reads into the shared data scratchpad. A second interface-native arbiter merges
DMA and vector local-memory traffic. Vector instructions use a backpressured
issue/result contract and retire only after the vector result is accepted.
The tile also integrates quant/helper operations, frontend-control execution,
and the matrix micro-op engine. `npu_v2_top` is the flattened SoC pin boundary;
all connections beneath that boundary remain interface-native. The host-visible
contract is the ABI 3.0 program descriptor and lifecycle register model.

## Descriptor Flags

| Flag | Behavior |
| ---- | -------- |
| `IRQ_ON_DONE` | Allows a successful program terminal event to set the DONE IRQ cause. |
| `IRQ_ON_FAULT` | Allows a trusted running program fault to set the FAULT IRQ cause. Descriptor/load faults always raise FAULT IRQ because their flags are not trusted. |
| `CLEAR_PERF_ON_START` | Clears cycle and retired-instruction counters after loading and before frontend execution. |
| `DEBUG_SNAPSHOT_ON_FAULT` | Records the faulting frontend PC; otherwise the completion/MMIO debug PC is zero for program faults. |

Unknown flag bits are descriptor validation faults.

## Completion Record

`completion_addr=0` disables the system-memory completion record. A nonzero
address must be 16-byte aligned and identifies a 32-byte record containing ABI
version, terminal status, fault code, debug PC, cycle count, and retired
instruction count. The generated layout is authoritative.

For a validated program, the completion writer receives the terminal snapshot,
writes all record beats over AXI4, and waits for the write response before
`DONE`/`FAULT` and IRQ become visible through MMIO. A completion write response
error replaces the program terminal result with `AXI_WRITE` fault. Descriptor,
compatibility, or load failures do not write a completion record because their
descriptor address and flags are not yet trusted.

## Control Plane

The V2 AXI-Lite control plane preserves useful V1 concepts while changing the
backend from GEMM descriptor execution to frontend lifecycle execution.

Register groups:

- device identity and ABI version;
- capability registers for ISA version, vector lanes, max vector length, local
  memory sizes, matrix shape, supported data types, and supported operation
  classes;
- control pulses for soft reset, frontend halt, frontend resume, and optional
  debug step;
- status bits for idle, loading, running, halted, done, fault, and IRQ pending;
- fault code and frontend debug snapshot;
- program descriptor base address;
- doorbell/start;
- IRQ enable/status/clear;
- performance counters.

Any register offset reuse from V1 must be deliberate and recorded in the ABI
schema. The V2 design should prefer clarity over compatibility with the V1 GEMM
descriptor flow.

The schema-owned `CONTROL` write-one bits are listed below. A `CONTROL` write
may set at most one command bit; multi-command writes are rejected with a slave
error and must not partially execute.

| Bit | Name | Meaning |
| --- | ---- | ------- |
| 0 | `SOFT_RESET` | Cancel active work, clear sticky state, and return lifecycle to `IDLE`. |
| 1 | `CLEAR_TERMINAL` | Clear sticky `DONE` or `FAULT` terminal state and return to `IDLE`. |
| 2 | `HALT` | Request a precise frontend halt from `RUNNING`. |
| 3 | `RESUME` | Resume frontend execution from `HALTED`. |
| 4 | `DEBUG_STEP` | Request one debug step from `HALTED` if implemented. |

The schema-owned IRQ cause bits are:

| Bit | Name | Meaning |
| --- | ---- | ------- |
| 0 | `DONE` | Program completed successfully. |
| 1 | `FAULT` | Descriptor, frontend, engine, memory, or DMA fault occurred. |
| 2 | `HALTED` | Frontend reached a precise halted state. |
| 3 | `DEBUG_STEP` | A debug-step request was accepted. |

## Frontend Lifecycle

The ABI-visible lifecycle must be a single state machine:

| State | Meaning | Allowed exits |
| ----- | ------- | ------------- |
| `IDLE` | No active program and no terminal state is pending. | Valid doorbell starts descriptor load. |
| `LOADING` | Descriptor, program image, or argument setup is in progress. | `RUNNING`, `FAULT`, `IDLE` on reset. |
| `RUNNING` | Frontend is executing Holon program instructions. | `HALTED`, `DONE`, `FAULT`, `IDLE` on reset. |
| `HALTED` | Frontend execution is stopped at a precise architectural point. | `RUNNING` on resume, `IDLE` on reset, `FAULT` if halt cannot be made precise. |
| `DONE` | Program completed successfully. Sticky until clear or next accepted start. | `IDLE` on clear, `LOADING` on accepted start. |
| `FAULT` | Descriptor, frontend, engine, DMA, or memory fault occurred. Sticky until clear or next accepted start. | `IDLE` on clear, `LOADING` on accepted start. |

Lifecycle rules:

- Doorbell is accepted only from `IDLE`, `DONE`, or `FAULT` after required clear
  semantics are satisfied by the ABI.
- Doorbell while `LOADING`, `RUNNING`, or `HALTED` returns a defined busy error
  and has no state side effect.
- Soft reset cancels active work, clears engine events, and returns to `IDLE`.
- Halt requests during DMA or matrix work become visible only after the frontend
  reaches a precise `sync.wait_dma`, `sync.fence.local`, `sync.fence.dma`, or
  instruction boundary.
- Resume is valid only from `HALTED`.
- Debug step is valid only from `HALTED` if implemented.
- Faults have priority over done. Descriptor validation faults have priority
  over frontend execution start.
- IRQ status is sticky and derived from terminal, halt, or fault causes
  according to descriptor flags and IRQ enables.

## Frontend Fault Model

V2 must distinguish host submission failures from frontend/runtime failures.

Minimum fault classes:

- invalid program descriptor;
- unsupported ABI or ISA version;
- unsupported capability request;
- unsupported program format;
- unsupported operation class;
- code or argument alignment error;
- local memory allocation or bounds fault;
- illegal instruction;
- unsupported instruction;
- vector configuration fault;
- matrix issue fault;
- DMA request fault;
- AXI read or write response fault;
- explicit program fault or trap.

Fault codes must be generated from the ABI schema and covered by tests.

## Software API

The V2 C23 driver provides a program-oriented API:

| Function Class | Purpose |
| -------------- | ------- |
| init/caps | Bind MMIO base and read ABI/capability registers. |
| build descriptor | Construct and validate a program descriptor. |
| submit | Write descriptor address and ring doorbell. |
| lifecycle | Halt, resume, soft reset, and optional debug step. |
| wait/poll | Observe done, fault, halt, and timeout. |
| debug | Read frontend PC, fault code, and selected counters. |
| perf | Read and clear performance counters. |

The driver should not own cache maintenance, physical memory allocation,
interrupt registration, or program image compilation. Platform firmware or a
runtime layer provides those services.

## Memory Visibility Contract

V2 is not cache coherent. Software and runtime code must provide platform cache
maintenance before ringing the doorbell and after observing completion if the
host CPU caches system memory used by descriptors, code, arguments, tensors, or
completion records.

Program visibility rules:

- Program image and argument data are loaded into frontend/local memory before
  entering `RUNNING`; frontend instruction fetch does not read system memory
  directly in the first V2 release.
- DMA completion events make DMA writes to local memory visible to later local
  reads after frontend `sync.wait_dma`, `sync.fence.local`, or `sync.fence.dma`
  instructions. In the current in-order DMA implementation, each DMA
  instruction retires after completion, so these sync instructions are explicit
  order points rather than separate engine commands.
- Vector and matrix local-memory writes become visible to DMA stores after
  frontend `sync.fence.local` or `sync.fence.dma` instructions.
- Completion records in system memory are architecturally valid before the
  associated `DONE` or `FAULT` MMIO status becomes visible.

## Compatibility Policy

V2 may break V1 descriptor and driver source compatibility. The V1.5 release
remains the stable ABI 2.0 GEMM accelerator. V2 ABI 3.0 should keep useful
control-plane concepts such as status, IRQ, doorbell, and performance counters,
but it should not distort the program descriptor model to preserve V1 GEMM
descriptor shape.
