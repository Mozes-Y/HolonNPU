# HolonNPU Verification

Verification is part of the product contract. External scoreboards, internal
native SVA, architectural differential testing, functional event coverage, and
structural coverage must agree before release.

## Verification Layers

| Layer | Purpose |
| ----- | ------- |
| Schema checks | Regenerate ABI/ISA artifacts and reject metadata drift or encoding overlap. |
| Structure checks | Enforce interface-native RTL, product reachability, canonical naming, and macro policy. |
| Module tests | Exercise control, loader, completion, local memory, DMA, vector, matrix, PE, and array behavior. |
| Product tests | Execute programs through AXI-Lite/AXI4 and compare system-memory effects. |
| Semantic core | Define decode, retirement, fault, scalar/vector/predicate, memory, DMA, and matrix semantics. |
| Native SVA | Check protocol and internal invariants at the cycle boundary. |
| Coverage gate | Require observed functional events, RTL cover properties, exact artifacts, and structural baselines. |

## Required Entry Points

Development feedback:

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug --output-on-failure
```

RTL lint:

```bash
ctest --preset lint --output-on-failure
```

Optimized regression:

```bash
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression --output-on-failure
```

Coverage:

```bash
cmake --preset coverage
cmake --build --preset coverage --parallel 2
ctest --preset coverage --output-on-failure
python3 tools/check_coverage.py --build-dir build/coverage
```

Simulation foundation:

```bash
cmake --preset gem5
cmake --build --preset gem5 --parallel 16
ctest --preset gem5 --output-on-failure
```

Presets default to two CTest jobs. Command-line `-j N` may override that based on
available memory. Build and test remain separate operations.

## Static Gates

```bash
python3 tools/gen_abi.py --check
python3 tools/gen_isa.py --check
python3 tools/check_isa.py
python3 tools/check_rtl_interface_usage.py
python3 tools/check_macro_policy.py
python3 -m json.tool CMakePresets.json
git diff --check
```

The ABI generator consumes both ABI and ISA schemas. The ISA checker rejects
class/opcode overlap, invalid reserved regions, and incomplete semantic or
coverage metadata. The RTL checker treats only the `npu_top` product graph as
evidence of use; test wrappers are not product consumers.

## Native Assertions

Verilator builds use native `--assert`. Assertions are written directly as
named `assert property` statements, without project assertion macros.

Required protocol properties include:

- VALID and payload stability while READY is low;
- AXI-Lite AW/W pairing and legal response behavior;
- AXI4 INCR profile, maximum burst length, alignment, and 4 KiB boundary;
- accepted read owner stability through `RLAST`;
- DMA data preservation under R/W/B backpressure and error drain;
- local-memory bounds and response ownership;
- legal lifecycle transitions and sticky terminal/IRQ behavior;
- no frontend issue during `RESETTING`;
- completion acknowledgment before terminal visibility;
- engine issue/result and matrix shape/mode invariants.

Software reset does not disable frontend, DMA, AXI, or top ownership assertions.
Local modules may disable state assertions only on the final internal clear
pulse after quiescence. External `aresetn` is the protocol abort boundary.

`npu_assert_fail` is an expected-fail test. CTest passing that test proves a
deliberately violated native assertion terminates simulation.

## Directed Protocol Tests

AXI tests place descriptor, code, argument, DMA, and completion addresses at
`...FF0`, `...FFC`, and adjacent page boundaries. Monitors verify every AR/AW
burst remains within one 4 KiB page and split data/response ordering is exact.

Product reset tests stall ARREADY, RVALID, AWREADY, WREADY, and BVALID in turn,
request software reset, and verify:

- immediate visible `RESETTING`;
- stable VALID payload;
- accepted transaction completion/drain;
- no new frontend or engine issue;
- clean final `IDLE`, IRQ, fault, terminal, and performance state.

## Program And Random Tests

Fixed tests anchor encoding and corner behavior. Deterministic random tests log
their seed and dimensions so failures are reproducible. Vector testing covers
element widths, signedness, predicates, tails, arithmetic boundaries,
rounding/saturation, reductions, permutes, and invalid configurations. Matrix
testing covers edge tiles, accumulation modes, INT32 wraparound, and local
bounds.

Program-level results are never read through a product test probe. Programs
issue DMA STORE and the scoreboard compares simulated system memory. Module-only
local-memory wrappers may use hierarchical observation under `sim/rtl/`.

The architectural model is the semantic oracle for decode, PC/retirement,
faults, scalar and vector state, local memory, and engine effects. RTL-visible
execution must match it.

## Simulator-First Architecture Verification

The v2.x simulation foundation has one C++26 semantic core used by both the fast
direct runner and the Holon gem5 SimObject. No independent gem5 expected-result
logic or separate performance model is permitted.

Future architecture behavior must pass these tiers before RTL:

| Tier | Gate |
| ---- | ---- |
| Semantic core | Directed, property-based, and deterministic random behavior with all typed required events observed at verified invariants. |
| Autonomous execution | Cold boot, token ownership, budget/resume, precise faults, and program-issued memory effects without descriptors. |
| RV32/toolchain migration | RV32IM/Zicsr semantics, ILP32 calls/stack, C23/C++26 ELF execution, no compressed scalar code, mixed 32/64-bit decode/truncation, and custom-byte link integrity. Planned, not satisfied by current custom-ISA tests. |
| Transformer | Complete Holon program compared stage-by-stage against an independent numeric reference. |
| gem5 execution | Same boot image on a no-Host Holon execution object with timing memory requests. |
| gem5 timing | Cycle-accounted queues, pipelines, banks, contention, and sensitivity. |

The current accelerator gem5 preset remains a migration baseline, not evidence
that the autonomous gem5 or Transformer tiers exist. It gates the
timing unit test, RISC-V bare-metal workload, and an idle/quiescent checkpoint
captured and restored by separate gem5 processes. The checkpoint test preserves
descriptor, IRQ, and cycle state and submits a second program after restore.
Linux full-system remains a separate resource-heavy nightly/release gate; its
locked kernel, disk image, matching module, workload, and unique guest PASS
sentinel have a passing local baseline recorded in `docs/PROGRESS.md`.

`holon_npu_execution` directly boots `program_machine` without constructing
`semantic::device`. It verifies invalid-image atomicity, cold state, stale tokens
across boot/reset, resumable instruction budgets, mapped-memory bounds and
precise DMA faults, 64 deterministic vector-loop programs (seed `0x48504e55`),
and `1x1x1`, `16x16x16`, `17x19x23`, `64x64x64` tiled GEMM. Program results are
observed in caller-owned memory after program-issued DMA STORE. It uses the
same memory service as accelerator direct tests, not another arithmetic model.
These programs still use the implemented ISA 1.0 encoding. A successful
cross-compile/link probe does not establish RV32 execution or ELF-loader
correctness; those need new semantic execution tests under ADR-0059.

`holon_npu_instruction` gates mixed 32/64-bit framing, all 56 standard scalar
and machine instruction patterns, 57,344 deterministic operand samples, and
exhaustive I/S/B/J immediate reconstruction. It rejects unsupported scalar
encodings independently of frame length. `isa_schema_tests` rejects malformed
profiles, omissions, overlap, and drift, and checks the RTL generation boundary.
The gem5 preset adds `scalar_toolchain_check`: upstream RISC-V assembly/linking
is an independent encoding oracle, and real C23/C++26 compiler output must decode.
The cross-toolchain dependency stays in that preset, not ordinary Debug builds.
Artifacts in `build/gem5/scalar-toolchain/` retain input assembly, ELF attributes,
decoded instructions, and compiler identity. These checks do not execute RV32.

`holon_npu_scalar` verifies all 56 scalar instruction effects: independent
arithmetic scoreboards (including signed-magnitude division and partial-product
multiplication), deterministic random cases, branch/JALR alignment and aliases,
32-bit physical-address requests, captured store bytes, exhaustive byte/halfword
load extension, CSR read/write suppression and machine-control/trap requests.
The existing machine shares matching arithmetic with this evaluator. These
tests do not establish physical routing, M-mode trap entry or complete RV32
retirement; those require the next program-machine integration tests.

```bash
cmake --build --preset debug --target holon_npu_execution_test --parallel 2
ctest --preset debug -R '^holon_npu_execution$' --verbose
```

The semantic test registry currently requires 13 typed events covering decode,
descriptor compatibility, precise completion, DMA visibility and payload
stability, vector/predicate/quant behavior, matrix accumulation, fault PC,
loader/completion ordering, and reset drain. Events are observed only after the
associated invariant succeeds. gem5 tests independently gate typed SimObject
statistics, minimum event counts, and unique bare-metal/Linux guest sentinels.

Zero-stall vector and matrix issue-to-event cycles are measured in their
Verilator module tests and compared directly with the gem5 timing calculator.
gem5 memory-response latency is reported separately as DMA wait cycles.

An accepted ADR must review correctness, measured workload benefit, alternatives,
software cost, RTL cost, and verification scope before implementation begins.
After RTL exists, differential results validate semantics and measured timing
calibrates gem5. Exact requirements are defined in `docs/SIMULATION.md`.

## Functional Coverage

`holon_npu_tb::test_run` owns test artifacts. Testbenches call
`test.observe(coverage_point, verified)` at the monitor or scoreboard location
where an event has been observed and checked. Bulk declarations at test exit are
forbidden.

The C++ `constexpr` registry is the functional-event authority. Each test writes
its required and hit manifests only after success. CMake's coverage fixture:

1. deletes the previous coverage directory;
2. writes `expected_tests.txt` for the current Verilated CTest set;
3. runs instrumented tests in parallel;
4. checks the raw `.dat` set exactly;
5. merges data with `verilator_coverage`;
6. gates functional events and named RTL covers;
7. writes annotated and summary artifacts.

## RTL And Structural Coverage

Every named product `cover property` is discovered from `rtl/` and must have a
nonzero aggregate Verilator user-coverage count. Unreachable properties are
design or property bugs; they are not retained as pretend goals.

`spec/holon_npu_coverage_baseline.json` contains clean-build integer floors for
line, branch, toggle, and expression coverage. Thresholds may only increase.
FSM is not gated while the tool reports no FSM denominator.

## Debug Workflow

Build and run one case verbosely:

```bash
cmake --build --preset debug --target npu_top_tb
ctest --preset debug -R '^npu_top$' --verbose
```

On failure, preserve seed, descriptor/program addresses, dimensions, fault/PC,
AXI channel state, and the first mismatching architectural result. Add waveform
generation locally only when cycle inspection is necessary; the checked tests
must remain deterministic without waveform dependence.

## Current Limits

- Structural thresholds are initial baselines, not a claim of exhaustive proof.
- Formal verification is not yet integrated.
- The current engine schedule is mostly single-command; future concurrency will
  require additional ordering and arbitration scenarios.
