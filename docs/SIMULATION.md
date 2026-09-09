# HolonNPU Simulation Contract

HolonNPU follows a simulator-first architecture process. The simulator is not a
late validation aid: it is the required executable specification and performance
evaluation platform used before new architecture behavior may enter RTL.

This document is the implementation contract for the active simulation
foundation. It defines the boundary that the C++26 semantic core, direct runner,
gem5 execution model, and system tests must preserve.

## Self-Hosted Direction

ADR-0066 is the active canonical execution replacement. There is no compatibility
ISA selector: local state, RV32 hart, Holon decode and typed completion belong
to the same program machine. Older accelerator-adapter descriptions below are
migration context, not permission to retain an independent old interpreter.

The target is autonomous Holon program execution, not a Host-controlled NPU
peripheral. Work proceeds in this order: functional boot/execution, a complete
minimal Transformer program, then a standalone gem5 timing system. ADR-0058
supersedes the earlier requirement for a RISC-V Host and `DmaDevice`.

ADR-0059 selects RV32IM + Zicsr, ILP32, and no C extension for the target
Holon scalar path. This is execution on Holon itself, not reintroduction of a
Host CPU. Vector/matrix instructions retain independent Holon encodings and
VLA/predicate principles; their redesign and the ELF/runtime contract are
reviewed in `docs/ISA_REDESIGN.md`. Standard scalar words are 32-bit and Holon
NPU instructions are fixed 64-bit using reclaimed non-RVC prefixes. ISA 1.0
remains the released RTL baseline, not an alternate semantic execution mode.
ADR-0066 replaces the canonical interpreter; consumer migration is incomplete.

The new semantic/runtime tests execute only the mixed-width ISA. Released
frontend RTL tests use `sim/rtl_program.*` as test-only encoding/planning, not
an interpreter, and compare independent mathematical results after DMA store.
The helper is linked only to that RTL test target; it does not define an
alternate semantic mode or a product runtime. Old gem5 timing/device adapters
still require replacement, so earlier complete-gate results are historical.

The target executes in single-hart M-mode, without U/S mode, MMU, or OS.
`holon_npu_instruction.hpp` separates typed frame recognition from standard
scalar opcode/operand decoding. The internal metadata is generated from the
canonical ISA schema's `semantic_frontend` section; it is not an
independently executable ISA mode or an advertised RTL capability. Program
execution/trap migration must consume this decoder rather than duplicate it.
The selected scalar address model maps scratchpad and system memory into one
32-bit physical space. Scalar system accesses will use the same typed external
completion boundary; system storage must not move into the semantic core.

`holon_npu_scalar.hpp` implements the scalar-effects stage (ADR-0061): standard
integer arithmetic, branch decisions, precise exception candidates and typed
load/store/CSR/fence/machine-control requests. It owns no state, run loop, memory
storage or timing. Existing scalar arithmetic already uses it; full RV32
machine cutover must consume these same effects. An evaluated memory or CSR
request is not evidence of successful access, retirement or trap handling.

`holon_npu_hart.hpp` adds shared scalar state/commit (ADR-0062). The canonical
program machine owns it, and current matching arithmetic retires through it.
Machine CSRs, interrupt/trap entry, MRET/WFI and token-checked memory/fence
completion are implemented without a second fetch loop or memory owner.
Architectural counters and simulator budget counts are distinct. MCYCLE only
consumes externally supplied elapsed cycles; no latency model is introduced.
These state tests alone are not evidence of ELF startup, mixed-width program
execution or autonomous gem5 completion.

ADR-0063 adds `memory::physical_map`, a validated mapping without storage.
`memory::bindings` supplies core-owned program/SPM bytes and environment-owned
system bytes; system addresses retain identity. Permissions, region boundaries
and backing extents are checked before any transfer. Program and SPM windows
cannot be aliased or silently redirected into system memory. Placement is an
explicit fixture/boot configuration, not a public ABI address allocation.

`fetch_instruction` uses the shared frame recognizer and executable 32-bit
parcels. A 64-bit word may span adjacent executable regions; a missing second
parcel reports that parcel's address while retaining the instruction PC.
`service_scalar` synchronously services only the hart's live pending effect;
loads/stores/fences still retire through `hart_state::complete`. It must not be
used as a gem5 timing shortcut. Existing accelerator system transfers use the
same checked external-memory view.

