# HolonNPU Decision Log

This file retains only decisions that constrain the current product. Superseded
implementation history is available from Git and release notes; it is not an
alternative architecture contract.

## ADR-0044: Canonical Single Mainline Product

**Status:** Accepted

**Decision:** `master` contains one programmable NPU architecture. Product
modules, public symbols, schemas, targets, tests, and documentation use
canonical unversioned names. The former product implementation remains
available only through its release tag.

**Rationale:** Parallel version trees caused ambiguous ownership, unused product
modules, duplicated ABI generators, stale tests, and unclear release gates. A
hardware repository needs one reachable product graph and one current contract.

**Consequences:** This is an intentional source-level breaking cleanup. No old
module, target, namespace, constant, header, or descriptor compatibility alias
is retained.

## ADR-0045: AXI-Compliant Boundary Splitting And Safe Reset

**Status:** Accepted

**Decision:** Every AXI master splits bursts before a 4 KiB boundary. Software
reset enters observable `RESETTING`, blocks new work, drains accepted AXI and
local-memory transactions, then clears state and returns to `IDLE`. Only external
`aresetn` may abort an in-flight transaction immediately.

**Rationale:** ABI alignment does not guarantee page-safe AXI bursts, and
asserting internal reset on a software request can violate VALID stability and
lose accepted responses.

**Consequences:** Loader, DMA, completion writer, and arbitration logic expose
quiescence. Software reset returns on acceptance; `holon_npu_wait_idle()` waits
for completion. Native SVA proves burst bounds and handshake stability.

## ADR-0046: Fixed Explicit Program Fault

**Status:** Accepted

**Decision:** `SYSTEM_FAULT` has no operand and always produces
`EXPLICIT_PROGRAM_FAULT`. Its immediate is reserved and must be zero; a nonzero
encoding is `ILLEGAL_INSTRUCTION`.

**Rationale:** Allowing programs to inject arbitrary ABI fault values confuses
program intent with hardware fault provenance and weakens diagnostics.

**Consequences:** The encoder and program builder expose no fault argument. ISA
metadata, RTL, model, runtime, and tests share one semantic rule.

## ADR-0047: Evidence-Driven Coverage Gate

**Status:** Accepted

**Decision:** Functional coverage is recorded at the monitor or scoreboard point
where behavior is observed and verified. Coverage completion requires:

- every required typed C++ functional event;
- every named product RTL `cover property` with nonzero aggregate user count;
- an exact raw coverage file set for the current CTest run;
- line, branch, toggle, and expression results at or above a checked baseline.

Coverage artifacts are deleted before each run. FSM receives no threshold while
Verilator reports `0/0`.

**Rationale:** Declaring coverage points at test exit or reusing stale files can
report success without observing behavior. Structural percentages alone do not
prove protocol and architectural scenarios.

**Consequences:** `test_run::observe()` requires a verified event. CMake writes
the expected-test manifest for each run. `tools/check_coverage.py` merges and
gates all evidence.

## ADR-0048: Holon Owns The Complete Program ISA

**Status:** Accepted for the implemented ISA 1.0 baseline. ADR-0059 supersedes
the custom scalar encoding direction for the self-hosted target; independent
NPU ISA ownership and frontend implementation interchangeability remain.

**Decision:** Holon ISA is the complete software-visible program ISA. Frontend
implementations are replaceable microarchitectures, not alternate ISA owners.
RVC and RVV binary compatibility are rejected; the 32-bit encoding space is
allocated directly to NPU control, predicate, vector, matrix, DMA, CSR/debug,
sync, and system classes.

**Rationale:** Binding the NPU to extension encoding pressure would constrain
masking, vector length, matrix, DMA, and future class design without providing a
useful compatibility benefit.

**Consequences:** `spec/holon_npu_isa.json` owns encodings and operation-class
numbers. Any frontend replacement must execute identical binaries and match the
architectural model.

## ADR-0049: ABI And ISA Are Schema-Generated

**Status:** Accepted

**Decision:** `spec/holon_npu_abi.json` and `spec/holon_npu_isa.json` are the
only numeric sources of truth. The ABI generator reads both schemas so ISA
version and operation-class capability masks cannot drift.

Generated outputs include RTL packages, C23/C++ headers, and reference docs.
Tracked generated files are byte-compared in static checks.

