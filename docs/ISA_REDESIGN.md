# HolonNPU ISA Redesign

Status: active architecture review under ADR-0059. This records the accepted
direction and remaining contract work, not features of the current decoder.
`docs/ISA.md` and generated metadata still describe the verified ISA 1.0
migration baseline. Scalar scope, M-mode execution, and instruction widths are
confirmed. ADR-0062 defines machine CSR/trap behavior; ADR-0065 selects the
NPU operand/state contract below. Its execution and integration remain work,
not capabilities implied by successful metadata decoding.

## Invariants

- Scalar control is RV32-compatible, including instruction behavior, register
  conventions, and toolchain calling convention. A separate Holon scalar
  compiler backend is not the destination.
- Vector execution remains vector-length agnostic. Program correctness cannot
  depend on physical lane count, pipeline depth, or a hardcoded VL of 16.
- Predicates are explicit architectural operands with deterministic inactive
  and tail behavior. Do not reintroduce a single implicit mask register.
- Holon owns vector/matrix instruction formats; there is no RVV binary
  compatibility requirement, LMUL encoding dependency, or mandatory RVC support.
- Space freed by rejecting RVC is used for first-class NPU instructions, not
  left unused to preserve a compatibility mode that does not exist.
- Frontend implementations are interchangeable implementations of this same
  contract. Changing the frontend does not change program semantics.
- One semantic implementation serves functional execution and gem5. New RTL
  remains behind the simulator-first review gate.

## Why The Current Format Must Change

The existing class/opcode/register fields are four bits each. Its limitations
are architectural, not missing arithmetic functions:

| Current limitation | Required replacement |
| ------------------ | -------------------- |
| Custom scalar ALU/branch encoding and 16 scalar registers | Standard RV32 control instruction semantics and ABI |
| Only p0 is accepted | Independent predicate register selection and predicate operations |
| 12-bit immediate local vector address | Scalar base plus displacement/stride/index operand forms |
| VL/configuration and single width state coupled to a minimal ALU | Explicit VLA length selection, operand type/conversion rules, and operation-specific arithmetic modes |
| Only contiguous loads/stores and a few fixed shuffles | Unit-stride, strided, indexed memory and general permutation contracts |
| Matrix command fetched from an immediate-addressed 32-byte block | Register-addressed tile views, shape/stride state, explicit accumulator lifetime, and load/compute/store operations |
| A matrix instruction means one INT8 16x16-limited implementation command | Logical tile operations with advertised limits, independent of physical PE geometry |
| Extending function codes leaves these constraints intact | Redesign operands, state, memory, faults, and completion together |

## Scalar And Toolchain Boundary

Confirmed baseline: RV32IM plus Zicsr, ILP32, no C extension. Initial Holon
vector/matrix access uses explicit intrinsics/assembly; compiler automatic
vectorization is not part of this baseline. Additional standard extensions
require a separately justified contract change. Build control code with
`-march=rv32im_zicsr -mabi=ilp32`, using C23 or C++26 in freestanding mode.

