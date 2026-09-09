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
6. Develop one reviewable feature at a time. After its acceptance commands pass,
   update `docs/PROGRESS.md` and `CHANGELOG.md`, commit the feature immediately,
   and start the next feature only from a clean tracked worktree.

Do not accumulate completed features as one long-lived uncommitted change. A
feature commit must contain its implementation, tests, generated artifacts, and
current-state documentation together. Build products and external dependencies
remain ignored and are never committed.

New architecture behavior is simulator-first. It must progress through:

1. requirements, workload, and measured bottleneck;
2. ISA/ABI proposal and at least two alternatives;
3. C++26 Holon semantic core implementation and tests;
4. autonomous Holon gem5 execution, timing, and memory-system evaluation;
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

Goal: a self-hosted NPU that runs its complete program without a Host CPU,
doorbell, or gem5 device driver. ADR-0058 supersedes the Host-based destination
of the earlier foundation; the released accelerator RTL contract is unchanged.

Implementation order and acceptance:

1. Completed autonomous functional execution: validated cold boot into `program_machine`,
   external system memory, precise typed completion, resumable execution
   budgets, and deterministic control/vector/matrix/DMA programs. Reject invalid
   images without mutation; no descriptor or `semantic::device` on this path.
2. Complete minimal Transformer: define the numeric contract before extending
   ISA metadata; execute attention, normalization, residuals, feed-forward,
   activation, and output computation as Holon program instructions. The initial
   fixed-shape execution acceptance is specified in `docs/TRANSFORMER_WORKLOAD.md`.
   It uses existing instructions and is part of the active execution cutover,
   not a separate ISA feature or authorization to skip consumer migration. Compare
   every stage with an independent mathematical reference, including random and
   numeric-edge inputs. Host-side orchestration/arithmetic between kernels is
   not evidence of self-hosted execution. BF16/FP8 are not prerequisites.
   Active prerequisite: redesign vector/matrix instruction contracts and adopt
   RV32IM + Zicsr scalar control with ILP32 and no C extension (ADR-0059,
   `docs/ISA_REDESIGN.md`). The envelope is 32-bit standard scalar plus fixed
   64-bit Holon instructions using reclaimed non-RVC prefixes. Preserve Holon
   VLA/predication principles; do not freeze the current narrow encoding
   by adding isolated FP32 opcodes. Then implement the unified semantic contract
   and compile a complete autonomous Transformer forward pass.
   Completed first slice (ADR-0060): machine-checkable mixed-width framing and
   RV32IM/Zicsr/MRET/WFI metadata, operand extraction, and disassembly, including
   upstream assembler/compiler cross-checks. This underpins standard scalar
   execution, M-mode state, ELF startup and the redesigned NPU operands.
   Current RTL capability generation stays unchanged until a reviewed cutover.
   Completed slice (ADR-0061): pure RV32 scalar effects shared with the current
   machine, typed memory/CSR/control requests and precise exceptions. Verified
   every selected opcode, arithmetic edges, source/destination aliasing,
   load completion and unchanged current-program results. This precedes
   physical memory routing and M-mode state, not a second interpreter.
   Completed slice (ADR-0062): a shared scalar hart state with standard machine
   CSRs, trap/MRET/WFI, interrupts and token-checked memory/fence completion.
   The current machine's scalar register/PC/retirement storage uses
   this state. Tests cover CSR WARL/read-only behavior, precise fault
   PC, counter writes/inhibition, interrupt boundaries and stale completions;
   physical routing and mixed-width execution are now verified below.
   Completed slice (ADR-0063): checked physical regions for local program memory,
   data scratchpad and environment-owned system memory, instruction fetch and
   synchronous servicing of the hart's captured memory/fence requests. Verified
   permissions, overflow, region/backing boundaries and compiled RV32 C23/C++26
   control code through the shared hart. Routing is integrated into the
   canonical machine and does not add another interpreter.
   Completed slice (ADR-0064): validated ELF32 executable loading by PT_LOAD,
   including RISC-V profile attributes, permissions, initialized bytes and BSS.
   Replaced raw-section extraction in the compiled scalar fixture. Bad images
   fail before memory mutation. Directed, sanitizer and deterministic mutation
   checks pass; the canonical machine now consumes the same load plans.
   Implemented slice (ADR-0065): expressive 64-bit NPU operand metadata and typed
   encode/decode. Freeze explicit vector length/predication, register-addressed
   matrix views and ordered DMA/stop contracts before their execution cutover.
   Check all reserved fields, operand domains, round trips and
   upstream raw-byte link preservation. No RTL capability changes in this slice.
   Verified execution checkpoint (ADR-0066): replaced the former decoder/program
   builder and migrated semantic program tests to the RV32/Holon instruction
   stream. The machine consumes the shared decoder, retires 4/8-byte instructions
   precisely and implements the new vector/matrix state. Public ISA/schema/RTL
   contract convergence remains open. No alternate bootable ISA, fallback or legacy translation facade is
   permitted. Old RTL tests cannot substitute for new semantic execution
   evidence, nor authorize unreviewed RTL changes.