The toolchain fixture compiles C23/C++26 RV32 control programs, executes their
stack accesses in SPM and globals in system memory, and verifies published
results before observing WFI wait. Portable guest `memset`/`memcpy` live in
`sim/guest/freestanding.c` and execute as guest instructions, not Host helpers.
The probes now load actual ELF executables through `elf::image` (ADR-0064),
not objcopy section binaries. This is component integration evidence, not a
second boot mode. The canonical mixed-width execution cutover remains next.

`elf::image::parse` owns validated ELF32 bytes and PT_LOAD metadata. `load`
preflights every segment against physical permissions and actual storage before
copying initialized bytes and zeroing BSS. It is a quiescent initialization API,
not a runtime store. ELF permissions are minimum requirements on the board map;
loading never changes runtime permissions. Program/SPM and system backings must
be distinct storage owned by the core and environment respectively.

The initial profile accepts static little-endian RV32 ILP32 ET_EXEC, no RVC,
matching virtual/physical addresses, nonoverlapping load segments and an aligned
entry parcel in initialized executable storage. Schema-derived RISC-V attributes
are required. PT attributes are authoritative, with SHT fallback when absent;
one `riscv` vendor/Tag_file record is accepted. Dynamic/TLS images, extended
table numbering and unsupported mandatory attributes fail explicitly. No ELF
loader invents ISA semantics, initializes sp/gp, or adds a syscall exit.

The former accelerator adapter and Host tests have been removed. Their
replacement is the autonomous clocked execution object described below, which
uses the canonical machine instead of restoring an old interpreter. Its build
and validation status is recorded separately from historical device results.

ADR-0065 defines the next NPU instruction/state contract in ISA_REDESIGN. The
canonical schema's `semantic_npu` generates a typed operand registry and
[reference](NPU_OPERAND_REFERENCE.md). `encode_holon`/`decode_holon` enforce
register domains, reserved fields and operation type restrictions; they do not
execute those operations on their own. The canonical machine now consumes the
codec and commits vector/matrix effects after a matching engine completion.
Full opcode verification and consumer migration must preserve explicit length,
predicate, numeric and precise-fault rules before the complete Transformer and
autonomous gem5 gates.

### Autonomous Boot And Execution Contract

- An explicit physical map routes core-owned program/SPM storage and
  environment-owned system memory. Boot loads a validated ELF or raw mixed-width
  program, not a descriptor. Entire image validation precedes state mutation;
  boot with pending work is rejected. Guest startup initializes sp/gp normally.
- Cold boot clears hart, vectors, predicates, tile views/registers, local memory
  and retirement. Predicate bits reset to zero; VL is an explicit operand, not
  hidden state. Tokens are not reused across boot/reset within a machine.
- `advance`/`complete` exposes fetch parcels, scalar memory, NPU execution and
  DMA requests. Local effects commit inside the core; the environment only
  services system memory and completion timing. Bad tokens/payloads do not
  mutate architectural state. All selected operations block retirement until
  successful completion; traps preserve the issuing PC and do not retire.
- `run_program` is the synchronous environment for this same protocol. It
  returns distinct stop, wait or budget reasons and never instantiates a Host
  device or interprets arithmetic. Budgets count retired instructions and trap
  entries so a guest fault loop cannot hang a bounded run; fetch parcels are not
  retired instructions. WFI waits, STOP retires once and returns its status.
- gem5 will use this same boot contract and program protocol, but schedule
  completion through its event queue and timing memory port. It must not call
  the synchronous runner as a performance shortcut.

Transformer acceptance requires program-executed QKV projection, attention
scores/masking/softmax, value aggregation, output projection, residual paths,
normalization, feed-forward activation, and output projection. A separate
reference may calculate expected values but may not service missing semantic
operations. Numeric and ISA contracts are reviewed before those extensions.
The first executable specialization is defined in
[TRANSFORMER_WORKLOAD.md](TRANSFORMER_WORKLOAD.md), including its guest exp
approximation, fixed dimensions, independent stage reference and tolerance.
It runs as one mixed-width program; it is not a Host sequence of kernel calls.
Its current functional evidence does not satisfy autonomous gem5 timing or
general dynamic-shape compilation gates.

## One Semantics, Two Entry Points

```text
ABI and ISA schemas
        |
        v
Holon semantic core
        |-----------------------------|
        v                             v
fast direct runner             Holon gem5 SimObject
semantic/random tests          timing and full-system tests
                                      |
                                      v
                               architecture review
                                      |
                                      v
                                     RTL
```

