# HolonNPU V2 Architecture Draft

This document describes the planned V2 programmable NPU tile. It is not a
description of the current V1.5 RTL. V2 implementation must update this file,
the roadmap, the decision log, the ABI schema, and verification documentation
before depending on new public behavior.

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

See `docs/V2_ISA.md` for the draft ISA contract.

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
  local-memory reads after a frontend `wait` or `fence`.
- Vector and matrix writes to local memory are visible to later DMA stores after
  a frontend `fence`.
- Completion records written to system memory are visible only after the
  associated DMA completion event and host-side cache maintenance required by
  the platform.
- Host cache maintenance, physical memory allocation, and address translation
  remain platform/runtime responsibilities.

The first V2 implementation may keep one active AXI transaction per DMA engine
while still exposing queued commands with in-order completion. Larger
outstanding windows are an implementation capability, not a semantic change.

## Matrix Micro-Op Contract

The matrix engine is exposed as a tile resource, not as V1 scheduler timing.
Required matrix micro-op operands:

- local A tile address;
- local B tile address;
- local C accumulator/output address or accumulator ID;
- active M/N/K shape up to the hardware tile limit;
- edge masks;
- clear, accumulate, and store mode bits;
- completion event ID.

Required matrix semantics:

- INT8 A/B operands and INT32 accumulation match V1 wraparound behavior.
- Inactive lanes do not update architectural C elements.
- Firmware cannot observe or depend on internal wavefront cycle timing.
- Completion means all architectural C accumulator effects for the issued tile
  are visible to later local-memory reads or DMA stores after a frontend fence.
- Illegal tile shapes, local addresses, or unsupported modes report a matrix
  issue fault.

## Execution Flow

1. Host software writes a V2 program descriptor address and rings the doorbell.
2. Hardware fetches and validates the program descriptor.
3. Hardware loads or exposes program code, arguments, and local memory
   configuration to the frontend.
4. The frontend starts at the descriptor entry PC.
5. The frontend issues DMA commands to move tensors into scratchpad.
6. The frontend issues vector/helper and matrix micro-ops.
7. Engines report completion or faults through the issue fabric.
8. The frontend writes completion state and reports done/error to the control
   plane.
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

Expected to change:

- descriptor ABI, moving from GEMM descriptor to program descriptor;
- command processor, moving from descriptor-specific decode to frontend
  lifecycle management;
- GEMM scheduler, moving from hardcoded FSM to frontend-issued matrix micro-ops;
- software driver, moving from GEMM submit to program submit.

The V1 ABI 2.0 values remain the V1.5 release contract. V2 ABI 3.0 must be
generated from the schema when implementation begins.
