# HolonNPU V2 ISA

This document defines the hand-written semantic contract for the Holon V2 NPU
instruction set. Machine-checkable metadata owns concrete instruction classes,
encodings, operand formats, faults, semantic hooks, and required coverage.

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

Instruction classes:

| Class | Purpose |
| ----- | ------- |
| Frontend control | Integer ALU, branch, loop, address generation, lightweight control. |
| Predicate | Predicate register initialization and bit-packed mask load. |
| Vector config | Set vector length, element width, signedness, rounding, and saturation mode. |
| Vector ALU | Elementwise integer arithmetic, compare, select, and shifts. |
| Vector memory | Contiguous scratchpad vector load/store. |
| Vector permute | Lane shuffle, pack/unpack, transpose helper operations. |
| Vector reduction | Sum and min/max reductions. |
| Quantization | Fixed-point requantization with explicit zero point and clamp bounds. |
| Matrix | Tile-level GEMM clear/accumulate/store issue for the reused INT8 engine. |
| DMA | System-memory to scratchpad transfers and scratchpad to system-memory transfers. |
| CSR/debug | Read-only frontend PC, retired count, image size, and local-memory size. |
| Sync | DMA wait and local/DMA ordering fences. |
| System | Program exit and explicit program fault. |

The final encoding table must leave clearly named reserved class space for
future BF16, FP8, multi-engine, and system-level extensions. Reserved space must
be intentional and documented; it must not be an artifact of trying to remain
compatible with RVV or RVC.

## Frontend Control

The reference frontend exposes sixteen 32-bit scalar registers; `s0` is
hardwired to zero. `movi`, `add`, and `addi` provide modulo-2^32 address and
loop arithmetic. Aligned `load` and `store` access 32-bit little-endian words in
data scratchpad. `beq` and `bne` use signed instruction-scaled PC-relative
offsets and fault when a taken target is outside the loaded program image.
Reserved operand fields must be zero, local-memory faults are precise, and a
faulting instruction does not retire.

## CSR And Debug State

`csr_debug.read` copies one read-only architectural value into a scalar
register. The implemented selectors are current PC, retired-instruction low and
high words, loaded program size, and active local-memory allocation. Unknown
selectors are illegal instructions and do not retire. Lifecycle halt, resume,
debug-step, IRQ policy, and fault snapshots remain host control-plane behavior;
they are not hidden system opcodes.

## Predicate And Mask Model

The predicate model is explicit and VLA-safe:

- Predicate registers represent active vector lanes as bit-packed state.
- `predicate.ptrue` activates lanes `[0, VL)` and clears lanes at or above VL.
- `predicate.load` loads one aligned 32-bit predicate word from data scratchpad.
- Vector ALU instructions name a predicate in immediate bits `[3:0]`; vector
  memory instructions name a predicate in `rs1`.
- Inactive lanes do not update destination elements unless the instruction
  explicitly requests a merge behavior.
- Inactive vector loads preserve destination register lanes and inactive
  vector stores perform no local-memory write.
- The current implementation exposes one predicate register, `p0`; the field
  width and capability register permit later implementations to expose more.
- Predicate-generating comparisons and predicate boolean operations remain
  planned extensions.
- Reductions define whether inactive lanes contribute identity values or are
  ignored.
- Matrix and DMA instructions use masks for edge tiles and bounds checking.

This should feel closer to SVE's clean predication than RVV's compatibility
constraints.

## Vector-Length-Agnostic Execution

V2 keeps the useful VLA idea:

- software discovers maximum VL through capability registers;
- software configures a nonzero VL no greater than that maximum;
- loops are written against the configured active length;
- tail handling is expressed through predicates and vector length state.

The current `vector_config.set` has no result register: an unsupported VL or
element configuration faults precisely instead of silently shortening VL.

V2 does not inherit RVV's encoding pressure. Vector config instructions should
have enough field space to express:

- element width;
- signedness when architecturally visible;
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
- explicit fixed-point narrowing through requantization.

Required operation groups:

- add/sub/min/max;
- compare and select;
- logical and arithmetic shifts;
- saturating add/sub;
- requantization;
- pack/unpack;
- transpose/tile movement;
- reductions.

BF16, FP8, FP16, and floating-point reductions are future ISA classes.