The direct runner and gem5 integration execute the same semantic core. They are
not separate functional models. gem5 is the only performance and full-system
model.

## Holon Semantic Core

The semantic core is a deterministic, gem5-independent C++26 library. It owns:

- ISA decode, legality, retirement, PC, and precise fault behavior;
- scalar, predicate, vector, matrix, CSR, and lifecycle architectural state;
- integer, quantized, and future numeric semantics;
- local-memory and DMA architectural ordering;
- typed issue, effect, token, completion, and fault contracts.

It does not own cycle counts, bandwidth, cache/DRAM behavior, AXI signals, RTL
state, or gem5 event scheduling. A fast runner may complete typed requests
synchronously so large directed and constrained-random suites remain practical.

Project code targets C++26 and follows modern type-safe practice: strong domain
types, concepts, RAII, standard vocabulary types such as `std::expected`,
`std::variant`, and `std::span`, and deterministic ownership. Project-defined
behavior macros and stringly typed protocol contracts are forbidden.

### Precise Two-Phase Execution

`holon_npu::semantic::program_machine` exposes one execution protocol:

- `advance()` returns a retired event, one typed pending operation, or a
  terminal event;
- a pending operation carries a strong token and a `std::variant` operation;
- `complete(token, result)` is the only way external work commits;
- stale, duplicate, or out-of-order tokens return `std::expected` API errors
  and do not create architectural faults;
- PC and `instret` advance only after successful completion, preserving the
  exact faulting instruction for engine and DMA failures.

Typed operations cover instruction fetch, scalar local/system memory, vector
and matrix execution, ordered tensor DMA, and scalar fences. DMA load data enters
the core only in a completion payload; stores capture their payload at issue.
Bus-error completions may identify the failed byte/packet address within the
pending request. The core rejects an out-of-request address as an API error
without consuming the token. A valid address supplies MTVAL while MEPC remains
the issuing instruction; an unspecified address denotes the request start.
The core owns code, scratchpad and register state, not system memory. A direct
execution environment owns external bytes and services the same pending requests
used by gem5. There is no descriptor loader, MMIO lifecycle device or second
interpreter on this autonomous path.

## Autonomous gem5 Model

`HolonNpu` derives from `ClockedObject`, not `DmaDevice`. It boots the same
mixed-width image as the direct runner and uses a timing `RequestPort` for
external reads, writes and fetches. No Host CPU, doorbell, PLIC or driver launches
the workload. Functional memory access is limited to initial image placement and
terminal observation; guest memory traffic uses timing requests.
The gem5 source set excludes the synchronous direct runner and test program
builder; the compilation audit rejects either being linked into the simulator.
The baseline board uses `NoncoherentXBar` (16-byte width, frontend/forward/response
latencies of 3/4/2 cycles) and `SimpleMemory`. It has no snoop filter or coherent
cache participants. These are explicit experiment parameters, not RTL claims.

The blocking baseline has one pending operation and one accepted memory packet.
Requests split at 256 bytes, 4 KiB pages and cache-line boundaries. A rejected
packet is retained unchanged until retry. Read data commits only after the entire
operation completes. The adapter schedules response header/payload delays before
using data and advances architectural cycle counters from elapsed gem5 time.

Timing uses semantic operand footprints, not a duplicate instruction evaluator.
It accounts for frontend fetch, local traffic, vector groups, divide/sqrt latency,
ordered reductions, matrix wavefronts and external memory service. Parameters
describe an initial model, not approved RTL or calibrated performance claims.
Statistics distinguish instruction retirement, traps, lane slots/active lanes,
matrix MACs, local bytes, memory bytes/packets/retries and scheduled resource time.
An exclusive tick ledger also accounts for frontend, local/vector/matrix service,
memory setup, synchronization and memory response/retry time. Its sum must equal
the entire execution interval; scheduled cycle estimates alone are insufficient.

The autonomous gate runs the independently verified complete Transformer image
at baseline, increased memory latency, reduced vector throughput and constrained
memory bandwidth. The last case must exercise timing request rejection/retry.
All cases
must preserve PC, retirement and complete memory effects while the affected
timing components change. Each run retains configuration, statistics and outcome
artifacts. Current validation status belongs in `docs/PROGRESS.md`.

