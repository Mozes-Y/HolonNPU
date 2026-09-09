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

The canonical architectural model defines the redesigned mixed-width ISA.
Released RTL still implements the preceding instruction contract; it cannot
currently be differential-tested by executing those words in the new model.
`sim/rtl_program.*` is test-only encoding/planning for that RTL, with no
interpreter or execution mode. Only the frontend RTL test target links it;
independent mathematical scoreboards check its DMA-visible results. This is
hardware baseline evidence, not redesigned-ISA implementation evidence.

## Simulator-First Architecture Verification

The v2.x destination has one C++26 semantic core shared by the fast runner and
autonomous Holon gem5 execution. The canonical interpreter replacement is
active; the old Host/DmaDevice adapters have been removed and the autonomous
ClockedObject replacement passes autonomous Transformer differential and timing
sensitivity tests. Full performance calibration remains open. No independent gem5 semantic
implementation is permitted.

Future architecture behavior must pass these tiers before RTL:

| Tier | Gate |
| ---- | ---- |
| Semantic core | Directed, property-based, and deterministic random behavior with all typed required events observed at verified invariants. |
| Autonomous execution | Cold boot, token ownership, budget/resume, precise faults, and program-issued memory effects without descriptors. |
| RV32/toolchain migration | RV32IM/Zicsr semantics, ILP32 calls/stack, C23/C++26 ELF execution, no compressed scalar code, mixed 32/64-bit decode/truncation, and custom-byte link integrity. Verified through the canonical machine, not a fixture interpreter. |
| Transformer | Complete Holon program compared stage-by-stage against an independent numeric reference. |
| gem5 execution | Same boot image on a no-Host Holon execution object with timing memory requests. |
| gem5 timing | Cycle-accounted queues, pipelines, banks, contention, and sensitivity. |

Prior accelerator gem5, bare-metal, Linux and checkpoint results are historical
foundation evidence only; their adapters have been removed. Neither
their previous results nor cached executables validate this cutover or the
autonomous gem5/Transformer tiers. Current build/test gates pass, but the broader
simulation-foundation release criteria remain incomplete; see Progress.

`holon_npu_execution` boots the canonical mixed-width machine. It checks token
ownership, invalid boot, budget/resume, guest trap/MRET, vector/DMA programs,
matrix dot/accumulate, a guest VLA loop across three capacities, FP32 exact-bit
edges, conversion rounding, masked loads and failed-load atomicity. Scalar and
DMA error completions validate the reported physical byte against the captured
request, reject invalid addresses without consuming tokens, and preserve precise
PC/retirement while exposing the accepted fault address in MTVAL.

`holon_npu_semantic` runs independent scoreboards through guest DMA writeback:
828 integer programs (six types, three capacities, seed `0x53454d49`), predicate
logic/query/packed tails, signed/unsigned/FP32 comparisons and seeded reductions,
negative stride, scaled gather/scatter, permutation, and captured matrix views.
Source/destination aliases and repeated scatter addresses are exercised.

`holon_npu_runtime` validates the current typed byte builder with 2,077 LI
programs (seed `0x484f4c4e`), all 4,096 ADDI immediates, rejected-append atomicity,
little-endian raw words and a guest loop spanning scalar/Holon instructions.
These are current execution tests, not restored former-ISA compatibility tests.

`holon_npu_transformer` executes one shape-specialized mixed-width program from
embedding through logits, including causal softmax and two affine LayerNorms.
It compares all 16 stage tensors against an independent double-precision
reference after final guest DMA writeback. Ten directed/random fixtures run at
three vector capacities; zero, constant, near-constant and large-score cases
exercise normalization and exp boundaries. The numerical tolerance and limits
are defined before execution in [TRANSFORMER_WORKLOAD.md](TRANSFORMER_WORKLOAD.md).
This gate does not claim dynamic-shape compilation or gem5 timing.

`holon_npu_instruction` gates mixed 32/64-bit framing, all 56 standard scalar
and machine instruction patterns, 57,344 deterministic operand samples, and
exhaustive I/S/B/J immediate reconstruction. It rejects unsupported scalar
encodings independently of frame length. `isa_schema_tests` rejects malformed
profiles, omissions, overlap, and drift, and checks the RTL generation boundary.
The gem5 preset adds `scalar_toolchain_check`: upstream RISC-V assembly/linking
is an independent encoding oracle, and real C23/C++26 compiler output must decode.
The cross-toolchain dependency stays in that preset, not ordinary Debug builds.
Artifacts in `build/gem5/scalar-toolchain/` retain input assembly, ELF attributes,
decoded instructions, and compiler identity. Encoding checks alone do not
execute instructions; the same gate also runs the ELF probes described below.

The instruction test additionally checks 54 Holon NPU operand forms with
55,296 deterministic round trips (seed `0x4e505536`), independent directed bit
allocations, every reserved bit, all element-type codes and typed register
domains. The toolchain oracle links all 54 forms interleaved with scalar words,
compares exact bytes at both 0/4 modulo-8 starts, and checks disassembly. These
checks validate metadata and the codec, not vector/matrix arithmetic, program
retirement or Transformer execution. The next execution path must consume
this decoder and remove the old one, not add an independently bootable mode.

`holon_npu_scalar` verifies all 56 scalar instruction effects: independent
arithmetic scoreboards (including signed-magnitude division and partial-product
multiplication), deterministic random cases, branch/JALR alignment and aliases,
32-bit physical-address requests, captured store bytes, exhaustive byte/halfword
load extension, CSR read/write suppression and machine-control/trap requests.
The existing machine shares matching arithmetic with this evaluator.
`holon_npu_hart` verifies shared scalar state/commit: all 4096 CSR addresses,
8192 random CSR operations, 8192 random register commits, WARL/read-only fields,
counter inhibition/writes, trap PC, interrupt priority, MRET/WFI and transactional
memory/fence completion. Seeds and verified case totals are printed by the test.
`holon_npu_memory` gates region permissions/ownership, atomic failed accesses,
32-bit bounds, cross-region instruction parcels, and routed hart completion.
An independent interval scoreboard checks 8192 deterministic cases with seed
`0x4d415033`. `scalar_toolchain_check` additionally compiles and executes C23
and C++26 probes using real RV32 stack/global accesses and target-side memory
support. The fixture checks every result, checksum, publication marker and WFI
wait; it does not substitute Host arithmetic or interpret guest instructions.
`holon_npu_elf` verifies ELF/profile compatibility, segment size/alignment/bounds,
attributes, entry validation, BSS, owning-image lifetime and transactional
failure. It checks every truncation and 16384 deterministic byte mutations
(seed `0x454c4632`); accepted loads must match exact initialized/zeroed bytes.
The toolchain probes use this loader directly on C23/C++26 executables and
verify BSS before execution through the canonical program machine. This proves
scalar compiled-program integration, not complete Transformer support.

```bash
cmake --build --preset debug --target holon_npu_execution_test --parallel 2
ctest --preset debug -R '^holon_npu_execution$' --verbose
```

The prior descriptor-oriented semantic coverage registry and gem5 statistics
gates need migration. The new scoreboards do not claim those old events or a
completed functional coverage gate. Required events must correspond to verified
new execution effects, not test-tail declarations.

Released RTL module tests retain their existing zero-stall cycle expectations.
They no longer link the obsolete gem5 timing calculator. Calibration of the
autonomous timing model remains a separate acceptance requirement.

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