3. Verified autonomous gem5 bring-up (ADR-0067): replaced the Host/DmaDevice path
   with a clocked Holon execution object and timing memory request port. The
   identical Transformer image runs without a Host CPU or MMIO launch sequence.
   The integration gate also covers executable system-memory fetch across a
   page boundary, non-aligned DMA tails, scalar memory and guest trap/MRET under
   timing backpressure. Bus-error injection and interrupt/WFI remain open.
4. Performance acceptance: account for frontend, local memory, vector, matrix,
   memory transfers, and synchronization on the actual event timeline. Report
   complete-program statistics and sensitivity (initial blocking model verified);
   complete detailed resource modeling and calibration before hardware predictions.
   Superseded Host-only sources, tests and CI paths have been removed. Do not
   maintain two product mainlines.

Each step is implemented, tested, documented, and committed separately. RTL and
released ABI 3.0/ISA 1.0 remain unchanged during the initial boot/runner step.
New numerical behavior requires schema/docs and semantic evidence before gem5;
no corresponding RTL is authorized by this roadmap alone.

The former accelerator's Host/MMIO/Linux/checkpoint evidence is historical and
retained in Git. It does not satisfy autonomous interrupt, checkpoint or timing
calibration acceptance. Current evidence and remaining gates are in Progress.

The semantic protocol is two-phase. `advance()` may retire internal work, emit
one typed pending operation, or report a terminal event. External work changes
architectural state only through the matching `complete(token, result)` call.
PC and `instret` remain at the precise instruction until successful completion.
System memory belongs to the runner or gem5, never to the semantic core.

Acceptance:

- autonomous direct execution and gem5 execute one semantic implementation;
- gem5 contains the only performance and full-system model;
- complete Transformer functional execution is independently checked;
- autonomous gem5 timing and memory-system tests are reproducible;
- result, fault and ordering have architectural scoreboards; RTL differential
  comparison/calibration applies only where contracts match, not between old
  accelerator encodings and the redesigned execution ISA;
- timing statistics identify frontend, DMA, scratchpad, vector, matrix, and
  synchronization costs separately;
- no future architecture phase begins before this foundation is accepted.

Non-goals: signal-level AXI simulation inside gem5, a second functional model,
new Host device features, or premature RTL numeric extensions.

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
integration must be modeled in the autonomous Holon gem5 system before public interfaces
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

As the autonomous foundation is implemented, its semantic, whole-workload,
gem5 execution, and timing gates become part of this release policy. Release status
and known limits belong in `docs/PROGRESS.md`; history belongs in
`CHANGELOG.md` and Git tags.

The current fast simulation gate is:

```bash
cmake --preset gem5
cmake --build --preset gem5 --parallel 16
ctest --preset gem5 --output-on-failure
```