The same gate also executes three independently checked system-memory programs.
The first Holon instruction is fetched across a 4 KiB boundary; 601-byte DMA
loads/stores begin at page offsets `0xFF0`, `0xFF3` and `0xFFC`. Scalar system
loads/stores and an illegal Holon instruction exercise guest trap/MRET recovery.
Guest-written MEPC/MCAUSE/MTVAL, unaffected memory, final PC and an independent
34-instruction retirement count are checked. Packet/byte counts and the elapsed
ledger must match, and constrained bandwidth must cause request retries.
Fixtures and `memory-boundaries.json` are retained with the timing artifacts.
This tests successful memory responses and architectural decode traps, not bus
error response injection.

Known unfinished work: detailed pipeline/resource calibration, broader timing
and fault workloads, interrupt-driven WFI wakeup and checkpoint restore. The
current adapter reports unsupported waiting/checkpoint operations explicitly;
the retired Host-device tests do not establish these autonomous capabilities.

## gem5 And Toolchain Policy

- Use the upstream gem5 `stable` branch.
- Build the complete gem5 binary, Holon extension, and shared semantic core in
  C++26 mode; verify the effective standard and fail rather than silently lower
  it for any simulator source.
- Record the exact gem5 commit SHA, compiler identity, model parameters, system
  configuration, workload revision, and random seed with every result.
- Run compatibility CI against the current upstream `stable` branch.
- Resolve upstream incompatibility explicitly; do not switch branches or fork a
  second semantic implementation as a workaround.

Because `stable` advances, an architecture report is reproducible by its
recorded commit and configuration rather than by the branch name alone.

The source checkout is created under the gem5 build tree from the official
repository and never reuses an arbitrary developer checkout. A reviewed overlay
selects C++26 for the complete gem5 binary. Build metadata records the checkout
SHA, overlay hash, compiler, effective standard, build type, and Host. A
compile-command audit rejects any effective C++17, C++20, or C++23 translation
unit. Overlay drift fails visibly when upstream `stable` changes.

Canonical commands are:

```bash
cmake --preset gem5
cmake --build --preset gem5 --parallel 16
ctest --preset gem5 --output-on-failure
```

The build uses an isolated upstream checkout, maps shared Holon sources into the
SCons variant tree without copying them, and defaults the upstream SCons build
to 16 jobs. Python bytecode output is disabled for build and run subprocesses,
so imported EXTRAS/configuration files cannot modify the source tree.
Debug/regression RTL tool dependencies are not required by this gem5-only
preset.

## Verification Tiers

| Tier | Required evidence |
| ---- | ----------------- |
| Semantic | Directed and deterministic random opcode, numerical, trap, token and memory-ordering checks. |
| Toolchain | Compiled RV32 C23/C++26 ELF execution and custom-instruction byte preservation. |
| Complete workload | Guest embedding, attention, residual, normalization, feed-forward and logits compared stage-by-stage with independent mathematics. |
| Autonomous gem5 | Identical program and memory effects without Host CPU/device launch; real timing-port traffic and retry/fault checks. |
| gem5 performance | Component accounting, parameter sensitivity and documented calibration with configuration/SHA/toolchain evidence. |
| RTL differential | Approved matching operations compare architecture effects and measured latency; unchanged old RTL is not evidence of the redesigned ISA. |

The Transformer contract is in `docs/TRANSFORMER_WORKLOAD.md`. A passing
functional workload is necessary but does not prove a complete performance
model. Previously collected Host-device/Linux statistics and coverage must not
be counted as autonomous model evidence.

## Architecture-To-RTL Gate

An RTL implementation may begin only after an ADR accepts evidence containing:

1. the workload and quantified limitation;
2. proposed ISA/ABI semantics and alternatives;
3. passing semantic-core tests;
4. passing autonomous gem5 execution and memory-system tests;
5. cycle, bandwidth, utilization, and sensitivity measurements;
6. software, RTL, verification, and migration costs;
7. explicit RTL acceptance criteria and coverage requirements.

After RTL exists, Verilator and implementation measurements calibrate gem5.
Material divergence is resolved in the semantic contract, timing parameters, or
RTL; it is never hidden by independent expected-result code.

Behavior-preserving RTL bug fixes and optimizations may use the existing model
contract. Any software-visible behavior, result, ordering, fault, capability, or
performance mechanism that changes the architecture must pass the full gate.
