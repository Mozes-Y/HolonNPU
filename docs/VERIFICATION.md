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

## Transition Coverage

Old Verilator functional/SVA/structural gates have been retired with their
implementation. They cannot count toward C++ or future timing-model coverage.
The third 3.0 transition step establishes clean-run C++ coverage and records its
measured baseline. Until that step passes, coverage acceptance is pending.

## Future Model Acceptance

The independent model must add finite-capacity/backpressure tests, causal
events, dependency/visibility checks, deterministic worker-count equivalence,
parameter sensitivity and calibrated ranking/error evidence. No current
functional pass substitutes for these tests. See [Simulation](SIMULATION.md).

RTL is paused. Later differential/calibration work only compares matching
contracts after explicit architecture admission. No old lint/coverage result
proves the current executable ISA in hardware.