**Rationale:** Manually synchronized RTL, C, C++, test, and documentation
constants fail as the interface grows.

**Consequences:** Generated files are never edited directly. Public contract
changes start in schema and require corresponding architecture/ADR review.

## ADR-0050: Interface-Native Reachable Product RTL

**Status:** Accepted

**Decision:** AXI-Lite, AXI4, valid-ready, frontend, and local-memory protocols
use SystemVerilog interfaces and modports inside product RTL. Flattened wrappers
exist only under `sim/rtl/`. Every implementation under `rtl/` must be reachable
from `npu_top` or be a leaf/shared IP instantiated by that graph.

**Rationale:** Protocol interfaces make ownership and assertions explicit;
simulation wrappers are useful for C++ access but are not architecture. Unused
product modules increase review and verification burden.

**Consequences:** Product cores cannot instantiate test wrappers or expose
test-only observation ports. Program results are observed through architectural
DMA store; module wrappers may use simulation-only hierarchical access.

## ADR-0051: Explicit Local Memory And Ordered DMA

**Status:** Accepted

**Decision:** The product uses local program memory and data scratchpad with
explicit DMA movement. It does not provide coherent caches. DMA and engine
instructions retire after their synchronous result, while explicit wait/fence
instructions preserve ordering semantics for future concurrency.

**Rationale:** Explicit movement is deterministic, synthesizable, and suitable
for an NPU tile. Keeping ordering in the ISA avoids coupling programs to the
current single-command implementation.

**Consequences:** Platform software owns physical allocation, address
translation, and cache maintenance. Future command queues must preserve current
wait/fence semantics.

## ADR-0052: Integer/Quant VLA Vector Architecture

**Status:** Accepted

**Decision:** Vector execution is vector-length agnostic with explicit predicate
registers, configured element width/signedness/rounding/saturation, and
preserved inactive lanes. The current data types are i8/u8/i16/u16/i32/u32.

**Rationale:** VLA predication provides portable tails and avoids fixed-width
program binaries. Type-orthogonal configuration preserves instruction space.

**Consequences:** Arithmetic and fault semantics must match the C++ model.
Floating-point and low-precision formats require a future contract rather than
dormant RTL.

## ADR-0053: B-Weight-Stationary Matrix Resource

**Status:** Accepted

**Decision:** The matrix resource uses a B-weight-stationary `ARRAY_K x ARRAY_N`
PE array, streamed A operands and partial sums, and INT32 wraparound
accumulation. Firmware issues tile micro-ops and owns multi-tile traversal.

**Rationale:** Stationary weights reduce B movement and retain the verified
INT8 systolic datapath while separating firmware-visible work from internal
wavefront timing.

**Consequences:** The ISA exposes local tile addresses, active dimensions,
accumulator identity, and clear/accumulate/store modes, not cycle timing.

## ADR-0054: C23, C++26, And Target-Centric CMake

**Status:** Accepted

**Decision:** Public C code targets C23; simulation/runtime code targets C++26;
CMake requires 4.0. Project constants use language constants rather than
project-defined behavior macros. CMake sources belong directly to targets,
public headers use file sets, and build creation is separate from CTest
registration.

Presets remain intentionally small: debug, regression, and coverage product
build trees plus one isolated gem5 build tree; debug, lint, regression,
coverage, and gem5 test entry points. The gem5 exception is justified by its
independent upstream source, toolchain, and build artifacts.

**Rationale:** Modern language and target-scoped contracts make requirements
transitive and reduce hidden directory state. Presets should pin workflows, not
duplicate every subsystem command.

**Consequences:** Single-target work uses `--target` and `ctest -R`; no
architecture-versioned build targets or subsystem preset explosion is allowed.

## ADR-0055: Completion Is Ordered Before Terminal Visibility

**Status:** Accepted

**Decision:** A requested completion record is fully acknowledged by AXI before
`DONE`, `FAULT`, or completion IRQ becomes visible. The record contains ABI
version, terminal status/fault, precise PC, cycles, and retired instructions.

**Rationale:** Host software must never observe terminal state while its
completion record is incomplete.

**Consequences:** Completion writeback participates in reset quiescence and AXI
write arbitration. A record at a page boundary is split into independently
acknowledged transactions.

## ADR-0056: Simulator-First Architecture Evolution

**Status:** Accepted

