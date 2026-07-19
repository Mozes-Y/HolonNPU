# HolonNPU Simulation Contract

HolonNPU follows a simulator-first architecture process. The simulator is not a
late validation aid: it is the required executable specification and performance
evaluation platform used before new architecture behavior may enter RTL.

This document defines the planned simulation foundation. The current repository
contains the C++26 architectural model but does not yet contain the gem5
integration described below.

## One Semantics, Two Entry Points

```text
ABI and ISA schemas
        |
        v
Holon semantic core
        |-----------------------------|
        v                             v
fast direct runner             Holon gem5 SimObject
semantic/random tests          timing and full-system tests
                                      |
                                      v
                               architecture review
                                      |
                                      v
                                     RTL
```

The direct runner and gem5 integration execute the same semantic core. They are
not separate functional models. gem5 is the only performance and full-system
model.

## Holon Semantic Core

The semantic core is a deterministic, gem5-independent C++26 library. It owns:

- ISA decode, legality, retirement, PC, and precise fault behavior;
- scalar, predicate, vector, matrix, CSR, and lifecycle architectural state;
- integer, quantized, and future numeric semantics;
- local-memory and DMA architectural ordering;
- typed issue, effect, token, completion, and fault contracts.

It does not own cycle counts, bandwidth, cache/DRAM behavior, AXI signals, RTL
state, or gem5 event scheduling. A fast runner may complete typed requests
synchronously so large directed and constrained-random suites remain practical.

Project code targets C++26 and follows modern type-safe practice: strong domain
types, concepts, RAII, standard vocabulary types such as `std::expected`,
`std::variant`, and `std::span`, and deterministic ownership. Project-defined
behavior macros and stringly typed protocol contracts are forbidden.

## Holon gem5 SimObject

The Holon gem5 SimObject consumes the semantic core and adds system context. It
is integrated as an external gem5 component through `EXTRAS` and provides:

- ABI MMIO register behavior, DMA requestor access, and IRQ delivery;
- frontend issue/retirement timing;
- DMA queues, transaction latency, and memory-system contention;
- vector and matrix pipeline occupancy and throughput;
- scratchpad ports, banks, conflicts, and arbitration;
- synchronization stalls and resource dependencies;
- structured statistics for workload and sensitivity analysis.

The timing model is cycle-accounted and event-driven. It models architecturally
relevant resources, queues, contention, and bandwidth, but not per-signal AXI
handshakes. Signal-accurate protocol verification remains an RTL responsibility.

The default system uses a RISC-V Host. Device and bare-metal tests form the
normal development gate; RISC-V Linux full-system workloads run in nightly and
release evaluation. Arm and x86 Host configurations are not maintained without
a separately approved requirement.

## gem5 And Toolchain Policy

- Use the upstream gem5 `stable` branch.
- Build the complete gem5 binary, Holon extension, and shared semantic core in
  C++26 mode; verify the effective standard and fail rather than silently lower
  it for any simulator source.
- Record the exact gem5 commit SHA, compiler identity, model parameters, system
  configuration, workload revision, and random seed with every result.
- Run compatibility CI against the current upstream `stable` branch.
- Resolve upstream incompatibility explicitly; do not switch branches or fork a
  second semantic implementation as a workaround.

Because `stable` advances, an architecture report is reproducible by its
recorded commit and configuration rather than by the branch name alone.

## Verification Tiers

| Tier | Required evidence |
| ---- | ----------------- |
| Semantic | Directed, property-based, and deterministic random tests of exact architecture behavior. |
| gem5 device | MMIO, DMA, IRQ, lifecycle, fault, and completion behavior using the shared core. |
| gem5 timing | Queue, pipeline, bank, bandwidth, contention, ordering, and sensitivity results. |
| RISC-V bare-metal | Driver and end-to-end program execution in the normal development gate. |
| RISC-V Linux full-system | OS, driver, memory-system, interrupt, and representative workload behavior. |
| RTL differential | Architectural effects match the semantic core; measured timing calibrates gem5 parameters. |

Functional correctness is necessary but does not authorize RTL. New behavior
also needs a measured workload benefit and a reviewed implementation tradeoff.

## Architecture-To-RTL Gate

An RTL implementation may begin only after an ADR accepts evidence containing:

1. the workload and quantified limitation;
2. proposed ISA/ABI semantics and alternatives;
3. passing semantic-core tests;
4. passing gem5 device and applicable RISC-V system tests;
5. cycle, bandwidth, utilization, and sensitivity measurements;
6. software, RTL, verification, and migration costs;
7. explicit RTL acceptance criteria and coverage requirements.

After RTL exists, Verilator and implementation measurements calibrate gem5.
Material divergence is resolved in the semantic contract, timing parameters, or
RTL; it is never hidden by independent expected-result code.

Behavior-preserving RTL bug fixes and optimizations may use the existing model
contract. Any software-visible behavior, result, ordering, fault, capability, or
performance mechanism that changes the architecture must pass the full gate.