The executable vector ISA uses type-orthogonal opcodes. A
`vector_config.set` instruction establishes `VL`, element width, signedness,
rounding, and saturation state; generic contiguous `vector_memory.load` /
`store` and `vector_alu` add/sub/min/max/equality/less-than/logical-shift-left/
logical-shift-right/arithmetic-shift-right instructions consume that state.
Signed and unsigned 8-, 16-, and 32-bit lanes are implemented in the C++
architectural simulator and vector RTL. Narrow stores use byte write strobes,
and add/sub either wrap or saturate according to vector configuration. The
implemented helper set includes select, gather, zip/unzip, 4x4 transpose,
sum/min/max reductions, and fixed-point requantization with nearest-even
rounding and explicit clamp bounds. Predicate register instructions,
inactive-lane preservation, and masked local stores are implemented in RTL and
the architectural simulator. The reference frontend retires an instruction
only after its engine result is accepted, so faults remain precise at the
issuing PC.

Each instruction group defines matching RTL and C++ semantics for:

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

The implemented `matrix.gemm` instruction points to a 32-byte command record in
data scratchpad. That record identifies local A/B/C addresses, byte strides,
active M/N/K dimensions, and clear/accumulate/store flags. Active dimensions
define the rectangular edge masks. The first implementation exposes accumulator
ID zero; other IDs fault until advertised by a future capability revision. The
instruction retires only after the matrix result handshake or faults precisely.

The matrix ISA must not require the frontend to expose V1 GEMM descriptor
fields directly. V2 matrix kernels are firmware programs over micro-ops.

Matrix instructions must expose a tile-level contract:

- local A, B, and C/accumulator operands;
- active M/N/K dimensions;
- edge behavior derived from active M/N/K;
- accumulator clear and accumulate modes;
- INT32 wraparound arithmetic matching V1;
- synchronous completion/fault result.

Internal systolic wavefront timing is not an architectural observable.

## DMA And Local Memory Instructions

DMA instructions move aligned 32-bit words between system memory and local
scratchpad. Two scalar registers provide the full 64-bit system address, a
third provides the 32-bit local byte address, and the 12-bit count-minus-one
field transfers 1 to 4096 words. Strided/2D DMA and asynchronous event IDs are
not part of ISA 1.0.

Scratchpad/vector memory instructions move data between local memory, vector
registers, and matrix buffers. They must have explicit bounds/fault semantics so
tests can verify illegal local-memory access.

The first executable DMA instructions are `dma.load` and `dma.store`, defined in
`spec/holon_npu_isa.json` and generated into `include/holon_npu_isa.h`,
`rtl/common/npu_isa_pkg.sv`, and `docs/V2_ISA_REFERENCE.md`.

Initial `dma.load` / `dma.store` RTL subset:

| Field | Bits | Meaning |
| ----- | ---- | ------- |
| Class | `[31:28]` | `DMA`. |
| Opcode | `[27:24]` | `DMA_LOAD` or `DMA_STORE`. |
| System address low register | `[23:20]` | Scalar register containing system address bits `[31:0]`. |
| System address high register | `[19:16]` | Scalar register containing system address bits `[63:32]`. |
| Local address register | `[15:12]` | Scalar register containing the scratchpad byte address. |
| Word count minus one | `[11:0]` | Transfer length from 1 to 4096 32-bit words. |

System-address or length alignment faults are DMA request faults. Local
scratchpad range failures are local-memory bounds faults. AXI read response
errors are AXI read faults; AXI write response errors are AXI write faults.

Ordering instructions:

- `sync.wait_dma` waits for prior frontend-issued DMA commands to complete. In
  the first in-order DMA implementation, each DMA instruction already retires
  only after completion, so the tile can acknowledge its explicit `sync_issue`
  handshake immediately.
- `sync.fence.local` orders prior local-memory writes before later local-memory
  reads or engine issue.
- `sync.fence.dma` orders prior local writes before later DMA stores and orders
  prior DMA loads before later local reads after completion.
- The current executable sync encodings require all operand and immediate
  fields to be zero. Non-zero fields are illegal instructions until an ISA
  revision assigns event or scope operands.
- System-memory visibility after DMA is defined by completion events plus
  platform cache-maintenance rules outside the NPU ISA.

## ISA Metadata Contract

The ISA table is represented in a machine-checkable form. The checker verifies:

- no duplicate encodings;
- no overlapping class patterns;
- all reserved patterns are named;
- each implemented instruction has a decode class, operand format, fault model,
  and required coverage point;
- each implemented instruction has executable C++ reference semantics;
- documentation is generated or byte-checked from the metadata.

The metadata can be integrated into the ABI generator later, but V2 decode must
not be maintained only as hand-written prose.
