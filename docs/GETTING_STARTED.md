# Getting Started

## Requirements

Use CMake 4.0+, Ninja, Python 3.10+, and a C23/C++26 compiler/library.
The local validation toolchain is recorded in Progress. Debug does not require
an external simulator or RISC-V compiler. Regression additionally requires a
C23/C++26-capable RISC-V GCC/G++, binutils and RV32IM/Zicsr ILP32 support.

## Configure, Build, Test

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
```

These are separate operations: configure declares targets; build compiles;
CTest runs checks/programs. List tests with `ctest --preset debug -N`.

```sh
cmake --build --preset debug --target holon_npu_transformer_test --parallel 2
ctest --preset debug -R '^holon_npu_transformer$' --verbose
ctest --preset debug -R '^holon_npu_system_memory$' --verbose
```

Transformer output includes seed, vector capacity, stages, retirement and
maximum numerical error. Memory tests report addresses, actual requests,
bytes, traps and retirement; these counts are not a timing prediction.

For an optimized complete functional gate:

```sh
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression
```

Select a cross compiler explicitly when needed:
`cmake --preset regression -DHOLON_NPU_RISCV_GCC=/path/to/riscv64-linux-gnu-gcc`.
Its sibling G++ must also support C++26. Toolchain probes check real ELF
attributes and execute C/C++ programs, not just decode hand-written words.
Artifacts live under `build/regression/scalar-toolchain/`.

For clean-run C++ coverage, use GCC and matching gcov:

```sh
cmake --preset coverage
cmake --build --preset coverage --parallel 2
ctest --preset coverage
```

The full preset prepares fresh counters, runs the tests, then checks coverage.
Read `build/coverage/coverage/summary.txt` or `summary.json`; `annotated/` shows
per-line hits and `raw/` retains gcov reports. A partial coverage run is not a
gate: use Debug with `-R` for individual tests. See
[Verification](VERIFICATION.md#c-coverage) for scope and baseline policy.

For library-only consumers use `cmake -S . -B build/library -G Ninja
-DBUILD_TESTING=OFF`, then `cmake --build build/library`. Public C++ headers
and language requirements propagate through target file sets and links.

## Workload Fixtures

```sh
build/debug/holon_npu_transformer_test --export=build/fixtures/transformer
build/debug/holon_npu_system_memory_test --export=build/fixtures/memory
```

These commands execute their scoreboards before exporting program/input/expected
bytes and reference metadata. They are portable functional fixtures; outputs
contain no invented cycle or retired-backend packet predictions.

## Change A Contract

Read Roadmap and Progress. Edit `spec/holon_npu_isa.json` for encoding metadata,
then run `python3 tools/gen_isa.py`. Update ISA semantics and directed/random
tests for any behavior change. Never manually edit generated headers/references.
Existing numerical rules and precise faults remain the baseline.

## Document Map

Read Architecture for implemented components, ISA for executable semantics,
Simulation for future-model methodology, and Verification for actual gates.
Research notes are hypotheses. Progress reports current evidence; Changelog
and Git preserve history. New features need a passing test result and a commit.

Presets only fix configurations/build trees and essential test entry points.
Use native `--target`, `-R`, `--verbose` and `-j` instead of new wrapper commands.
