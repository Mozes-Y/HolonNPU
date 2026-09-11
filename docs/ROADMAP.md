# HolonNPU Roadmap

HolonNPU 3.0 is an architecture-research and simulation platform, not a new
hardware release or a new program ABI. This is the authority for development
order. Current behavior belongs in Architecture and ISA; candidate behavior
belongs in research notes, not capability claims.

## Development Rules

- Read this roadmap and Progress before implementation.
- Define the scope, acceptance checks and affected contracts before editing.
- Use one schema for the current executable ISA. No legacy execution modes.
- Preserve C23/C++26, typed APIs and the project macro policy.
- Validate each reviewable feature, update current evidence, and commit it
  before starting the next feature. Do not push or publish without instruction.
- RTL development is paused. New RTL requires an explicit later decision after
  semantic, performance, software-cost and verification review.
- Research candidates may coexist as explicit experiments, not historical
  product forks or independently maintained instruction interpreters.

## 3.0 Mainline Transition (Active)

Recovery checkpoint: `9e70a99ef0f694cbd503f259aadf99317c76344b`.
The `v2.0` tag retains the accelerator RTL; the checkpoint also retains later
test helpers and the autonomous gem5 adapter. No existing tag is moved.

| Step | Deliverable | Acceptance |
| ---- | ----------- | ---------- |
| 1. Governance | Research scope, recovery reference, original research note | Documentation/static checks; no implementation claims |
| 2. Mainline convergence | Remove RTL/gem5 and accelerator-only ABI/driver/build paths; canonical executable ISA; current docs | Fresh C++ build, generation/schema checks, all retained semantic/workload/toolchain tests |
| 3. Verification | C++ coverage and clean-run evidence, CI, dependency/document guards | Debug/regression/coverage pass; no RTL/gem5 dependency or stale coverage credit |

Retain the scalar hart, decode, numerical semantics, memory/ELF loader, direct
runner, program builder, independent mathematical scoreboards and reusable
memory-boundary fixtures. Removing an old backend must not delete the tests
of behavior still implemented by the semantic core.

The 3.0 transition does not implement a parallel performance simulator or a new
task ISA. Report those as remaining work, not as completed foundation.

## Research Foundation

1. Define typed operation effects, dependencies and memory visibility, preserving
   precise baseline retirement. Separate ISA changes from scheduling experiments.
2. Build an independent C++26 execution-driven performance model with finite
   resources and deterministic single-run multithreading. The same input,
   configuration and seed must produce the same architecture results and
   modeled cycles at different worker counts.
3. Add parameterized Transformer prefill/decode, including KV cache, alongside
   matrix/vector/reduction/transpose/DMA/scalar microbenchmarks. Keep full
   instruction correctness checks. The existing tiny Transformer is an anchor,
   not representative performance evidence.
4. Compare latency and throughput under compute, SRAM, port and bandwidth
   budgets. Use Pareto comparisons and held-out workloads; uncalibrated resource
   proxies are not physical area/power estimates.

Scope: single chip and one RV32 control hart, with scalable engine grouping,
scratchpad and memory channels as experimental parameters. Start with legal
internal overlap and precise ordered retirement; explicit asynchronous command
semantics are a separately reviewed candidate. Trace replay is deferred until
profiling establishes a need and its applicability can be checked.

## Open Architecture Research

The block-structured explicit tile-dataflow proposal in
[research/README.md](research/README.md) is a core candidate to validate in full,
not a predetermined winner. Compare its components separately: explicit DAG,
soft affinity/local binding, tile storage, scope lifetime and block retirement.
New ideas enter through a falsifiable question, baseline and acceptance study.

Future research includes BF16/mixed accumulation, modern low precision and
scale metadata, and system scaling/isolation. These directions are not tied to
promised release numbers and are not frozen ISA, ABI or RTL features.

## Evidence To RTL

Each selected feature needs: a workload and bottleneck; alternatives; semantic
implementation and directed/random tests; performance sensitivity and error
analysis; compiler/runtime and hardware cost; and an ADR explicitly authorizing
RTL. Timing detail is refined only where it changes the architectural decision.
Retired RTL is a historical reference only where contracts match.

See [Verification](VERIFICATION.md) for actual gates and
[Progress](PROGRESS.md) for current evidence and limitations.
