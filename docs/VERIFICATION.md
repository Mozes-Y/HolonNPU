# HolonNPU Verification

## Current Functional Gates

| Test layer | Evidence |
| ---------- | -------- |
| ISA generation/schema | Exact outputs, RV32 patterns, NPU field domains, overlap/reserved encodings and malformed metadata |
| Decode and scalar effects | Selected opcode inventory, arithmetic boundaries, typed operands and upstream encoding oracle |
| Hart/memory/ELF | CSR/traps/WFI, stale tokens, precise retirement, map permissions, overflow and transactional image validation |
| Semantic/runtime | Vector types, predicates/tails, views/tiles, numerical edges, DMA visibility and program construction |
| Whole programs | Independent Transformer stages and system-memory/guest-trap scoreboards |
| Toolchain | C23/C++26 RV32 ELF and raw Holon-byte preservation, required in regression |
| Repository/tooling | Owned C++ sources in build graph, effective C++26, no retired dependencies/macros, local links and negative checker tests |

Run `cmake --preset debug`, `cmake --build --preset debug --parallel 2`, then
`ctest --preset debug`. Repeat with `regression` for optimized code and required
cross-toolchain probes. Build and test are separate. Tests default to two workers;
`-j N` overrides this. `ctest --preset debug -R '^holon_npu_transformer$' -V`
shows case-level output; `-N` lists the actual matrix.

## Oracles And Limits

There is one executable semantic implementation. Independent mathematical
references verify arithmetic, not a second processor interpreter. The tiny
Transformer's shapes/tolerances are specified in
[Transformer Workload](TRANSFORMER_WORKLOAD.md). They are not trained-model
accuracy or representative DSE evidence.

System-memory fixtures include external instruction fetch crossing a page,
601-byte unaligned DMA tails, scalar accesses, and guest illegal-trap/MRET.
Bytes, precise trap registers, PC and retirement are checked independently.
The direct runner does not generate packets or simulate memory backpressure.

Semantic tests retain invalid/stale completion, failed-load atomicity and
captured-store-payload checks. Test counts are not exhaustive coverage claims.

## C++ Coverage

Old Verilator functional/SVA/structural gates have been retired with their
implementation. They cannot count toward C++ or future timing-model coverage.

Configure/build/test with the `coverage` preset. It uses unoptimized GCC
instrumentation and matching gcov, without C++ behavior macros or logging
wrappers. CTest fixtures clean old counters/artifacts before tests and collect
after all tests finish. Atomic counters support the preset's concurrent tests.
Ordinary configurations do not depend on gcov.

The gate requires the exact current translation-unit data set, current
toolchain evidence, unchanged source/configuration during the run, and fresh
counter timestamps. Build before testing; an edited source requires rebuilding.
Filtering tests does not constitute a full coverage gate. Negative tests reject
missing, extra, stale and source-mismatched evidence and below-baseline results.

`spec/coverage_baseline.json` gates owned semantic/runtime C++ lines and
functions at 98% each, derived from clean GCC 15/16 runs. Generated metadata,
standard library, guest code and test oracles are excluded from these metrics;
guest code is validated by cross-toolchain execution. Branch coverage is reported
without a threshold. It includes compiler/exception branches and is not an ISA
functional coverage claim. Functional behavior is judged by the scoreboards.

Artifacts: `build/coverage/coverage/{summary.json,summary.txt,raw/,annotated/}`.
`python3 tools/check_coverage.py --build-dir build/coverage` rechecks a completed
run. After reviewed test improvements, `--record-baseline` may raise measured
integer thresholds; the tool refuses reductions. CI runs all three presets and
uploads logs, toolchain evidence and coverage reports.

## Future Model Acceptance

The independent model must add finite-capacity/backpressure tests, causal
events, dependency/visibility checks, deterministic worker-count equivalence,
parameter sensitivity and calibrated ranking/error evidence. No current
functional pass substitutes for these tests. See [Simulation](SIMULATION.md).

RTL is paused. Later differential/calibration work only compares matching
contracts after explicit architecture admission. No old lint/coverage result
proves the current executable ISA in hardware.
