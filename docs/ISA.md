# HolonNPU ISA

This is the current executable research baseline in HolonNPU 3.0, not the old
accelerator ISA and not a newly numbered product ISA/ABI. Exact allocation comes
from `spec/holon_npu_isa.json`; generated
[scalar](ISA_REFERENCE.md) and [NPU operand](NPU_OPERAND_REFERENCE.md) references
are not edited manually. Candidate architecture changes require explicit
contracts and tests before replacing this baseline.

## Envelope And Toolchain

Scalar control is RV32IM + Zicsr, ILP32, single-hart M-mode bare metal. There
are 32 integer registers, x0 reads zero, and the calling convention uses a
16-byte aligned stack. No RVC, RVV binary compatibility, U/S mode, MMU, scalar
F registers or hosted standard library is implied. Compile guest scalar code
with `-march=rv32im_zicsr -mabi=ilp32` and C23 or C++26 freestanding mode.

Instructions are little-endian and four-byte aligned. Low bits 11 select a
32-bit scalar instruction. Low bits 00/01/10 select fixed 64-bit Holon
vector/predicate, matrix and DMA/system instructions. Holon opcode is bits
11:2; fields above bit 11 are operation-specific and unused bits must be zero.
Unlisted opcodes, unsupported modes/types and the all-zero word are illegal.
A Holon instruction may start at four modulo eight; no padding is required.
Unknown standard long/extension encodings do not fall through to Holon decode.
No instruction-start bitmap is assumed; labels must identify instruction starts.

Scalar instructions use standard toolchain encodings. Custom instructions use
typed construction or explicit raw-word assembly; automatic Holon register
allocation/vectorization and stock disassembler support are not implemented.
Upstream assembler/linker tests check raw instruction bytes and ELF attributes,
not just the compiler command line.

## Scalar Effects

Evaluation returns a typed register, memory, CSR, fence, control or trap effect.
It does not perform I/O, retire instructions or select a latency. The hart and
program machine complete those effects; there is no alternate interpreter.

- Integer computation uses modulo-2^32 results and defined signed comparisons.
  Shifts mask register shift counts to five bits. All eight M operations include
  division by zero and signed overflow, without host-language undefined behavior.
- Taken branches and JAL/JALR check four-byte target alignment before producing
  any link-register update. Untaken branches do not fault on their unused target.
  JALR clears bit zero; PC-relative arithmetic wraps to XLEN. A target fetch
  access fault belongs to the next instruction, not the successful jump.
- Scalar effective addresses are 32-bit physical addresses, not SPM offsets.
  LB/LH/LW/LBU/LHU and SB/SH/SW are little-endian; halfword/word misalignment
  raises a contained machine exception before any memory access. The
  router checks mapping/permissions and returns load/store access faults.
  Load-to-x0 still accesses memory and may fault. Store bytes are captured at
  issue; a failed load produces no register update. A payload with the wrong
  length is a simulator API error, not an architectural trap.
- Zicsr requests retain separate read and write enables. CSRRS/CSRRC with a
  nonzero source register containing zero still request a write. Immediate
  zero suppresses writes only for CSRRSI/CSRRCI. CSR permissions, WARL updates
  and atomic commit are implemented by the shared M-mode hart state.
- FENCE preserves predecessor/successor sets and ignores reserved mode/rd/rs1
  fields as RV32I specifies. It requires an ordering completion, not an early
  retirement. ECALL/EBREAK produce machine-call/breakpoint traps; MRET and WFI
  produce distinct machine-control requests, never a fake successful exit.

## M-Mode State

The shared hart commits one evaluated scalar instruction at a time. Its API
does not fetch programs, own system memory or calculate latency. Loads, stores
and fences return stable tokenized requests; invalid completions preserve all
state. Access failures trap at the issuing PC and do not retire. Reset clears
state but never reuses completion tokens; it is an external-reset operation,
not an implemented software-reset/quiesce protocol.

Machine CSR inventory/reset values and writable masks come from the internal
ISA metadata. MSTATUS implements MIE/MPIE and fixed MPP=M; MISA advertises RV32IM
in the current baseline. MTVEC supports direct/vectored modes (reserved
modes coerce to direct), MEPC clears bits 1:0, MIP reflects external interrupt
inputs. Machine identity values are zero. Unsupported HPM counters/selectors
are read-only zero fields; unsupported CSR addresses are illegal. No U/S,
delegation, PMP, Zicntr aliases or MMU are advertised.

Trap entry saves PC/cause/value, copies MIE to MPIE and clears MIE. MRET restores
MIE from MPIE, sets MPIE and returns to MEPC. Interrupt priority is machine
external, software, timer; vectored offsets apply only to interrupts. Pending
memory/fence effects must complete before an interrupt can be taken. WFI retires
once and waits; a locally enabled pending interrupt wakes it even with global
MIE clear, but trap entry still requires global MIE.