**Decision:** New architecture behavior is implemented first in one C++26 Holon
semantic core. A fast direct runner and an external gem5 SimObject consume that
same core. gem5 is the only performance and full-system model and uses a
cycle-accounted event model with a RISC-V Host. RTL requires a separate ADR that
accepts semantic, gem5, workload, cost, and verification evidence.

The gem5 integration tracks the upstream `stable` branch. Every result records
the exact commit, C++26 toolchain, model configuration, workload, and seed. The
complete gem5 binary, including upstream sources, is built in verified C++26
mode.

**Rationale:** A deterministic semantic core supports exhaustive and random
correctness testing, while gem5 supplies Host, memory-system, contention, and
performance context. Sharing semantics prevents drift without coupling the
architecture contract to gem5 events or duplicating a performance model.

**Consequences:** New ISA, ABI, engine, ordering, fault, capability, and
software-visible performance mechanisms cannot begin in RTL. Behavior-preserving
RTL fixes remain allowed against existing semantics. Upstream gem5
incompatibilities must be fixed explicitly without changing branch or lowering
the C++26 requirement.

## ADR-0057: Precise Semantic Protocol And Reproducible gem5 Foundation

**Status:** Accepted

**Decision:** Replace the synchronous `holon_npu::model::machine` directly with
`holon_npu::semantic::program_machine`, `device`, and `direct_runner`; no
compatibility facade is retained. The semantic core emits typed operations and
commits external effects only through matching strong completion tokens. System
memory belongs to the runner or gem5. System, local, instruction, and token
domains use distinct strong types without raw-integer compatibility overloads.
PC and retirement remain precise across pending work and faults.

gem5 is acquired from the official rolling `stable` branch into an isolated
build tree. One `gem5` preset builds the complete simulator and Holon EXTRAS in
C++26, audits compile commands, and records immutable build metadata. Normal
gem5 tests cover the functional device and RISC-V bare-metal path; locked Linux
full-system resources are a separate nightly/release gate.

Linux execution is catalog-independent. A reviewed lock owns resource URLs,
final uncompressed sizes, checksums, root partition, the exact Ubuntu source
package, and kernel release. Preparation tools download and verify these
artifacts before simulation; the gem5 configuration consumes only local
resource objects. The Linux gate uses an Atomic RISC-V Host CPU for functional
OS/driver validation while Holon timing remains in the cycle-accounted SimObject
and executable RTL calibration contracts.

Shared semantic/runtime sources are mapped into gem5's SCons variant directory
with `duplicate=False`; source-tree object files and copied semantic
implementations are forbidden. Build/run subprocesses disable Python bytecode
output for the same source-tree ownership reason. The upstream build defaults
to 16 jobs. Current
vector and matrix timing parameters are executable calibration contracts: RTL
module tests compare observed zero-stall issue-to-event cycles against the same
calculator used by the SimObject. DMA setup latency is applied to the actual
gem5 completion event and reported separately from memory-response wait cycles.
Idle checkpoints require an `IDLE`, quiescent device. A separate-process
round-trip gate preserves descriptor, IRQ enable/status, interrupt assertion,
and elapsed-cycle state, then requires another program to complete after
restore.

**Rationale:** Explicit issue/completion ownership is the smallest contract that
supports both fast synchronous tests and event-driven simulation without
duplicating semantics. Isolated acquisition and effective-standard auditing
make a rolling upstream dependency reviewable and reproducible.

**Consequences:** The old model target, namespace, files, and symbols are
removed. gem5 timing parameters and the simulation-only Linux driver are not
product ABI. New architecture features remain blocked until semantic, device,
timing, workload, and cost evidence is approved by a later ADR.

## ADR-0058: Self-Hosted Execution Before Performance Expansion

**Status:** Accepted; supersedes the Host/DmaDevice destination in ADR-0056/0057.

**Decision:** Holon runs the complete workload on its own program machine.
Implement validated cold boot and autonomous functional execution first, a
complete minimal Transformer second, then a no-Host gem5 execution system with
timing memory requests. One C++26 semantic core owns all instruction semantics.
The accelerator ABI device is not part of the autonomous boot/execute path.

**Alternatives:** Continuing the RISC-V Host peripheral model would measure a
different system. Computing unsupported Transformer stages in the test harness
would conceal missing architecture semantics. Both are rejected. A clocked
gem5 execution object can share semantic effects without inheriting DmaDevice
or introducing another instruction interpreter.

