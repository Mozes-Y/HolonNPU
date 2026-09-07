# HolonNPU ISA Redesign

Status: active architecture review under ADR-0059. This records the accepted
direction and remaining contract work, not features of the current decoder.
`docs/ISA.md` and generated metadata still describe the verified ISA 1.0
migration baseline. Scalar scope, M-mode execution, and instruction widths are
confirmed. ADR-0062 defines machine CSR/trap behavior; NPU operand allocation,
program startup and the integrated execution contract remain.

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
are defined below under ADR-0062. Program termination still requires a contract. WFI is not a
program-exit instruction. Decode recognition alone does not implement traps.

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
ADR-0063 implements physical routing; integrated ELF startup and new Holon
operand formats remain subsequent steps. Tests
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

## Vector Contract To Freeze

1. Register/state model: independently addressable vector and predicate banks;
   defined bit capacity, element views, alias behavior, and discoverable limits.
   Register capacity and physical execution lanes are different concepts.
2. VLA control: set/request length and return the actual length to scalar code.
   Define zero length, tail iteration, narrowing/widening capacity, and how
   multi-width instructions choose a legal active length. Do not reproduce RVV
   register-group restrictions merely to fit the current encoding.
3. Predication: predicate construction/comparison/Boolean operations, predicate
   memory representation, merge versus zero policy, and reduction identities.
   Inactive lanes must not access memory or generate numeric exceptions.
4. Operand formats: sufficient register and predicate fields; independent
   immediates; scalar base/displacement, stride, and indexed addressing. Do not
   make every instruction fit one overloaded three-register format.
5. Arithmetic: signed/unsigned integer operations, widening/narrowing, explicit
   saturation, shifts, conversion/quantization, FP32 operations, and reductions.
   Define rounding, overflow, NaNs, signed zero, subnormals, reduction order, and
   aliasing before implementing numerical helpers.
6. Data movement: contiguous/strided/indexed loads/stores, gather/scatter,
   broadcast, select, slide, transpose and packing primitives. Fault and partial
   write rules must be explicit for masked and overlapping accesses.
7. Dependency rules: specify when vector results become visible, what scalar
   code can observe, and which fences/events are required. Physical latency
   cannot become part of program correctness.

These are contract work items. Opcode count, register count, bit layout, and
which helpers are initial versus deferred must be selected using the full
Transformer program, not frozen by this checklist.

## Matrix Contract To Freeze

- Separate tile views/configuration, loaded operands, and accumulator state.
  Use scalar-register addresses and explicit strides/shapes rather than a
  mandatory immediate-addressed command record.
- Expose load, matrix multiply-accumulate, accumulator clear/accumulate,
  conversion, and store as independently schedulable operations.
- Define logical M/N/K, transpose/layout, input and accumulation types, valid
  edge extents, and accumulator ownership. Physical ARRAY_K/N and weight-load
  wavefront timing remain implementation details.
- Define interactions with vector/local-memory results, including safe
  vector post-processing of accumulators without a Host round trip.
- Define synchronous completion and any asynchronous event/fault semantics
  together. Event lifetime, issuer PC, wait/fence visibility, resource hazards,
  reset drain, and errors cannot be left to implementation accident.
- INT8/INT32 behavior is retained as reusable semantics, not as a restriction
  on the new matrix operand contract. FP32 is the initial numerical reference
  for Transformer correctness; future BF16/FP8 require separate evidence.

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
not as a claim of measured performance superiority. NPU opcode/operand
allocations and relocation behavior must still be frozen before execution.
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
