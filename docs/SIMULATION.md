# HolonNPU Simulation Contract

## Status And Ownership

The 3.0 mainline transition retains the autonomous C++26 semantic implementation
and retires the old RTL and gem5 integration. Until the transition is complete,
those sources may still exist; they are not an approved direction for new work.
The independent performance simulator described below is planned, not built.

- Semantic core: instruction decode, architectural state, numerical results,
  traps, memory effects and ordering. System memory belongs to the environment.
- Direct runner: synchronous completion for fast functional tests; no separate
  instruction semantics or performance predictions.
- Performance model: future finite-resource/event scheduling over shared
  semantic effects. It owns modeled time, pipelines, ports, bandwidth and stalls.
- Workload software: real RV32/Holon programs, not Host callbacks implementing
  tensor operators.
- Independent mathematical references: test oracles, never execution backends.

## Functional Baseline

`program_machine::advance()` and `complete(token, result)` implement one pending
operation with precise retirement and faults. This is a functional baseline, not
a throughput model. Decode, hart state, local/system memory and ELF validation
are shared by all functional entry points. The current matrix/vector contract
is documented in ISA, not derived from physical array dimensions.

## Research Method

The platform must compare architectures rather than bake in one task scheduler.
Keep numerical semantics shared. Microarchitecture alternatives preserve the
same observable contract; ISA/visibility/retirement alternatives must declare
their different contracts and be verified explicitly.

Use the least expensive fidelity that preserves the decision under study.
Dependence, finite capacity, backpressure, memory visibility and precise faults
are never silently removed. Refine banks, transport and cycle reservations when
those details affect placement or scheduling results. Do not build several
independent semantic implementations or sum post-hoc instruction latencies and
call that an overlapping execution model.

## Deterministic Parallelism

Support parallel experiments and, separately, multithreading within one run.
Host worker scheduling must not decide target arbitration. A fixed model,
configuration, workload and seed must produce identical architectural effects
and modeled cycles across worker counts. Shared resources need deterministic
ordering and causal synchronization; do not invent latency to create lookahead.
Measure simulator speed and simulated hardware speed separately.

## Workloads And Comparison

Start execution-driven with Transformer prefill/decode and KV-cache studies,
plus scalar, DMA, vector, matrix, reduction and transpose microbenchmarks.
Use controlled programs and real-model shape/operator cross-checks. Trained
weights and task accuracy are not implied by mathematical kernel correctness.

Compare latency/throughput under explicit compute, SRAM, ports and bandwidth
budgets, with compiler quality and numerical rules controlled. Include metadata
storage, queue/allocator throughput and transport overhead. Report uncertainty,
ranking stability and counterexamples; resource proxies are not silicon PPA.
Trace replay, multiple contexts and multi-chip modeling are not first-step goals.

## Research Records And Admission

Record source commit, compiler, model/configuration, workload, seed, worker
count, outputs and measurement provenance per experiment. Keep build artifacts
outside source control; retain compact reproducible inputs and conclusions.
The research note is a hypothesis source, not an implementation instruction.

RTL remains paused until an accepted ADR selects an architecture using semantic,
performance, software and implementation-cost evidence. Historical backend
results remain recoverable at the checkpoint in Roadmap; they do not validate
the future independent simulator.
