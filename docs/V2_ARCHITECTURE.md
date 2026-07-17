# HolonNPU V2 Architecture

This document defines the implemented V2 programmable NPU tile architecture.
The released V1.5 implementation remains documented in `docs/ARCHITECTURE.md`;
V2 public behavior is owned by the V2 ISA/ABI schemas and the contracts linked
from this document.

## Product Direction

V2 turns HolonNPU from a descriptor-specific GEMM accelerator into a
programmable NPU tile. A replaceable frontend implementation runs Holon ISA
programs that schedule DMA, frontend-control work, vector work, and matrix
work. The V1 B-weight-stationary INT8 matrix engine remains valuable compute
IP, but its hardcoded descriptor scheduler is not the V2 control model.

V2 first release scope:

- ABI 3.0 program descriptor submission.
- Pluggable frontend microarchitecture boundary.
- Holon-owned vector/matrix ISA.
- Explicit scratchpad/local memory plus AXI4 DMA.
- Integer and quantized vector/helper operations.
- Frontend-issued INT8 matrix micro-ops using the existing systolic datapath.

V2 non-goals:

- BF16, FP8, coherent cache, IOMMU, multiple contexts, multiple descriptor
  queues, graph scheduler, or multi-NPU tile scaling.
- RVV binary compatibility.
- RVC compatibility for the NPU vector/matrix instruction stream.

## Top-Level Model

The V2 tile has these architectural blocks:

- AXI-Lite control plane for lifecycle, status, fault, debug, capability, and
  performance registers.
- Program descriptor fetch and validation.
- Frontend program/local memory.
- Replaceable frontend implementation.
- Engine issue fabric for vector, matrix, DMA, predicate, CSR, and sync
  commands.
- Data scratchpad and matrix tile buffers.
- Vector/helper execution engine.
- Reused INT8 matrix engine behind a micro-op issue boundary.
- AXI4 DMA engines for system-memory movement.

The CPU no longer submits a GEMM descriptor as the primary command. It submits a
program descriptor that points to code, argument data, local memory layout, and
completion state. In the first V2 release, hardware loads the program image into
local program memory and copies the argument block into data scratchpad before
frontend execution starts. The frontend does not fetch instructions directly
from system memory.

Current RTL checkpoint: `npu_v2_top` integrates ABI 3.0 control/lifecycle,
program descriptor loading, local program/data memory, the reference frontend,
DMA, integer/quant vector helpers, the matrix micro-op engine, completion-record
writeback, and AXI arbitration. Dedicated local-memory read/write interfaces
carry addresses, byte strobes, and explicit responses; engine traffic is never
packed into an ad hoc flattened payload. The C++26 architectural simulator and
public program runtime provide decode, semantic, and example-kernel references
for differential verification.

## Replaceable Frontend Implementation

Holon owns the V2 program ISA, including frontend-control, predicate, vector,
matrix, DMA, CSR/debug, sync, and system instructions. The replaceable boundary
is the frontend implementation, not the software-visible ISA. A reference
frontend may reuse RV32-style microarchitecture ideas internally, but it must
execute the Holon ISA and expose the same architectural behavior as any other
frontend implementation.

Required frontend boundary responsibilities:

- Fetch program instructions from local program memory.
- Read program arguments and descriptors.
- Maintain program counter, trap/fault state, and lightweight CSRs.
- Issue vector, matrix, DMA, predicate, and synchronization commands.
- Observe engine completion/faults.
- Report terminal done/error state to the control plane.

The boundary must allow replacing the reference frontend implementation without
changing program binaries, vector engine, matrix engine, DMA engines,
ABI-visible control registers, or test harness policy.

## Holon ISA Ownership

Holon instructions are an NPU ISA, not an RVV compatibility layer and not a
RISC-V scalar extension profile. The ISA must use first-class encoding space
for NPU work instead of compressing semantics into RISC-V extension leftovers.

Core rules:

- Instructions are 32-bit aligned in V2.
- Holon owns the complete V2 program binary format.
- RVC is intentionally not supported for the NPU instruction stream.
- The encoding space that RVC would consume is available to Holon vector,
  matrix, predicate, DMA, CSR, and sync instruction classes.
