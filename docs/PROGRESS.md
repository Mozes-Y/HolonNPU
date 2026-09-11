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
| Canonical code/schema/build | Pending | Fresh retained-test matrix required |
| C++ verification/CI | Pending | New coverage evidence required; old RTL metrics do not count |

The retained implementation includes mixed RV32/Holon execution, precise
traps/retirement, ELF loading, independent system-memory boundary tests, and a
small whole-program Transformer checked against a double-precision reference.
These are pre-transition functional assets; revalidation follows code cleanup.

## Next Work And Limits

Complete the three transition steps in Roadmap, with one verified commit per
step. Then design the dependency/effect contract and independent performance
simulator before adding architectural mechanisms.

No deterministic parallel timing model, task compiler, calibrated DSE,
representative prefill/decode corpus or new RTL is implemented by this cleanup.
The user's block/tile-dataflow document is a core research candidate, not a
selected product architecture. Historic details belong in Git and Changelog.
