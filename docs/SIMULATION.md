# HolonNPU Simulation Contract

HolonNPU follows a simulator-first architecture process. The simulator is not a
late validation aid: it is the required executable specification and performance
evaluation platform used before new architecture behavior may enter RTL.

This document is the implementation contract for the active simulation
foundation. It defines the boundary that the C++26 semantic core, direct runner,
gem5 execution model, and system tests must preserve.

## Self-Hosted Direction

The target is autonomous Holon program execution, not a Host-controlled NPU
peripheral. Work proceeds in this order: functional boot/execution, a complete
minimal Transformer program, then a standalone gem5 timing system. ADR-0058
supersedes the earlier requirement for a RISC-V Host and `DmaDevice`.

ADR-0059 selects RV32IM + Zicsr, ILP32, and no C extension for the target
Holon scalar path. This is execution on Holon itself, not reintroduction of a
Host CPU. Vector/matrix instructions retain independent Holon encodings and
VLA/predicate principles; their redesign and the ELF/runtime contract are
reviewed in `docs/ISA_REDESIGN.md`. Standard scalar words are 32-bit and Holon
NPU instructions are fixed 64-bit using reclaimed non-RVC prefixes. Current
ISA 1.0 remains the implemented migration baseline until those semantics are
replaced and verified.

The target executes in single-hart M-mode, without U/S mode, MMU, or OS.
`holon_npu_instruction.hpp` separates typed frame recognition from standard
scalar opcode/operand decoding. The internal metadata is generated from the
canonical ISA schema's `semantic_frontend` section; it is not an
independently executable ISA mode or an advertised RTL capability. Program
execution/trap migration must consume this decoder rather than duplicate it.
The selected scalar address model maps scratchpad and system memory into one
32-bit physical space. Scalar system accesses will use the same typed external
completion boundary; system storage must not move into the semantic core.

`holon_npu_scalar.hpp` implements the scalar-effects stage (ADR-0061): standard
integer arithmetic, branch decisions, precise exception candidates and typed
load/store/CSR/fence/machine-control requests. It owns no state, run loop, memory
storage or timing. Existing scalar arithmetic already uses it; full RV32
machine cutover must consume these same effects. An evaluated memory or CSR
request is not evidence of successful access, retirement or trap handling.

The current accelerator adapter and Host tests remain useful for released RTL
differential verification during migration. They are not the target execution
path and must not become a second permanent product. Their replacement and
removal are gated together after autonomous gem5 validation.

### Autonomous Boot And Execution Contract

- A boot image supplies instruction words, an aligned in-range entry PC, active
  word-aligned nonzero local-memory size, and optional initial local data with a
  strong local address.
  It is a simulation input, not a new binary format or public descriptor ABI.
- Validate the entire image before changing machine state. Empty/oversized code,
  invalid entry, invalid memory capacity, and overflowing data ranges return a
  typed boot error. Boot with pending work is rejected; the environment must
  finish that operation or explicitly reset first.
- Successful cold boot clears scalar/vector/predicate/matrix state, scratchpad,
  faults, events, and retirement before loading the image and entering RUNNING.
  Predicate reset is all-active, vector length is zero. Tokens are not reused
  across boot/reset within a machine instance.
- `run_program` drives `program_machine` directly, using caller-owned system
  memory and the same `advance`/`complete` protocol. It does not instantiate the
  ABI device, fetch descriptors, require MMIO, or interpret arithmetic itself.
- An instruction budget is a simulator limit, not an architectural fault.
  Exhaustion returns a typed error without inventing a retired instruction or
  fault; the caller may resume. Memory errors retain precise fault PC/instret.
- gem5 will use this same boot contract and program protocol, but schedule
  completion through its event queue and timing memory port. It must not call
  the synchronous runner as a performance shortcut.

Transformer acceptance requires program-executed QKV projection, attention
scores/masking/softmax, value aggregation, output projection, residual paths,
normalization, feed-forward activation, and output projection. A separate
reference may calculate expected values but may not service missing semantic
operations. Numeric and ISA contracts are reviewed before those extensions.

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

### Precise Two-Phase Execution

`holon_npu::semantic::program_machine` exposes one execution protocol:

- `advance()` returns a retired event, one typed pending operation, or a
  terminal event;
