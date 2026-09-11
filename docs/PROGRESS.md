# HolonNPU Progress

## Current Status

HolonNPU 3.0 mainline convergence is in progress. The accepted destination is an
open architecture-research platform with a C++26 semantic baseline. RTL work is
paused; old RTL/gem5 paths are being retired. No 3.0 release tag is published.

Recovery: `9e70a99ef0f694cbd503f259aadf99317c76344b` preserves the complete
pre-transition implementation. Existing release tags remain unchanged.
Project version 3.0 does not assign a new ISA or program ABI version.

## Transition Evidence

| Step | State | Evidence |
| ---- | ----- | -------- |
| Governance | Complete | `gen_abi.py --check`, `gen_isa.py --check`, `check_macro_policy.py`, `git diff --check` passed |
| Canonical code/schema/build | Complete | Fresh configure/build; Debug 14/14, Regression 15/15 including compiled RV32 probes; generation/schema/macro/whitespace checks passed |
| C++ verification/CI | Pending | New coverage evidence required; old RTL metrics do not count |

The retained implementation includes mixed RV32/Holon execution, precise
traps/retirement, ELF loading, independent system-memory boundary tests, and a
small whole-program Transformer checked against a double-precision reference.
These assets were revalidated after cleanup with CMake 4.3.4 and GCC/G++ 16.2.
Generated scalar/NPU C++ metadata is byte-identical to the checkpoint; only
schema ownership and generated references changed. No instruction semantics
were changed. System-memory fixtures now report actual semantic requests, not
estimated packets from a retired backend.

Commands: `cmake --preset debug --fresh`, `cmake --build --preset debug
--parallel 2`, `ctest --preset debug`; the same configure/build/test sequence
for `regression`; `python3 tools/gen_isa.py --check`, `python3
tools/check_isa.py`, `python3 tests/isa_schema_test.py`,
`python3 tools/check_macro_policy.py`, and `git diff --check`.

## Next Work And Limits

Complete the three transition steps in Roadmap, with one verified commit per
step. Then design the dependency/effect contract and independent performance
simulator before adding architectural mechanisms.

No deterministic parallel timing model, task compiler, calibrated DSE,
representative prefill/decode corpus or new RTL is implemented by this cleanup.
The user's block/tile-dataflow document is a core research candidate, not a
selected product architecture. Historic details belong in Git and Changelog.
