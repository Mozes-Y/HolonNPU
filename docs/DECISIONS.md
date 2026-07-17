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

**Status:** Accepted

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

Presets remain intentionally small: debug, regression, and coverage build trees;
debug, lint, regression, and coverage test entry points.

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
