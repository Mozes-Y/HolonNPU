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
| Architectural model | Define decode, retirement, fault, scalar/vector/predicate, memory, DMA, and matrix semantics. |
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