Scalar instructions retain standard encodings and all 32 integer registers;
`x0` is hardwired zero. The integer calling convention includes 16-byte stack
alignment and standard argument, return, saved-register, `gp`, and `tp` rules.
ILP32 is not ILP32F/ILP32D: scalar hardware floating-point and hosted C/C++
libraries are not promised by this choice. See the
[RV32I specification](https://docs.riscv.org/reference/isa/v20260120/unpriv/rv32.html)
and [RISC-V psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/).

The functional implementation must cover all selected scalar instructions, not
only those emitted by one smoke program. Include x0 semantics, sign extension,
overflow, multiply/divide edge cases, branches, JAL/JALR, aligned instruction
fetch, scalar load/store widths, FENCE ordering, ECALL/EBREAK, and all six CSR
instruction forms. Zicsr does not itself select a privileged execution
environment or require every standard CSR. Define supported addresses,
permissions, read/write suppression and side effects, traps, and termination
before accepting ELF programs. See the
[Zicsr specification](https://docs.riscv.org/reference/isa/v20260120/unpriv/zicsr.html).

The execution environment is confirmed as single-hart M-mode bare metal, with
standard traps, CSRs, MRET, and WFI; no U/S mode, MMU, or OS is introduced.
Machine-mode CSR inventory, reset values, interrupt sources and trap priority
are defined below under ADR-0062. ADR-0065 selects explicit STOP for program
termination. WFI is not program exit. Decode recognition alone does not
implement traps or program termination.

Upstream GCC/LLVM are the intended scalar toolchains. A freestanding ILP32 runtime
must define startup, stack/global-pointer initialization, code/rodata/data/BSS
placement, calls/returns, and program termination. The memory map must make
compiler-generated scalar accesses valid while preserving explicit tensor
scratchpad/DMA management. No RISC-V Host CPU launches the Holon program.

The selected address model is a unified 32-bit physical address space. Scalar
loads/stores may access mapped scratchpad and system memory; they are not
restricted to scratchpad. Tensor bulk transfers remain explicit DMA operations.
Core-owned local memory and environment-owned system memory remain separate
storage owners behind that address map. ADR-0063 defines checked regions,
permissions and scalar-access faults. Region placement is a boot/board input;
integrated scalar/DMA ordering must be preserved at machine cutover. This is a
target change, not a claim that the released accelerator permits scalar system
memory accesses.

Custom vector/matrix instructions initially enter through explicit intrinsics
or assembly kernels with a documented calling/clobber convention. That is not
a promise of RVV auto-vectorization, stock disassembly, or automatic register
allocation for Holon vector registers. A compiler backend is a separate feature
after the ISA and kernel ABI are stable.

The toolchain gate must inspect ELF class, machine, attributes, relocations,
instruction bytes, and the absence of RVC. Merely supplying `-march` is not
proof. Raw custom instruction emission must survive assembly/linking unchanged;
linker relaxation and disassembly of Holon encodings need explicit tests.
An ELF RVC flag check alone cannot prove that every linked object is free of
compressed instructions. Inspect the scalar code and input attributes too.
The assembler's `.option norvc` disables compressed emission, while numeric
instruction emission still obeys its instruction-length checks. A raw-word
carrier is not stock disassembler support. See the
[GNU assembler directives](https://sourceware.org/binutils/docs/as/RISC_002dV_002dDirectives.html).

## Scalar Effects Contract

ADR-0061 implements a pure scalar evaluation layer inside the existing semantic
library, not a second interpreter or an independently bootable ISA mode. It
consumes a standard scalar word, its PC and captured source values; x0 sources
always read zero. It returns either a register/next-PC update, a typed memory,
CSR, fence or machine-control request, or a synchronous trap. Evaluation does
not mutate a machine, retire an instruction, perform I/O or account for cycles.

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

The existing machine consumes shared scalar arithmetic during migration.
Shared 32-register state and trap entry/return are implemented by ADR-0062.
ADR-0063/0064 implement physical routing and ELF loading; integrated startup
and execution of the ADR-0065 operand contracts remain subsequent steps. Tests
must distinguish an evaluated request from its successful architectural commit.
The behavior follows the [RV32I specification](https://docs.riscv.org/reference/isa/v20260120/unpriv/rv32.html),
[M extension](https://docs.riscv.org/reference/isa/v20260120/unpriv/m-st-ext.html), and
[Zicsr specification](https://docs.riscv.org/reference/isa/v20260120/unpriv/zicsr.html).

## ELF Loading Slice (ADR-0064)

`elf::image` validates static RV32 ELF32 executables and transactionally loads
PT_LOAD bytes/BSS through the checked physical map. Supported arch versions and
stack alignment come from `semantic_frontend.elf_profile` in the canonical ISA
schema. The runtime decoder still rejects unsupported instructions regardless
of ELF attributes. Actual C23/C++26 images execute through the shared hart in
component tests; full mixed-width program-machine boot remains an integration
step, not a second permanent execution mode.

## Selected NPU Contract (ADR-0065)

This is the semantic target, not an advertised capability of current RTL.
`semantic_npu` in the canonical ISA schema owns opcode/operand allocation;
generated metadata drives typed encoding, decoding and disassembly. Low bits
`00` select vector/predicate, `01` matrix, and `10` DMA/system. Bits 11:2 are
the ten-bit family opcode. Each opcode uses its own operand fields above bit 11;
every unused bit is reserved-zero. Undefined opcodes, modes and types are illegal.
The all-zero instruction is unallocated and illegal, not an implicit no-op.
The schema is authoritative for exact bit positions; register domains are not
interchangeable despite equal field widths. Initial emitted custom words use
two little-endian `.word` directives with relaxation and RVC disabled.

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
  checked against discoverable implementation capacities, not ARRAY_K/N.
- VIEW, LOAD, CLEAR, DOT, MACC and STORE are independently schedulable commands.
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
  internal two-phase token protocol remains essential for gem5 timing.
- Validate local ranges before DMA issue. A bus fault may leave accepted store
  bytes externally visible, but an unsuccessful load never commits partial SPM
  data. Standard RV32 FENCE waits for prior effects; there is no redundant Holon
  FENCE opcode in this blocking baseline. Reset drains accepted work. STOP retires
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

### Workload Mapping And Acceptance

QKV, attention scores/value aggregation and FFN use VIEW/LOAD/DOT/MACC/STORE.
Residuals and normalization use arithmetic/reductions/broadcast; causal masks
use comparisons and predicate operations. Guest softmax/activation uses those
same primitives. Tile traversal and VLA loops use RV32 scalar code. Instruction
metadata/codec tests must cover every opcode/domain/reserved bit and raw link
integrity. Subsequent semantic tests must prove these arithmetic and ordering
rules before any Transformer or gem5 performance claim.

The design adopts explicit predication/VLA lessons from
[Arm SVE](https://developer.arm.com/community/arm-research/b/articles/posts/the-arm-scalable-vector-extension-sve)
and independent mask operands from
[Intel AVX-512](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-avx-512-instructions.html),
without claiming binary compatibility or their exact numerical contracts.

## Execution Evidence Still Required

| Domain | Acceptance beyond encoding |
| --- | --- |
| Vector/predicate | Multiple capacities, mixed widths, independent masks/tails, alias snapshots, no inactive accesses, all numeric boundaries and rounding modes. |
| Memory | Contiguous/strided/indexed and packed predicates, overflow/alignment, repeated stores, all-or-nothing local validation, precise bus faults. |
| Matrix | View/register independence, zero/edge shapes, transposition, aliasing, integer wrap and ordered f32 FMA, local vector post-processing. |
| Program | Guest VLA/tile loops, trap recovery, explicit stop and complete Transformer numerical comparisons without Host arithmetic. |

Encoding tests alone do not satisfy these execution gates. BF16/FP8 remain
separate exploration, and no physical lane/PE timing becomes a binary contract.

## Encoding Review

Confirmed envelope: standard RV32 scalar instructions are 32 bits; Holon NPU
instructions, including vector/matrix/DMA, are fixed 64 bits. Reclaimed non-RVC
prefixes identify Holon formats. Standard RV32 scalar words retain their meanings;
the extension is not RVC and does not follow its 16-bit length interpretation.
Scalar Zicsr instructions remain standard 32-bit instructions.

The selection compares two approaches:

| Candidate | Benefit | Cost to measure |
| --------- | ------- | --------------- |
| Selected: 32-bit scalar plus fixed 64-bit NPU instructions | More operand/predicate/immediate space, simple class-specific forms | NPU code footprint, fetch bandwidth, alignment, toolchain raw emission |
| Not selected: 32-bit scalar/NPU base with explicit extension words | Smaller common instructions and larger optional operands | More decode/length cases, register/immediate restrictions, tooling and fault complexity |

Fixed 64-bit NPU forms are selected for expressive, predictable operand formats,
not as a claim of measured performance superiority. ADR-0065 selects the
opcode/operand allocation; raw-byte link integrity is tested independently.
Holon needs length-aware decoding/disassembly; upstream tools remain responsible
for the standard scalar portion and carrying explicitly emitted custom bytes.

The first executable frontend contract (ADR-0060) fixes little-endian byte order
and four-byte instruction alignment. A word with low bits `11` occupies four
bytes and must decode as a selected standard scalar instruction. Other low-bit
prefixes (`00`, `01`, `10`) occupy eight bytes; their NPU opcode allocations are
not implied by successful framing. No compressed instruction is executed.
A 64-bit instruction may start at an address congruent to four modulo eight;
no alignment padding is required between scalar and NPU instructions.
Fetch rejects misaligned PCs, incomplete frames, and wrapping image ranges.
It determines length at the requested PC, without a hidden instruction-start
bitmap. Program labels must identify intended instruction starts. Unsupported
standard extensions, including standard long-instruction prefixes, never fall
through to the Holon decoder.

Keeping the current 12-bit address format solely to avoid changing the decoder
is not a selection criterion. Measure program size and fetch cost before RTL
approval. The chosen format must cover real Transformer kernels without
out-of-band arithmetic or repeated configuration just to overcome field limits.

## Implementation Order And Evidence

### M-Mode State Contract (ADR-0062)

The shared hart commits one evaluated scalar instruction at a time. Its API
does not fetch programs, own system memory or calculate latency. Loads, stores
and fences return stable tokenized requests; invalid completions preserve all
state. Access failures trap at the issuing PC and do not retire. Reset clears
state but never reuses completion tokens; it is an external-reset operation,
not a substitute for software-reset drain.

Machine CSR inventory/reset values and writable masks come from the internal
ISA metadata. MSTATUS implements MIE/MPIE and fixed MPP=M; MISA advertises RV32IM
only during scalar migration. MTVEC supports direct/vectored modes (reserved
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

### Sequence

1. Scalar extension/ABI scope and 32/64-bit framing are confirmed. Implement
   framing and standard scalar decode independently of the remaining NPU
   operands, memory map, and detailed trap contract. Freeze each remaining
   contract before implementing its execution semantics; record decisions here
   and in ADRs.
2. Extend the single canonical ISA/ABI metadata and generators. Distinguish
   semantic-stage features from capabilities of existing RTL; reject overlap,
   truncation, unsupported modes, stale generated output, and toolchain drift.
3. Implement RV32 semantic execution and ELF startup. Run real C23/C++26 control
   programs from the upstream toolchain and directed ISA edge tests.
4. Implement the redesigned vector/predicate and matrix contracts in the same
   semantic core. Test multiple vector capacities/physical-lane configurations,
   masks/tails, indexed memory, shapes, numeric edges, aliasing and faults.
5. Compile and run one complete minimal Transformer program: embedding/position,
   QKV, scaled attention/mask/softmax, value aggregation, output projection,
   residual/normalization, feed-forward activation, and output logits. Compare
   stages against an independent mathematical reference; never service missing
   operators in the test harness.
6. Implement the autonomous gem5 execution system for the same image and core.
   Measure frontend, memory, vector/matrix and synchronization on its event
   timeline. Remove superseded Host-only paths after validating replacement.
7. Review workload/performance/cost evidence before any new RTL implementation.

Each completed implementation feature has tests, current-state documentation,
and an immediate separate commit. This review does not authorize a second
permanent ISA implementation or a second performance model.
