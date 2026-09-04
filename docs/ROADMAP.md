# HolonNPU Roadmap

`master` is the only active product line. This file is the authority for future
architecture work. Released behavior is described by the current architecture,
ISA, interface, and verification documents; Git tags preserve historical source.

Future items in this roadmap are research candidates, not implemented
capabilities or frozen ABI/ISA commitments.

## Engineering Discipline

Before implementation:

1. Read this file, `docs/PROGRESS.md`, and `docs/SIMULATION.md`.
2. Define the target workload and quantify the limitation in the current
   architecture.
3. Update architecture, interface, ISA, or an ADR before changing a contract.
4. Change ABI/ISA metadata only through the canonical schemas.
5. Define simulator evidence, acceptance tests, and coverage before RTL work.

New architecture behavior is simulator-first. It must progress through:

1. requirements, workload, and measured bottleneck;
2. ISA/ABI proposal and at least two alternatives;
3. C++26 Holon semantic core implementation and tests;
4. gem5 device, timing, and RISC-V system evaluation;
5. performance sensitivity, software cost, RTL cost, and verification report;
6. an accepted ADR authorizing RTL implementation;
7. RTL differential verification and gem5 calibration.

Behavior-preserving RTL fixes may proceed against existing model semantics. They
must not introduce new architectural behavior without completing this gate.

## Released Baseline: v2.0

The current product is a programmable integer/quant NPU tile with:

- ABI 3.0 program submission and lifecycle control;
- Holon ISA 1.0 and a replaceable frontend implementation;
- explicit program memory, data scratchpad, and ordered DMA;
- integer/quant VLA vector execution;
- B-weight-stationary INT8 matrix execution;
- completion, IRQ, debug, fault, and performance facilities;
- schema-generated ABI/ISA contracts;
- native SVA and evidence-driven coverage gates.

The descriptor-driven GEMM generation is archived at tag `v1.5`.

## v2.x: Simulation Foundation (Active)

Goal: establish the mandatory architecture-exploration platform before adding
new product behavior.

Implemented foundation:

- one deterministic C++26 Holon semantic core under `sim/semantic/`;
- expose typed issue/effect/completion contracts without gem5 or RTL coupling;
- retain a fast direct runner for semantic and constrained-random tests;
- integrate the same core into a gem5 external SimObject with MMIO, DMA, IRQ,
  engine timing, local-memory resources, and statistics;
- provide a RISC-V device/bare-metal configuration for normal development;
- execute a locked Ubuntu 24.04/Linux 6.8.12 full-system configuration with a
  matching C23 driver workload and retained evidence;
- build the complete gem5 simulator and Holon extension in verified C++26 mode;
- calibrate zero-stall vector/matrix engine timing against RTL module tests and
  record the exact gem5 `stable` commit, C++26 toolchain, model parameters, and
  workloads.

Remaining foundation follow-up:

- broaden RTL calibration beyond current vector/matrix issue-to-event latency;
- add idle-state checkpoint and sensitivity workloads.

Implementation sequence:

1. completed: replace `holon_npu::model` with the C++26
   `holon_npu::semantic` core and direct runner;
2. completed: add the reproducible upstream `stable` gem5 build and C++26 audit;
3. completed: integrate functional MMIO/DMA/IRQ behavior and cycle accounting;
4. completed: add RISC-V bare-metal tests to the fast gem5 gate;
5. completed: run locked-resource Linux full-system tests and retain their
   resource, kernel, guest, terminal, and simulator evidence;
6. active: extend current vector/matrix zero-stall calibration coverage.

The semantic protocol is two-phase. `advance()` may retire internal work, emit
one typed pending operation, or report a terminal event. External work changes
architectural state only through the matching `complete(token, result)` call.
PC and `instret` remain at the precise instruction until successful completion.
System memory belongs to the runner or gem5, never to the semantic core.

Acceptance:

- the direct runner and gem5 device execute one semantic implementation;
- gem5 contains the only performance and full-system model;
- semantic, device, timing, and bare-metal tests are reproducible;
- Linux full-system is reproducible before this phase is marked complete;
- result, fault, and ordering behavior matches current RTL;
- timing statistics identify frontend, DMA, scratchpad, vector, matrix, and
  synchronization costs separately;
- no future architecture phase begins before this foundation is accepted.

Non-goals: signal-level AXI simulation inside gem5, a second functional model,
or new product ABI/ISA behavior.

## v2.x: Program And Runtime Hardening

Goal: build representative software and measurements before selecting hardware
optimizations.

Planned work:

- larger deterministic random programs and metadata-driven diagnostics;
- assembler/disassembler support and reproducible program images;
- vector, requantization, reduction, transpose, and tiled GEMM workloads;
- profiling of launch, instruction, DMA, scratchpad, engine, and synchronization
  overhead in gem5;
- sensitivity studies for DMA concurrency, engine overlap, frontend issue, and
  local-memory organization.

Candidate mechanisms such as queued DMA, multiple outstanding transactions,
banked scratchpad, or engine overlap enter RTL only when measurements identify
the bottleneck and an ADR selects them. Firmware-visible ordering must remain
independent of implementation timing.

## v3: Transformer And BF16 Exploration

Problem to study: future Transformer workloads may require wider dynamic range,
higher matrix utilization, and more efficient attention and normalization data
movement than the integer/quant baseline provides.

Candidate research:

- BF16 vector/matrix arithmetic and mixed accumulation;
- batched GEMM and QKV/attention dataflows;
- softmax, normalization, activation, and reduction assistance;
- local-memory bandwidth and synchronization changes.

Before selection, representative prefill, decode, sequence-length, batch, and
model-size workloads must quantify utilization, capacity, bandwidth, numerical
accuracy, and software overhead. Programmable kernels, helper instructions, and
dedicated resources must be compared before any opcode or ABI is frozen.

## v4: Modern Low-Precision Exploration

Problem to study: emerging models may benefit from lower precision only when
scaling metadata, conversion overhead, accumulation accuracy, and memory traffic
are treated as one system.

Candidate research:

- FP8 E4M3/E5M2 semantics;
- per-channel, per-block, and MX/block scaling;
- scale metadata movement and storage;
- conversion, rounding, saturation, and mixed accumulation.

Semantic-core accuracy studies and gem5 workload measurements must establish a
quality/performance/energy case before a format or metadata path is selected.
No dormant FP8 or scaling RTL is allowed.

## v5: System Scaling Exploration

Problem to study: multi-program throughput, isolation, virtual memory, and
multi-tile scaling may require system facilities beyond the single-program tile.

Candidate research:

- multiple queues and contexts;
- IOMMU and address-translation integration;
- additional outstanding DMA and local-memory partitioning;
- inter-tile communication, synchronization, and scheduling;
- coherence only if platform workloads demonstrate a requirement.

Security, ordering, isolation, recovery, software ownership, and operating-system
integration must be modeled in RISC-V full-system gem5 before public interfaces
or RTL are approved.

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

Once the simulation foundation exists, its semantic, gem5 device, timing, and
required RISC-V system gates become part of this release policy. Release status
and known limits belong in `docs/PROGRESS.md`; history belongs in
`CHANGELOG.md` and Git tags.

The current fast simulation gate is:

```bash
cmake --preset gem5
cmake --build --preset gem5 --parallel 16
ctest --preset gem5 --output-on-failure
```