- RVV binary compatibility is not a goal.
- Vector semantics are vector-length agnostic, but instruction formats are
  chosen for NPU kernel expressiveness, not RVV format compatibility.

Architecture references:

- SVE-style predicate clarity and VLA execution are preferred.
- AVX-style rich operation grouping, permute, and reduction expressiveness are
  preferred.
- RVV lessons are accepted at the semantic level, but its constrained encoding
  and compatibility-driven complexity are rejected.

See `docs/V2_ISA.md` for the ISA contract.

## Execution Engines

### Frontend Control Path

The frontend control path handles program flow and low-rate arithmetic:

- integer ALU;
- branch and loop control;
- address generation;
- CSR access;
- engine issue;
- synchronization;
- fault classification.

It is not intended to perform high-throughput tensor math.

### Vector And Helper Engine

The first V2 vector engine focuses on integer and quantized operations:

- element widths: i8/u8/i16/u16/i32/u32;
- predicate and mask operations;
- elementwise add/sub/min/max/compare/select;
- shifts and fixed-point multiply helpers;
- clip, saturate, and requantization;
- reductions;
- transpose and tile movement helpers.

The vector engine must support a VLA programming model with a maximum hardware
vector length reported through capability registers.

### Matrix Engine

The V1 matrix datapath is reused as an INT8 matrix resource:

- B-weight-stationary PE array;
- A wavefront input;
- streamed partial sums;
- INT32 accumulation semantics.

V2 changes the control boundary. Matrix work is launched through frontend-issued
micro-ops rather than a fixed GEMM descriptor FSM.

### DMA And Scratchpad

V2 uses explicit local memory:

- program memory for frontend instructions;
- data scratchpad for vector and DMA traffic;
- vector register file;
- matrix tile buffers.

System memory access is done through AXI4 DMA. V2 does not introduce coherent
cache behavior. Software and microprograms are responsible for explicit data
movement.

V2 first-release local address spaces:

| Space | Access | Purpose |
| ----- | ------ | ------- |
| Program memory | Frontend fetch, loader write | Holon instruction image. |
| Data scratchpad | Frontend, vector, DMA | Kernel arguments, tensors, temporaries. |
| Vector register file | Vector engine | High-bandwidth vector operands/results. |
| Matrix buffers | Matrix engine, DMA/vector moves | A/B tiles and INT32 accumulator tiles. |
| MMIO/CSR window | Frontend control | Engine issue, events, fault, and debug CSRs. |

Architectural ordering rules:

- Program code is loaded into local program memory before frontend start.
- Argument data is copied into scratchpad before frontend start.
- DMA commands complete in program order within a DMA queue.
- A completed DMA-to-scratchpad command is visible to later vector and matrix
  local-memory reads after a frontend `sync.wait_dma`, `sync.fence.local`, or
  `sync.fence.dma`.
- Vector and matrix writes to local memory are visible to later DMA stores after
  a frontend `sync.fence.local` or `sync.fence.dma`.
- Completion records written to system memory are visible only after the
  associated DMA completion event and host-side cache maintenance required by
  the platform.
- Host cache maintenance, physical memory allocation, and address translation
  remain platform/runtime responsibilities.

The first V2 implementation may keep one active AXI transaction per DMA engine
while still exposing queued commands with in-order completion. Larger
outstanding windows are an implementation capability, not a semantic change.

Current RTL DMA slice:

- `dma.load` and `dma.store` are the first executable DMA instructions in the
  reference frontend.
- Instruction fields are schema-owned through `spec/holon_npu_isa.json`:
  opcode in bits `[27:24]`, scalar registers for system-address low/high in
  `[23:20]`/`[19:16]`, a scalar local-address register in `[15:12]`, and a
  1-to-4096 word count minus one in `[11:0]`.
- The frontend packs accepted DMA commands into the stable DMA issue channel as
  `{direction, byte_count, local_addr, system_addr}`, where direction `0`
  loads system memory into scratchpad and direction `1` stores scratchpad words
  back to system memory.
