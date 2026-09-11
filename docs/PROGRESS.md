# HolonNPU Progress

## Current Status

HolonNPU 3.0 mainline convergence is complete. The repository is an open
architecture-research platform with one C++26 functional baseline. RTL work is
paused; old RTL, accelerator ABI/driver and gem5 paths are retired. Project
version is 3.0.0, independent of ISA/ABI numbering. No 3.0 tag has been published.

Recovery: `9e70a99ef0f694cbd503f259aadf99317c76344b` preserves the complete
pre-transition implementation. Existing release tags remain unchanged.
Governance, code/schema convergence and verification are separate commits.

## Latest Evidence (2026-09-11)

| Gate | Result |
| ---- | ------ |
| Fresh Debug configure/build/test | 16/16 passed |
| Fresh Regression configure/build/test | 17/17 passed, including real RV32 C23/C++26 programs |
| Fresh Coverage configure/build/test | 19/19 passed; 20 current data files |
| GCC 15 coverage cross-check | 19/19 passed, same baseline |
| Native C++ coverage | Lines 98.82%, functions 98.82%, branches 72.31%; line/function gates 98% |
| Generation/schema/repository/macro gates | Passed, including local links and six checker negative tests |
| Library-only build | Passed with `BUILD_TESTING=OFF`, no Python/gcov dependency |
| Portable fixtures | Transformer and system-memory exports passed their scoreboards |

Host tools: CMake 4.3.4, GCC/G++/gcov 16.2 and 15.3. Run configure with
`cmake --preset <name> --fresh`, build with `cmake --build --preset <name>
--parallel 2`, then `ctest --preset <name>` for debug/regression/coverage.
`python3 tools/gen_isa.py --check`, `python3 tools/check_isa.py`,
`python3 tools/check_macro_policy.py`, `python3 tools/check_repository.py
--build-dir build/debug`, and `git diff --check` also pass.

A deliberately partial coverage run was rejected for missing current data;
the full preset was then rerun successfully. Coverage artifacts record source
provenance/compiler and are not ISA functional completeness claims. CI now runs
these gates; the remote workflow has not been executed as part of this local work.

The semantic core, generated C++ instruction metadata and runtime implementation
are unchanged from the recovery checkpoint. Existing precise traps, ELF,
vector/matrix arithmetic, DMA and independent Transformer tests remain active.
Memory fixtures report actual semantic requests, not retired-backend packets.

## Next Work And Limits

Define the dependency/effect contract, then build the independent performance
simulator according to Roadmap and Simulation. No deterministic parallel timing
model, task compiler, calibrated DSE, representative prefill/decode corpus or
new RTL was implemented by this cleanup. The user's block/tile-dataflow design
is a core research candidate, not a selected product architecture. Historical
details belong in Git and Changelog.