**Consequences:** Initial boot work preserves released ISA/ABI and RTL behavior.
New numeric semantics need schema, reference tests, and explicit documentation
before implementation. gem5 performance evidence follows whole-program
functional correctness; no new RTL follows without a separate approval. The
existing Host adapter is removed with its verified autonomous replacement,
not carried forward as a parallel product. Feature-sized tested commits are
required throughout migration.

## ADR-0059: RV32 Scalar Compatibility And NPU ISA Redesign

**Status:** Accepted direction and RV32IM + Zicsr / ILP32 baseline, with the
C extension disabled; standard scalar instructions are 32-bit and Holon NPU
instructions are fixed 64-bit. Operand/opcode details require the review in
`docs/ISA_REDESIGN.md` before metadata or decoder implementation.

**Decision:** Preserve RV32 scalar instruction and calling-convention
compatibility so freestanding C23/C++26 control programs can use the upstream
RISC-V toolchain. Vector/matrix access initially uses explicit intrinsics or
assembly with a defined calling convention, not compiler auto-vectorization.
Redesign vector and matrix instruction contracts together instead of adding
FP32 opcodes to the current restricted Holon scalar/vector format. Holon retains
VLA execution, explicit predicates, independent vector/matrix encodings, and
intentional use of the space freed by excluding RVC. RVV binary compatibility
is not a goal. Frontend replacement changes implementation, not ISA semantics.
Reclaimed non-RVC prefixes identify the 64-bit Holon instructions; standard
RISC-V instruction-length parsing is not reused for these custom formats.

**Alternatives:** Keeping the custom scalar instruction set would require a
separate compiler backend for ordinary control code. Extending the current
single-predicate, immediate-addressed vector and matrix-command-block interface
would preserve the limitations this redesign is intended to remove. Neither
is the target. Standard scalar compilation does not imply RVV auto-vectorization
or stock disassembler support for Holon instructions.
Fixed 64-bit NPU forms are chosen over a compact base plus extension words for
operand space and simpler length handling; program footprint and fetch cost
still require workload measurements before RTL approval.

**Consequences:** ADR-0048 remains valid for independent NPU ISA ownership;
the new target refines its complete-program contract to include standard RV32
scalar semantics. Current ISA 1.0/ABI 3.0 and RTL remain the verified migration
baseline. The target execution environment is confirmed as single-hart M-mode
bare metal with standard traps/CSR/MRET and no U/S mode, MMU, or OS. This approval
does not select NPU opcode fields or the detailed CSR address/interrupt contract.
Scalar loads/stores use a unified 32-bit physical address space spanning mapped
scratchpad and system memory; tensor bulk movement still uses explicit DMA.
The scratchpad-only scalar alternative is rejected because it unnecessarily
restricts ordinary C/C++ pointers, stack and global-data placement. Exact region
placement, permissions and ordering belong to the upcoming execution contract.
Numeric extensions, ELF/toolchain integration, predicates, vector/matrix
operands, and memory/ordering must be specified coherently before the semantic
core is migrated. This does not authorize RTL implementation.

## ADR-0060: Semantic Frontend Decode Foundation

**Status:** Accepted for simulator implementation; no RTL authorization.

**Decision:** Freeze four-byte alignment and little-endian framing: low bits
`11` select a 32-bit standard scalar word; `00/01/10` select a 64-bit Holon
frame. Frame recognition is distinct from opcode legality. Implement complete
RV32IM/Zicsr and MRET/WFI pattern decoding, operand extraction, and disassembly
before changing program execution. Standard encodings follow the RISC-V
unprivileged specification; unselected extensions fault rather than aliasing
Holon instructions. CSR address legality remains an execution-stage check.

**Rationale:** Scalar decode and framing do not depend on unresolved vector
operand or privileged-state choices. Separating them permits executable,
independently testable progress without prematurely defining those semantics.

**Consequences:** The canonical ISA schema gains a `semantic_frontend` section
with an initial decode-only stage, extended to scalar effects by ADR-0061.
Its internal C++ metadata is generated by
the existing ISA generator; the existing ABI/RTL outputs continue to describe
the verified accelerator. This is a temporary migration boundary, not two
selectable product ISAs. Program-machine cutover must remove the superseded
control decoder after the replacement execution/tests are complete. No new
opcode is advertised by framing/decoding alone.

