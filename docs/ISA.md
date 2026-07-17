# HolonNPU ISA

This document defines the architectural semantics of Holon ISA 1.0. Exact
encodings, instruction metadata, semantic hooks, and coverage names are
generated in `docs/ISA_REFERENCE.md` from `spec/holon_npu_isa.json`.

## ISA Ownership

Holon owns the complete program ISA. Replaceable frontend implementations are
microarchitectures of this ISA; they cannot redefine program binaries or
software-visible behavior.

The ISA deliberately does not implement RVC or RVV binary compatibility.
Instruction space is allocated directly to frontend control, predicate, vector,
matrix, DMA, CSR/debug, synchronization, and system operations. The design uses
SVE-style explicit predication and VLA execution while retaining independent,
NPU-oriented encodings.

## Instruction Envelope

- Instruction width: 32 bits.
- Instruction alignment: 4 bytes.
- Bits `[31:28]`: major instruction class.
- Bits `[27:24]`: class-local opcode.
- Remaining fields are class-owned and schema-defined.
- Unknown classes, opcodes, malformed fields, nonzero reserved bits, and invalid
  branch targets produce precise `ILLEGAL_INSTRUCTION` faults.
- A faulting instruction does not retire and its PC is reported precisely.

Instruction classes are `FRONTEND_CONTROL`, `PREDICATE`, `VECTOR_CONFIG`,
`VECTOR_ALU`, `VECTOR_MEMORY`, `VECTOR_PERMUTE`, `VECTOR_REDUCTION`,
`QUANTIZATION`, `MATRIX`, `DMA`, `CSR_DEBUG`, `SYNC`, and `SYSTEM`.

## Frontend State And Control

The frontend has sixteen 32-bit scalar registers; `s0` is hardwired to zero.
Implemented control operations provide immediate materialization, modulo-2^32
add, local-memory word load/store, and PC-relative equality/inequality branches.
Taken branch targets must identify complete instructions in the loaded image.

Read-only CSRs expose current PC, retired instruction count, loaded program
size, and active local-memory size. Halt, resume, debug-step, IRQ, and software
reset are host lifecycle controls rather than program instructions.

## Predicate And Vector State

Vector configuration is architectural state and includes:

- active vector length `VL`;
- element width of 8, 16, or 32 bits;
- signed or unsigned interpretation;
- round-to-nearest ties-to-even mode;
- saturation enable;
- explicit predicate register selection.

`VL` must be in `[1, max_vl]`. `predicate.ptrue` activates lanes below `VL`;
`predicate.load` imports a bit-packed predicate from aligned scratchpad memory.
Lanes at or above `VL` are always inactive.

For predicated operations, inactive destination lanes are preserved unless an
instruction explicitly defines another result. Predicated stores perform no
write for inactive lanes. Tail lanes cannot access memory.

## Integer Arithmetic

The vector engine supports i8/u8/i16/u16/i32/u32 interpretations. Normal add and
subtract wrap at the selected element width unless saturation is enabled.
Comparisons honor configured signedness. Shift counts and results are confined
to the selected width; arithmetic right shift requires signed interpretation.

Implemented classes include add/sub/min/max, equality and less-than compare,
logical and arithmetic shifts, select, gather, zip/unzip, transpose helpers,
sum/min/max reductions, and vector scratchpad load/store.

Reduction identity values are:

- sum: zero;
- signed/unsigned min: greatest representable selected value;
- signed/unsigned max: least representable selected value.

Only destination lane zero receives a reduction result; other destination lanes
retain their previous values unless the generated reference states otherwise.

## Quantization

`quantization.requantize` reads a schema-defined command block from local memory
and applies, per active lane:

1. signed or unsigned source interpretation;
2. fixed-point multiplication;
3. right shift with round-to-nearest, ties-to-even;
4. output zero-point addition;
5. inclusive clamp;
6. canonicalization to configured width and signedness.

The command block contains multiplier, shift, zero point, clamp minimum,
clamp maximum, and a required-zero reserved word. Invalid alignment, bounds,
shift, or range values fault without partial architectural update.

## Matrix Operations

`matrix.gemm` points to a 32-byte aligned local command record. The record
defines A/B/C local addresses, row strides, active M/N/K up to 16, and
clear/accumulate/store flags. The instruction issues one synchronous INT8 tile
operation to the B-weight-stationary matrix resource.

Architectural accumulation is signed INT8 multiply into INT32 with
two's-complement wraparound. Firmware owns multi-tile traversal and accumulator
lifetime. Internal PE placement and wavefront cycle timing are not visible to
software.

## DMA And Ordering

`dma.load` and `dma.store` transfer 1 through 4096 aligned 32-bit words between a
64-bit system address held in scalar registers and a local scratchpad address.
The instruction retires only after completion or faults precisely on invalid
requests, local bounds, or AXI responses.

`sync.wait_dma`, `sync.fence.local`, and `sync.fence.dma` are explicit ordering
points. They may complete immediately in the current single-command in-order
implementation, but their architectural meaning must remain valid when engine
concurrency increases.

## System Operations

`system.exit` terminates successfully.

`system.fault` has no operand. Its immediate/reserved field must be zero and it
always terminates with `EXPLICIT_PROGRAM_FAULT`. A nonzero value is an illegal
instruction, not a software-selected ABI fault code.

## Memory And Fault Precision

Program memory is read-only to the frontend after loading. Data scratchpad is
little-endian and explicitly managed. All local accesses are bounds checked;
engine faults are returned through issue/result channels.

An instruction retires exactly once after all its architectural effects are
complete. Faulting instructions do not retire. Engine issue remains stable under
backpressure, and the frontend waits for the associated result before advancing
PC.

## Metadata Discipline

`spec/holon_npu_isa.json` is the sole authority for:

- ISA version and class allocation;
- opcodes, formats, field constraints, and reserved regions;
- operation-class numbering consumed by ABI capability generation;
- encoder constants and generated declarations;
- reference semantic hooks and required coverage event names.

Changes must pass `tools/gen_isa.py --check` and `tools/check_isa.py`, which
reject overlaps, incomplete metadata, and invalid references.

## Deliberate Limits

- Integer and quantized operations only.
- No floating-point, BF16, or FP8.
- No RISC-V, RVC, or RVV binary compatibility.
- No program-visible dependence on engine cycle timing.