- a pending operation carries a strong token and a `std::variant` operation;
- `complete(token, result)` is the only way external work commits;
- stale, duplicate, or out-of-order tokens return `std::expected` API errors
  and do not create architectural faults;
- PC and `instret` advance only after successful completion, preserving the
  exact faulting instruction for engine and DMA failures.

Typed operations cover descriptor/code/argument fetch, local scalar/vector
work, matrix work, program DMA, synchronization, and completion writeback. DMA
load data enters the core only in a completion payload. DMA store payload is
captured when issued so it remains stable while the environment delays it.

`holon_npu::semantic::device` owns ABI lifecycle, loader, completion, IRQ, and
safe reset behavior. `direct_runner` owns a byte-addressed system-memory image
and completes the same requests synchronously. Neither layer implements a
second copy of ISA arithmetic or fault semantics.

## Current Accelerator gem5 Adapter

This section describes the implemented migration baseline, not the autonomous
architecture destination. No new Host integration is required by ADR-0058.

The Holon gem5 SimObject consumes the semantic core and adds system context. It
is integrated as an external gem5 component through `EXTRAS` and provides:

- ABI MMIO register behavior, DMA requestor access, and IRQ delivery;
- frontend issue/retirement timing;
- DMA queues, transaction latency, and memory-system contention;
- vector and matrix pipeline occupancy and throughput;
- current single-port scratchpad service and operation traffic accounting;
- synchronization stalls and resource dependencies;
- structured statistics for workload and sensitivity analysis.

The timing model is cycle-accounted and event-driven. It models architecturally
relevant resources, queues, contention, and bandwidth, but not per-signal AXI
handshakes. Signal-accurate protocol verification remains an RTL responsibility.

Current defaults intentionally match the released single-command design: one
frontend operation, one pending engine operation, one DMA transaction, 16
vector lanes, a `16x16` matrix array, and two-phase scratchpad request/response
service. Vector and matrix Verilator module tests measure issue-to-event cycles
and compare them directly with the timing calculator. Bank conflicts and engine
overlap are not fabricated for the current unbanked, non-overlapped baseline;
they become model parameters only after a simulator-first architecture proposal.
Checkpoints are accepted only when the device is both `IDLE` and quiescent;
descriptor, IRQ enable/status, and software-visible cycle state are preserved.
Any pending semantic operation, DMA, or scheduled device event rejects capture.
The fast gem5 gate captures after a completed program with sticky IRQ state,
restores in a separate gem5 process, verifies the restored MMIO state, and
submits another program before reporting success.

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

The source checkout is created under the gem5 build tree from the official
repository and never reuses an arbitrary developer checkout. A reviewed overlay
selects C++26 for the complete gem5 binary. Build metadata records the checkout
SHA, overlay hash, compiler, effective standard, build type, and Host. A
compile-command audit rejects any effective C++17, C++20, or C++23 translation
unit. Overlay drift fails visibly when upstream `stable` changes.

The canonical functional device is a `DmaDevice` with a 4 KiB MMIO aperture at
`0x10010000`, RISC-V PLIC source `0x20`, one DMA requestor port, and a 1 GHz
default device clock. These are model defaults, not new ABI fields. MMIO is
aligned 32-bit little-endian and follows the generated ABI exactly. Device DMA
uses at most 256-byte transactions and never crosses a 4 KiB boundary.

The normal gem5 gate covers device and RISC-V bare-metal execution. Linux
full-system tests use locked gem5 resource metadata and are reserved for
nightly, release, or explicit manual jobs. The matching guest bundle is rebuilt
from the locked kernel recipe; the Linux platform driver is simulation-only and
does not establish a stable product userspace API.

Canonical commands are:

```bash
cmake --preset gem5
cmake --build --preset gem5 --parallel 16
ctest --preset gem5 --output-on-failure
```

The build uses an isolated upstream checkout, maps shared Holon sources into the
SCons variant tree without copying them, and defaults the upstream SCons build
to 16 jobs. Python bytecode output is disabled for build and run subprocesses,
so imported EXTRAS/configuration files cannot modify the source tree.
Debug/regression RTL tool dependencies are not required by this gem5-only
preset.

## Linux Full-System Gate

The Linux gate uses the reviewed Ubuntu 24.04 no-systemd workload, Linux
6.8.12, OpenSBI 1.3.1, and root partition `1`. `resources.lock.json` records
the exact URLs, final uncompressed sizes, and checksums. Resource preparation
is the only networked step; `holon_linux.py` constructs local gem5 resource
objects and never queries the online catalog.