MCYCLE is supplied elapsed cycles by the execution environment. MINSTRET counts
successful commits unless inhibited; simulator retirement remains independently
monotonic for budgets. Counter CSR writes replace the selected half after the
writing instruction completes, suppressing its implicit increment when writing
MINSTRET. MCOUNTINHIBIT changes apply after the writing instruction retires.
CSR reads sample the pre-instruction value. ECALL/EBREAK enter standard traps,
never successful program exit. These choices follow the
[RISC-V machine specification](https://docs.riscv.org/reference/isa/priv/machine.html)
and [Zicsr](https://docs.riscv.org/reference/isa/v20260120/unpriv/zicsr.html).

## Addressing And Boot

One 32-bit physical map routes program memory, scratchpad and external memory.
Scalar loads/stores can access mapped local and system memory; vector/matrix
memory accesses target scratchpad. Tensor bulk transfers use explicit DMA.
Regions have permissions and disjoint checked ranges; overflow or unavailable
backing storage is not silently redirected to another owner.

The ELF loader validates little-endian RV32 ILP32 ET_EXEC, PT_LOAD permissions,
ranges, alignment, supported architecture attributes, initialized bytes and
BSS before mutation. The `scalar.elf_profile` schema defines accepted base,
extensions and stack alignment. Unsupported instructions still trap even if
the ELF attributes passed validation. Guest startup initializes sp/gp and
terminates with Holon STOP; no Host CPU or descriptor is needed.

## NPU Semantics

### Vector And Predicate State

- There are 32 vector registers and 32 predicate registers, none special.
  Each vector contains implementation-selected VBYTES bytes; each predicate
  contains VBYTES lane-ordinal bits. VBYTES is a positive multiple of 16,
  independent of physical execution lanes. Reset clears all bits.
- VSETL writes `min(unsigned(xAVL), VBYTES/max(sizeof(type),sizeof(peer_type)))`
  to xRD. It changes no hidden VL/type state. Every subsequent operation names
  its xVL register explicitly. VL=0 performs no memory/numeric accesses;
  VL exceeding any operand's capacity faults before effects. Indexed accesses
  include their u32 index vector in the capacity bound.
- Element views are i8/u8/i16/u16/i32/u32/f32, explicitly encoded. Predicates
  address element ordinals, not byte positions, across width conversions.
  PTRUE/WHILELT construct masks; logic, count, first and packed load/store make
  every predicate accessible. WHILELT uses nonwrapping unsigned start+i<end.
  PFIRST returns 0xffffffff when no selected bit exists.
- Vector destinations select merging or zeroing of inactive lanes below VL.
  Bytes beyond VL*destination_width are zero. Predicate-producing instructions
  zero all unselected and tail bits. Sources and masks are captured before any
  destination write, including source/destination aliasing. No inactive lane
  performs arithmetic, accesses memory or faults.
- Unit-stride, signed scalar-stride and u32 indexed addressing use a full scalar
  base plus signed 20-bit displacement. Index scale is 1/2/4/8 bytes. Effective
  addresses are calculated without wrapping, must be element-aligned and within
  the mapped scratchpad. All active addresses are checked before local effects.
  Repeated store addresses resolve in increasing lane order, last lane wins.
  Packed predicate memory is LSB-first, ceil(VL/8) bytes; unused final bits are
  zero. Scalar accesses and explicit DMA provide system-memory interaction.

### Numeric And Data Movement Rules

- Integer add/sub/mul/shift wrap at destination width. Signedness comes from
  the type. Shift counts are masked by element_bits-1. Min/max and comparisons
  respect signedness. Bitwise operations act on exact element bits.
  VASHR requires a signed integer type; VSHR shifts zeros into the high bits.
  VMULH gives the high element-width half of the full double-width product,
  respecting signedness, and pairs with VMUL for fixed-point arithmetic.
  Integer widening uses CONVERT before arithmetic, without register groups.
- F32 add/sub/mul/div/sqrt/FMA use IEEE binary32, round-to-nearest ties-even,
  gradual underflow and canonical quiet NaN 0x7fc00000. FMA rounds once. No
  floating traps, sticky flags or implicit dependence on Host rounding state
  are architectural. MIN/MAX return the numeric operand for one NaN and the
  canonical NaN for two; min(-0,+0)=-0, max(-0,+0)=+0. Comparisons with NaN are
  false except NE. Integer-only bitwise/shifts reject f32.
- CONVERT explicitly names source/destination types and RNE/RTZ/RDN/RUP. Integer
  narrowing saturates; float-to-integer rounds then clamps, with NaN becoming
  zero. Integer-to-float and float-to-float round according to the selected mode.
  Integer-to-integer conversion ignores rounding but never reinterprets sign.
- Reductions start from an explicit scalar seed and visit active lanes in
  increasing order, returning scalar result bits. Integer sums extend lane
  values and wrap to 32 bits. Floating sums round at each binary32 addition.
  Empty reductions return the seed. Carrying the previous seed across chunks
  preserves numeric order independently of vector capacity.
- Broadcast/extract transfer scalar register bits without scalar F registers;
  integer extraction sign/zero extends. SELECT uses an explicit selection
  predicate (true selects va, false vb); EXTRACT requires index<VL and faults
  otherwise. Other vector operations have a governing
  predicate. PERMUTE uses u32 lane indices, zero for
  indices outside VL, and captures aliased sources before writes.
- Softmax/normalization/GELU or other activation approximations are guest
  programs built from these primitives, including arithmetic/conversion and
  permutation; no whole-operator or transcendental Host callbacks are added.
  The Transformer workload must state its approximation/error contract before
  numerical acceptance. BF16/FP8 remain separate exploration.

### Matrix State And Ordering

- Sixteen views hold a physical base, signed row/column byte strides, logical
  rows/columns and type captured from scalar registers. Eight matrix registers
  hold typed dense logical tiles, independently of those views. Changing a view
  never changes a loaded tile. A tile's rows, columns and element count are
  checked against discoverable implementation capacities, not physical array geometry.
- VIEW, LOAD, CLEAR, DOT, MACC and STORE are separate architectural operations.
  LOAD snapshots a view; STORE requires matching shape/type and writes a view.
  CLEAR creates a typed zero tile. DOT sets C=A*B; MACC adds to matching C.
  Transposition/subviews are expressed by strides and dimensions, not a command
  record in memory. Zero extents are legal; zero K gives zero for DOT and leaves
  C unchanged for MACC. All source tiles are captured, so destination aliasing
  is defined. Views themselves need no memory access until LOAD/STORE.
- Integer inputs of any supported integer type accumulate modulo 2^32 into
  i32/u32. F32 inputs accumulate into f32 with increasing-K binary32 FMA,
  seeded by +0 (DOT) or C (MACC). Mixed integer/f32 input pairs are illegal;
  conversions use vector/local-memory operations. LOAD/STORE validate all
  active local addresses before effects, and repeated stores use row-major
  order. Vector post-processing consumes stored accumulators in SPM, no Host.
- CAPS returns VBYTES, matrix row/column limits or tile-byte capacity. These
  parameters must be supplied by the implementation and tested across sizes;
  they are not performance promises or frozen physical resource dimensions.
- DMA LOAD/STORE use captured full physical source/destination and byte count
  registers, one SPM and one external endpoint. Zero length has no access.
  Commands, vector/matrix operations and fences are architecturally blocking:
  retire at successful completion, faults retain issuing PC. No asynchronous
  architected event tokens are introduced by the first implementation. The
  internal two-phase token protocol belongs to the execution environment.
- Validate local ranges before DMA issue. A bus fault may leave accepted store
  bytes externally visible, but an unsuccessful load never commits partial SPM
  data. Standard RV32 FENCE waits for prior effects; there is no redundant Holon
  FENCE opcode in this blocking baseline. Environment reset invalidates pending\n  work and tokens; it is not a software quiesce protocol. STOP retires
  once, then returns its scalar status to the environment; nonzero status is
  a program result, not a substituted trap. WFI remains only architectural wait.

Decode errors raise standard illegal-instruction trap 2. Invalid VL, extract
index, tile/view capacity, undefined tile state or incompatible loaded shapes
raise Holon invalid-operand trap 24. Both report the low 32 instruction bits in
MTVAL and preserve the issuing MEPC. This uses the RISC-V custom exception
range, not a reassignment of a standard cause; see the
[machine specification](https://docs.riscv.org/reference/isa/priv/machine.html).
Local/data address alignment/access faults use standard load/store causes
4/5/6/7; MTVAL is the offending address (low 32 bits for arithmetic overflow).
Legality precedes dynamic operand checks, which precede addresses in lane or
row-major order. Memory response errors occur only after issue. No NPU fault
retires the instruction or silently completes STOP. Trap handling, rather than
a Host callback, determines whether guest code recovers or stops.

## Numerical And Architecture Changes

Programs must remain correct across advertised vector capacities, independent
of physical lane/PE count. Explicit predicates, inactive-lane safety, ordered
floating arithmetic and precise faults are baseline contracts. A faster
schedule cannot silently reassociate floating reductions or alter visibility.

Shared tile storage, explicit task dependencies, asynchronous commands, block
commit and new numerical formats are research candidates, not current ISA.
See [Simulation](SIMULATION.md) and [research](research/README.md).