## ADR-0061: Shared Scalar Effects Before Machine Cutover

**Status:** Accepted for semantic implementation; no RTL authorization.

**Decision:** Evaluate RV32IM/Zicsr instructions into typed effects inside the
canonical semantic library. Arithmetic and branch results are computed once;
memory, CSR, fence, MRET and WFI requests remain explicit until the machine
commits them. A failed load never writes a destination, and issue never counts
as retirement. Use 32-bit physical addresses for scalar requests and native
machine exception causes, distinct from the released accelerator fault ABI.

**Alternatives:** A separate RV32 interpreter would duplicate state/retirement
and eventually diverge between direct and gem5 execution. Switching the current
machine before memory/trap and NPU operand contracts exist would silently break
the verified baseline. Neither is accepted. The existing machine reuses the
new evaluator for matching arithmetic while its encoding is being replaced.

**Consequences:** Extend internal schema metadata to the scalar-effects stage;
public RTL/C capability values remain unchanged. Tests independently verify
all instruction effects and delayed load completion. The next cutover must
consume this evaluator in `program_machine`, implement physical routing and
M-mode state, and remove the old control decoder rather than retain a mode
switch. This feature is not evidence of complete RV32 program execution.

## ADR-0062: Shared M-Mode Hart State

**Status:** Accepted for semantic implementation; no RTL authorization.

**Decision:** One scalar hart state owns 32 registers, PC, retirement, machine
CSRs and pending scalar effects. The existing program machine owns this state;
there is no second interpreter, fetch loop or memory model. RV32 effects from
ADR-0061 commit through typed events. Memory/fence issue does not retire;
completion validates token and payload before mutation. Trap entry records the
faulting PC, and interrupts wait for accepted operations to complete.

**Contract:** M-mode only, IALIGN=32, little-endian, MPP fixed to M. Support direct
and vectored mtvec, standard machine software/timer/external interrupts, MRET
and a real WFI wait state. CSR inventory is internal ISA-schema metadata.
Unimplemented hardware performance counters/selectors read zero; unsupported
CSR addresses trap. Architectural mcycle consumes externally supplied elapsed
cycles, never a second timing model. Guest-writable minstret is separate from
monotonic simulator retirement used for execution budgets.

**Alternatives:** A standalone RV32 machine would duplicate state; direct CSR
mutation in the runner would make gem5 and fast execution disagree. Neither is
accepted. This component issues effects without owning system memory or
claiming complete RV32 boot. Region routing, instruction fetch and ELF startup
remain subsequent work. Public accelerator ABI/ISA values stay unchanged.

## ADR-0063: Unified Physical Regions And External Storage

**Status:** Accepted for semantic implementation; no RTL authorization.

**Decision:** A validated, immutable physical map routes 32-bit addresses to
local program memory, data scratchpad or system memory. Placement is a boot/
board parameter, not a new ABI register. Local regions have one storage owner
each; program memory is read/execute, scratchpad is read/write and non-executable.
System regions have explicit read/write/execute permissions and identity
physical addresses. The environment owns system bytes. Maps never own storage.

Validate nonempty regions, permissions, overflow, overlap and duplicate local
windows before accepting a map. A data transfer must fit one permitted region
and its actual backing before any write. Instruction fetch reads aligned
32-bit parcels, including a second parcel for Holon words; adjacent executable
regions may meet at a parcel boundary. Address-space wrap never fabricates a
contiguous instruction or memory transfer.

**Ordering:** The synchronous environment services only the hart's current
pending request, not a caller-supplied stale copy. Stores use captured bytes;
loads commit only after a complete read. A fence can acknowledge once prior
requests have completed, which is immediate in this single-pending synchronous
environment. gem5 must use real completion/events instead of this synchronous
service shortcut. No cache, MMIO device, timing model or second interpreter is
introduced by this feature.

**Alternatives:** Putting system storage into the core prevents correct gem5
memory integration. Treating every pointer as an SPM offset breaks ordinary
RV32 code. Silently falling through a denied/local access into system memory
breaks permissions and ownership. These alternatives are rejected. Compiled
scalar tests may drive fetch/issue/complete but contain no instruction arithmetic;
the eventual canonical mixed-width machine must reuse these mechanisms.