Prepare the immutable system resources:

```bash
python3 tools/prepare_gem5_linux_resources.py \
  --resource-lock sim/gem5/resources.lock.json \
  --resource-directory build/gem5/gem5-resources
```

Build the exact Ubuntu source package and matching guest artifacts with GCC 15
and 16 workers:

```bash
python3 tools/prepare_gem5_linux_kernel.py \
  --compiler riscv64-linux-gnu-gcc-15 \
  --resource-lock sim/gem5/resources.lock.json \
  --output-dir build/gem5/linux-kernel \
  --jobs 16
python3 tools/build_gem5_linux_guest.py \
  --compiler riscv64-linux-gnu-gcc-15 \
  --kernel-build-dir build/gem5/linux-kernel/build \
  --kernel-metadata build/gem5/linux-kernel/kernel-metadata.json \
  --source-root . \
  --resource-lock sim/gem5/resources.lock.json \
  --output-dir build/gem5/linux-guest
```

Register and run only the long Linux test:

```bash
cmake --preset gem5 \
  -DHOLON_NPU_ENABLE_GEM5_LINUX=ON \
  -DHOLON_NPU_GEM5_LINUX_BUNDLE=$PWD/build/gem5/linux-guest/guest-bundle.sh \
  -DHOLON_NPU_GEM5_RESOURCE_DIRECTORY=$PWD/build/gem5/gem5-resources
ctest --test-dir build/gem5 -R '^gem5_riscv_linux$' --output-on-failure
```

The Linux gate uses an Atomic RISC-V Host CPU to keep OS and platform-driver
validation tractable and a deterministic 3 GiB simple-memory service model.
It is a functional OS gate, not a DRAM performance workload. Holon device
latency remains cycle-accounted by the SimObject and is independently gated by
timing tests and RTL calibration. Each run emits resource, kernel, guest,
configuration, statistics, terminal, and simulation metadata under
`build/gem5/`.

## Verification Tiers

| Tier | Required evidence |
| ---- | ----------------- |
| Semantic | Directed, property-based, and deterministic random tests of exact architecture behavior; all typed required events must be observed at successful invariants. |
| gem5 device | MMIO, DMA, IRQ, lifecycle, fault, and completion behavior using the shared core. |
| gem5 timing | Current resource occupancy, service latency, bandwidth, ordering, and sensitivity results. |
| RISC-V bare-metal | Driver and end-to-end program execution in the normal development gate. |
| RISC-V Linux full-system | OS, driver, memory-system, interrupt, and representative workload behavior. |
| RTL differential | Architectural effects match the semantic core; measured timing calibrates gem5 parameters. |

The semantic, baseline timing, gem5 device, and bare-metal tiers are implemented
in the normal `gem5` preset. The separate Linux gate passes with the locked
Ubuntu 24.04 image, Linux 6.8.12 matching module, C23 workload, DMA/IRQ
completion, and a unique guest sentinel. Queue-depth, bank-contention, and
broader sensitivity studies remain follow-up work.

The current semantic registry contains 13 required events. Each event is marked
where its assertions or scoreboard comparison succeeds, never as a test-exit
declaration. gem5 device and system gates separately require SimObject statistic
minimums and unique guest PASS sentinels, so either integration layer can fail
without being masked by semantic-core evidence.

Functional correctness is necessary but does not authorize RTL. New behavior
also needs a measured workload benefit and a reviewed implementation tradeoff.

## Architecture-To-RTL Gate

An RTL implementation may begin only after an ADR accepts evidence containing:

1. the workload and quantified limitation;
2. proposed ISA/ABI semantics and alternatives;
3. passing semantic-core tests;
4. passing autonomous gem5 execution and memory-system tests;
5. cycle, bandwidth, utilization, and sensitivity measurements;
6. software, RTL, verification, and migration costs;
7. explicit RTL acceptance criteria and coverage requirements.

After RTL exists, Verilator and implementation measurements calibrate gem5.
Material divergence is resolved in the semantic contract, timing parameters, or
RTL; it is never hidden by independent expected-result code.

Behavior-preserving RTL bug fixes and optimizations may use the existing model
contract. Any software-visible behavior, result, ordering, fault, capability, or
performance mechanism that changes the architecture must pass the full gate.
