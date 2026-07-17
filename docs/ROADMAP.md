# HolonNPU Roadmap

`master` is the only active product line. This roadmap describes the current
programmable NPU architecture and its forward work. Historical source is
preserved by Git tags and release notes rather than parallel directories,
targets, schemas, or compatibility aliases.

## Engineering Discipline

Before implementation:

1. Read this file and `docs/PROGRESS.md`.
2. Update architecture, interface, ISA, or an ADR before changing a contract.
3. Change ABI/ISA metadata through the canonical schemas.
4. Define acceptance tests and coverage evidence for the intended behavior.

A phase is complete only when implementation, generated artifacts, native SVA,
tests, coverage, and current-state documentation agree. Product RTL must be
reachable from `npu_top`; simulation-only use does not justify product code.

## Product Baseline

The current mainline is a programmable integer/quant NPU tile with:

- ABI 3.0 program submission and lifecycle control;
- Holon ISA 1.0;
- replaceable frontend microarchitecture;
- local program memory and data scratchpad;
- frontend-issued DMA, vector, matrix, and synchronization work;
- integer/quant vector execution;
- B-weight-stationary INT8 matrix execution;
- completion records, IRQ, debug, faults, and performance counters;
- schema-generated ABI/ISA contracts;
- native SVA and evidence-driven coverage gates.

The prior GEMM-only generation is archived at tag `v1.5`.

## Active Phase: Single-Mainline Release Hardening

Goal: make the programmable architecture the only coherent product before its
first release.

Deliverables:

- remove all old descriptor-driven product RTL, software, tests, and build
  targets from `master`;
- canonical product names without architecture-version prefixes;
- canonical ABI and ISA schemas/generators;
- AXI 4 KiB split correctness for loader, DMA, and completion writeback;
- observable safe software reset with transaction drain;
- fixed `SYSTEM_FAULT` semantics;
- no product test probes or simulation-only product consumers;
- event-driven functional coverage, named RTL cover gating, exact raw artifact
  set checking, and structural coverage baselines;
- current-state architecture, ISA, interface, verification, and onboarding
  documentation.

Acceptance:

- debug, lint, regression, and coverage presets pass from clean build trees;
- all required functional events and product RTL cover properties are hit;
- line/branch/toggle/expression coverage meets or exceeds the checked baseline;
- all AXI bursts stay within one 4 KiB page under directed boundary tests;
- software reset drains AR/R/AW/W/B and local-memory work under backpressure;
- schema generation, ISA metadata, macro policy, RTL ownership, canonical naming,
  JSON, and whitespace checks pass;
- no current product file, public symbol, CMake target, or test has an
  architecture-version compatibility prefix.

Risks:

- soft-reset bugs may only appear under channel-specific stalls;
- structural coverage may expose untested error-state transitions;
- canonical deletion can leave stale CMake sources or generated references;
- firmware-visible ordering must not depend on current single-command timing.

## Next Phase: Program And Runtime Hardening

Goal: expand program-level confidence without changing the architecture.

Planned work:

- larger deterministic random Holon programs differential-tested against the
  C++ architectural model;
- randomized control flow, predicate tails, quantization boundaries, DMA page
  splits, and matrix tile traversal;
- public assembler/disassembler diagnostics generated from ISA metadata;
- example programs for vector add, requantization, reduction, transpose, and
  tiled GEMM;
- firmware image/version tooling and completion-debug utilities.

Acceptance:

- every implemented ISA opcode has directed and constrained-random execution;
- model and RTL agree on retirement, fault PC, local memory, and system-memory
  effects;
- examples execute through the public runtime and product top.

## Performance Phase

Goal: improve throughput without changing program semantics.

Candidate work, in order of measured value:

- queue multiple frontend DMA commands while preserving in-order events;
- permit multiple AXI outstanding transactions with explicit ownership tags;
- pipeline vector issue and local-memory access;
- overlap DMA, vector, and matrix execution behind existing sync contracts;
- add banked local memory only when conflict measurements justify it.

Each optimization requires unchanged architectural-model results, new protocol
assertions, backpressure/reset tests, and updated coverage baselines. No
performance mechanism becomes software-visible unless deliberately added to ABI
capabilities.

## Future Data Types

BF16 and FP8 are future architecture work, not dormant implementation in the
current tree. Before implementation they require:

- arithmetic and exception semantics;
- vector/matrix encoding allocation;
- scale metadata and capability ABI;
- C++ reference semantics;
- accuracy, edge-case, and coverage plans.

## Future System Features

IOMMU integration, multiple contexts, multiple program queues, coherent memory,
graph scheduling, and multi-tile scaling remain out of scope until their
security, ordering, isolation, and software contracts are designed.

## Release Policy

A release candidate requires:

```bash
python3 tools/gen_abi.py --check
python3 tools/gen_isa.py --check
python3 tools/check_isa.py
python3 tools/check_rtl_interface_usage.py
python3 tools/check_macro_policy.py
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug --output-on-failure
ctest --preset lint --output-on-failure
cmake --preset regression
cmake --build --preset regression --parallel 2
ctest --preset regression --output-on-failure
cmake --preset coverage
cmake --build --preset coverage --parallel 2
ctest --preset coverage --output-on-failure
python3 tools/check_coverage.py --build-dir build/coverage
git diff --check
```

Release status and known limits are recorded in `docs/PROGRESS.md`; long-term
history belongs in `CHANGELOG.md` and Git tags.