- `npu_v2_dma_fabric_core` accepts one command at a time, issues 32-bit AXI4
  read or write bursts, streams returned load words into data scratchpad, reads
  store words from data scratchpad before issuing the corresponding write
  address burst, and reports either completion or a DMA/local-memory/AXI fault
  event.
- `npu_v2_data_port_arbiter_core` owns the loader/host versus engine shared data scratchpad access
  contract. Loader writes have priority over client writes. Host/debug data
  reads and DMA/client data reads use a valid-ready local read request interface
  with round-robin selection and owner-routed one-cycle responses.
- `sync.wait_dma`, `sync.fence.local`, and `sync.fence.dma` are implemented as
  precise frontend-retired ordering points over the `sync_issue` contract. The
  current single-command in-order tile acknowledges that contract immediately
  because every DMA/vector/matrix instruction already retires only after its
  result; the explicit handshake keeps synchronization semantics visible at the
  replaceable frontend boundary.
- `npu_v2_engine_data_arbiter_core` uses round-robin write and read selection
  between DMA and vector clients, preserves the accepted read owner until the
  response, and exposes one interface-native engine client to the outer data
  scratchpad arbiter.
- The reference frontend dispatches vector config/memory/ALU instructions over
  a backpressured 128-bit issue stream and retires each instruction only after
  the 64-bit success/fault result handshake.
- The first implementation is single-command and in order. Multi-entry DMA
  queues remain a future performance extension without changing ordering
  semantics.

## Matrix Micro-Op Contract

The matrix engine is exposed as a tile resource, not as V1 scheduler timing.
Required matrix micro-op operands:

- local A tile address;
- local B tile address;
- local C output address and encoded accumulator ID;
- active M/N/K shape up to the hardware tile limit;
- rectangular edge masks derived from active M/N/K;
- clear, accumulate, and store mode bits;
- synchronous completion/fault result.

Required matrix semantics:

- INT8 A/B operands and INT32 accumulation match V1 wraparound behavior.
- Inactive lanes do not update architectural C elements.
- Firmware cannot observe or depend on internal wavefront cycle timing.
- Completion means all architectural C accumulator effects for the issued tile
  are visible to later local-memory reads or DMA stores after a frontend
  `sync.fence.local` or `sync.fence.dma`.
- Illegal tile shapes, local addresses, or unsupported modes report a matrix
  issue fault.

ISA 1.0 encodes one `matrix.gemm` instruction pointing to a 32-byte scratchpad
command record. Accumulator ID zero is the implemented resource. Firmware tile
traversal is supplied by the public runtime: it emits one command per M/N/K
tile, clears on the first K tile, accumulates later K tiles, and stores after the
last K tile. Internal 47-cycle wavefront timing remains non-architectural.

## Execution Flow

1. Host software writes a V2 program descriptor address and rings the doorbell.
2. Hardware fetches and validates the program descriptor.
3. Hardware loads or exposes program code, arguments, and local memory
   configuration to the frontend.
4. The frontend starts at the descriptor entry PC.
5. The frontend issues DMA commands to move tensors into scratchpad.
6. The frontend issues vector/helper and matrix micro-ops.
7. Engines report completion or faults through the issue fabric.
8. Hardware writes and acknowledges the optional completion record before
   reporting done/error to the control plane.
9. Software observes status, IRQ, fault code, debug snapshot, and performance
   counters.

## Reuse And Replacement Policy

Reusable from V1:

- AXI-Lite and AXI4 interface style;
- valid-ready stream interface;
- assertion and coverage discipline;
- ABI schema generation flow;
- INT8 systolic array and matrix datapath components;
- C++26 typed test runtime.

Changed for V2:

- descriptor ABI, moving from GEMM descriptor to program descriptor;
- command processor, moving from descriptor-specific decode to frontend
  lifecycle management;
- GEMM scheduler, moving from hardcoded FSM to frontend-issued matrix micro-ops;
- software driver, moving from GEMM submit to program submit.

The V1 ABI 2.0 values remain the V1.5 release contract. V2 ABI 3.0 is generated
independently from `spec/holon_npu_v2_abi.json`.
