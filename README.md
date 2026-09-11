# HolonNPU 3.0

HolonNPU is an AI-processor architecture research and simulation project.
The current implementation is a C++26 functional baseline: RV32IM/Zicsr
M-mode control with 64-bit Holon vector, predicate, matrix and DMA instructions.

A complete small Transformer executes as guest instructions and is checked
against an independent mathematical reference. A deterministic parallel
performance simulator and compiler/task research are **planned, not implemented**.
Release `v3.0` marks the repository transition, not a new ISA/ABI version or
hardware implementation. See [Changelog](CHANGELOG.md) for the release scope.

## Build And Test

Requirements: CMake 4.0+, Ninja, Python 3.10+, and a C23/C++26 compiler with
`std::expected` (GCC 15+ is the tested toolchain family). No RTL simulator or
external system simulator is required.

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
```

Regression also requires upstream RISC-V GCC/G++ supporting C23/C++26,
binutils, RV32IM/Zicsr and ILP32:

```sh
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression
```

For measured C++ source coverage, repeat configure/build/test with `coverage`.
It requires matching GCC/gcov and the same RISC-V toolchain as regression.
The gate cleans counters, checks current-run evidence and enforces the measured
line/function baseline. Reports are in `build/coverage/coverage/`.

Use `--target holon_npu_transformer_test` for one executable and
`ctest --preset debug -R '^holon_npu_transformer$' --verbose` to see seeds,
capacities and numerical comparisons. Build commands do not run tests.
Presets only pin build trees, configurations and small test entry points;
no per-subsystem aliases. See [Getting Started](docs/GETTING_STARTED.md).

## Research Direction

| Topic | Status |
| ----- | ------ |
| Typed semantic core, direct execution, ELF/RV32 probes | Implemented |
| Vector/predicate/matrix arithmetic and independent Transformer oracle | Implemented functional baseline |
| Deterministic multithreaded, mixed-fidelity performance simulation | Planned |
| Parameterized prefill/decode, KV cache and resource-constrained DSE | Planned |
| Explicit tile dataflow, soft affinity, lifetime and block scheduling | Core research candidate, not a selected product |
| BF16, low precision and system scaling | Open research directions, not release promises |

[Roadmap](docs/ROADMAP.md) is the development authority. New ideas are welcome
as falsifiable studies alongside the [original research note](docs/research/README.md).
RTL work is paused until a later evidence-based admission decision.

## Repository

| Path | Ownership |
| ---- | --------- |
| `spec/` | Current executable ISA schema; no historical accelerator ABI |
| `sim/semantic/` | Shared semantics, hart, memory, ELF and direct execution |
| `sim/guest/` | Freestanding guest support |
| `include/`, `sw/` | Typed program-construction API and implementation |
| `tests/` | Directed/random programs, mathematical oracles and toolchain probes |
| `tools/` | Generation and verification utilities |
| `docs/` | Current contracts, workflow and research records |

[Architecture](docs/ARCHITECTURE.md), [ISA](docs/ISA.md),
[Simulation](docs/SIMULATION.md), [Verification](docs/VERIFICATION.md) and
[Progress](docs/PROGRESS.md) separate implementation, methodology and evidence.

## History And Discipline

Old RTL, descriptor/MMIO software and gem5 integration are retired from the
mainline. The complete recovery checkpoint is
`9e70a99ef0f694cbd503f259aadf99317c76344b`; release tags remain unchanged.
No in-tree legacy products or compatibility interpreters are maintained.

Use C23/C++26 and typed APIs, not project behavior macros. Change schemas before
generated files, verify each feature, update current documentation and commit
it before starting the next feature. Publish only on explicit instruction.

All rights reserved; see [LICENSE](LICENSE) for the repository's terms.
