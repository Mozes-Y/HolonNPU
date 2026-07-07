# HolonNPU V2 ISA Draft

This document defines the planning contract for the Holon V2 NPU instruction
set. It is a draft for architecture and verification work; exact bit encodings
must become machine-checkable metadata before RTL decode is implemented.

The first machine-checkable ISA metadata source is:

- `spec/holon_npu_isa.json`

Generated reference artifacts:

- `include/holon_npu_isa.h`
- `docs/V2_ISA_REFERENCE.md`

## Design Goals

Holon V2 needs an ISA that serves NPU kernels directly:

- predictable decoding;
- enough first-level opcode space for vector, matrix, DMA, predicate, CSR, and
  synchronization instructions;
- clear predicate/mask behavior;
- vector-length-agnostic execution;
- good immediate and register-field space for tensor kernels;
- room for future data types without compatibility contortions.

Holon owns the complete V2 program ISA. A frontend implementation may borrow
RV32-style microarchitecture techniques internally, but program binaries,
decoder metadata, fault behavior, and architectural state are defined by Holon,
not by RISC-V or RVV compatibility.

## Non-Goals

- No RVV binary compatibility.
- No RVC compressed instruction compatibility.
- No attempt to preserve 16-bit instruction holes for later use.
- No BF16, FP8, or floating-point vector/matrix operations in the V2 first
  release.

Rejecting RVC is an architectural choice to reclaim encoding and decode budget
for first-class NPU operations. It is not a temporary simplification.

## Instruction Envelope

V2 uses fixed 32-bit instruction words for the NPU instruction stream. The
encoding is Holon-owned and must be validated by an ISA metadata checker before
RTL decode lands.

Draft instruction classes:

| Class | Purpose |
| ----- | ------- |
| Frontend control | Integer ALU, branch, loop, address generation, lightweight control. |
| Predicate | Predicate register operations, mask generation, mask combine, active-lane tests. |
| Vector config | Set vector length, element width, grouping, rounding, saturation mode. |
| Vector ALU | Elementwise integer, compare, select, shift, fixed-point helpers. |
| Vector memory | Scratchpad vector load/store, strided/local tile movement. |
| Vector permute | Lane shuffle, pack/unpack, transpose helper operations. |
| Vector reduction | Sum, min/max, any/all, narrow/widen reduction helpers. |
| Quantization | Requant, clip, saturate, scale/zero-point helpers for integer tensors. |
| Matrix | Matrix tile load/issue/accumulate/store micro-ops for the reused INT8 engine. |
| DMA | System-memory to scratchpad transfers and scratchpad to system-memory transfers. |
| CSR/debug | Frontend CSRs, fault reporting, performance counters, debug snapshots. |
| Sync | Wait, fence, engine barrier, event/IRQ signaling. |
| System | Program exit, fault, halt, and privileged lifecycle operations. |

The final encoding table must leave clearly named reserved class space for
future BF16, FP8, multi-engine, and system-level extensions. Reserved space must
be intentional and documented; it must not be an artifact of trying to remain
compatible with RVV or RVC.

## Predicate And Mask Model

The predicate model should be explicit and easy to verify:

- Predicate registers represent active vector lanes.
- Most vector instructions take an explicit predicate operand.
- Inactive lanes do not update destination elements unless the instruction
  explicitly requests a merge behavior.
- Predicate-generating comparisons write predicate registers directly.
- Reductions define whether inactive lanes contribute identity values or are
  ignored.
- Matrix and DMA instructions use masks for edge tiles and bounds checking.

This should feel closer to SVE's clean predication than RVV's compatibility
constraints.

## Vector-Length-Agnostic Execution

V2 keeps the useful VLA idea:

- software asks for a vector length;
- hardware returns the actual active length up to the implementation maximum;
- loops are written against the returned active length;
- tail handling is expressed through predicates and vector length state.

V2 does not inherit RVV's encoding pressure. Vector config instructions should
have enough field space to express:

- element width;
- signedness when architecturally visible;
- vector grouping or register grouping;
- rounding mode;
- saturation mode;
- predicate behavior;
- operation sub-class.

The architectural vector state must include the current vector length, hardware
maximum vector length, element width, predicate selection, rounding mode, and
saturation mode. Any implementation-specific lane count must be discoverable
through capability registers and must not change program-visible results.

## Integer And Quant Data Types

The first V2 vector/helper ISA supports:

- signed and unsigned 8-bit lanes;
- signed and unsigned 16-bit lanes;
- signed and unsigned 32-bit lanes;
- predicate/mask lanes;
- widening/narrowing where needed for quantized arithmetic.

Required operation groups:

- add/sub/min/max;
- compare and select;
- logical and arithmetic shifts;
- fixed-point multiply/shift helpers;
- clip and saturate;
- requantization;
- pack/unpack;
- transpose/tile movement;
- reductions.

BF16, FP8, FP16, and floating-point reductions are future ISA classes.

Before RTL implementation, each instruction group must define exact C++ model
semantics for:

- signed and unsigned interpretation;
- overflow wrap versus saturation;
- narrowing and widening behavior;
- rounding mode and tie handling;
- inactive predicate lane behavior;
- reduction identity values;
- exception/fault conditions;
- whether destination inactive lanes are preserved, zeroed, or left
  architecturally unchanged.

## Matrix Instructions

Matrix instructions control the existing INT8 matrix engine as a resource:

- configure tile dimensions and edge masks;
- identify local A/B tile locations;
- issue matrix compute;
- clear, accumulate, or store INT32 tile results;
- synchronize on matrix completion or fault.

The matrix ISA must not require the frontend to expose V1 GEMM descriptor
fields directly. V2 matrix kernels are firmware programs over micro-ops.

Matrix instructions must expose a tile-level contract:

- local A, B, and C/accumulator operands;
- active M/N/K dimensions;
- edge masks;
- accumulator clear and accumulate modes;
- INT32 wraparound arithmetic matching V1;
- completion event and fault result.

Internal systolic wavefront timing is not an architectural observable.

## DMA And Local Memory Instructions

DMA instructions move data between system memory and local scratchpad:

- system address;
- scratchpad address;
- byte count;
- stride or 2D shape where supported;
- direction;
- completion event;
- fault reporting.

Scratchpad/vector memory instructions move data between local memory, vector
registers, and matrix buffers. They must have explicit bounds/fault semantics so
tests can verify illegal local-memory access.

Ordering instructions:

- `wait` observes a named DMA, vector, or matrix event.
- `fence.local` orders prior local vector/matrix writes before later local
  reads.
- `fence.dma` orders prior local writes before later DMA stores and orders prior
  DMA loads before later local reads after completion.
- System-memory visibility after DMA is defined by completion events plus
  platform cache-maintenance rules outside the NPU ISA.

## ISA Metadata Requirement

Before decoder RTL is implemented, the ISA table must be represented in a
machine-checkable form. The checker must verify:

- no duplicate encodings;
- no overlapping class patterns;
- all reserved patterns are named;
- each implemented instruction has a decode class, operand format, fault model,
  and required coverage point;
- each implemented instruction has executable C++ reference semantics;
- documentation is generated or byte-checked from the metadata.

The metadata can be integrated into the ABI generator later, but V2 decode must
not be maintained only as hand-written prose.
