# HolonNPU Progress

Last updated: 2026-09-04.

## Current Status

The programmable NPU single-mainline convergence remains the released `v2.0`
baseline. The v2.x Simulation Foundation semantic, gem5 fast, bare-metal, and
Linux full-system paths are implemented and passing. Broader calibration and
sensitivity workloads remain active.

- `master` contains one canonical product rooted at `npu_top`.
- Public contract is ABI 3.0 and Holon ISA 1.0.
- The former descriptor-driven product is retained only by tag `v1.5`.
- No old ABI headers/schema, descriptor path, product target, compatibility
  alias, or architecture-versioned product identifier remains.
- All 21 product RTL source targets are reachable from the canonical top.

## Implemented Product

- AXI-Lite lifecycle, capability, IRQ, fault, debug, and performance control.
- ABI 3.0 program descriptor validation and code/argument loading.
- Local program memory and data scratchpad with interface-native arbitration.
- Replaceable frontend contract and reference Holon ISA frontend.
- Scalar control, predicate, CSR/debug, sync, and system instruction paths.
- Integer/quant VLA vector engine.
- B-weight-stationary INT8 matrix micro-op engine with INT32 accumulation.
- Frontend-issued AXI4 DMA load/store and ordered completion records.
- AXI 4 KiB split support for descriptor, code, argument, DMA, and completion
  transfers.
- Observable software reset through `RESETTING` and accepted-work drain.
- Fixed no-operand `SYSTEM_FAULT` semantics.
- Program-level result observation through DMA store rather than product test
  probes.

## Contract And Engineering State

- `spec/holon_npu_abi.json` is the only ABI source.
- `spec/holon_npu_isa.json` owns ISA version, encoding, and operation classes.
- ABI generation consumes both schemas and produces canonical RTL/C/reference
  artifacts.
- Product RTL uses SystemVerilog interfaces internally; wrappers live only in
  `sim/rtl/`.
- C23, C++26, CMake 4.0, target-scoped source ownership, and minimal presets are
  enforced.
- Native SVA remains active during software-reset drain.
- Coverage evidence is recorded at verified monitor/scoreboard events.

## Release Verification

Fresh configurations were used on 2026-07-22.

| Gate | Result |
| ---- | ------ |
| ABI generation | `python3 tools/gen_abi.py --check` passed |
| ISA generation | `python3 tools/gen_isa.py --check` passed |
| ISA metadata | `python3 tools/check_isa.py` passed |
| ABI schema | `python3 tools/check_abi.py` passed |
| RTL ownership/interfaces | 21 product targets reachable; passed |
| Macro policy | passed |
| Debug | `23/23` passed |
| RTL lint | `11/11` passed |
| Regression | `34/34` passed |
| Coverage preset | `36/36` passed |
| Coverage evidence | 12 raw files, 137/137 functional events, 56/56 RTL covers |

Structural coverage:

| Metric | Result | Baseline |
| ------ | ------ | -------- |
| Line | 65.1% (958/1472) | 65% |
| Branch | 61.0% (1126/1846) | 60% |
| Toggle | 31.4% (28885/91908) | 31% |
| Expression | 54.6% (1052/1925) | 54% |

FSM is not assigned a threshold because Verilator reports no FSM denominator.

## Simulation Foundation Verification

Fast gates verified locally on 2026-09-04; Linux evidence is from 2026-07-22
and has not been rerun with the updated upstream/compiler:

| Gate | Result |
| ---- | ------ |
| Semantic core | migrated model/runtime/frontend differential tests passed; 13/13 typed required events observed |
| Typed completion protocol | wrong, duplicate, and invalid completions rejected transactionally |
| gem5 upstream | official `stable` SHA `cbc94c1a773e94118070294750dbe2c9c75898cb` |
| C++ standard audit | 27,542/27,542 translation units use effective C++26 |
| gem5 build | complete `RISCV/gem5.opt` built with 16 SCons jobs |
| gem5 fast gate | `6/6` passed |
| RISC-V bare-metal | vector, matrix, DMA, completion, IRQ, fault, and reset passed |
| Idle checkpoint | separate capture/restore processes preserve descriptor, IRQ, and cycle state; a second program completes after restore |
| RISC-V Linux full-system | locked Ubuntu 24.04, Linux 6.8.12, matching module, DMA/IRQ smoke, and PASS sentinel completed in 1516.59 s |
| DMA profile | 256-byte maximum and 4 KiB splitting exercised at page-edge addresses |
| Timing calibration | vector config/ALU 1 cycle, 4-lane load/store 9 cycles, `2x2x2` matrix 93 cycles |

Build metadata, gem5 stats, and Linux resource/kernel/guest evidence are
generated under `build/gem5/`. The current build uses host GCC 16.2 and RISC-V
GCC 16.1. gem5 `stable` warns that host GCC 16.2 is newer than its listed support
range; the reviewed C++26 overlay builds successfully and the full compile
database is audited.

## Known Limits

- One active program and one synchronous engine command at a time.
- One active AXI transaction per DMA engine.
- Explicit scratchpad/DMA memory management; no coherent cache or IOMMU.
- Integer/quantized data paths only; no BF16 or FP8.
- No multiple contexts, program queues, graph scheduler, or multi-tile scaling.
- Formal verification, CDC signoff, synthesis timing, power, and physical design
  are not yet release gates.
- gem5 DMA callbacks do not expose an architectural bus-error response; system
  address faults remain covered by semantic/direct and RTL tests.
- Current timing calibration directly gates vector/matrix zero-stall latency;
  frontend, DMA setup, and full-program calibration need broader workloads.

## Next Work

Simulation Foundation follow-up is:

- extend calibration to frontend, loader, DMA setup, and representative whole
  programs without equating real memory latency to the RTL test memory;
- add representative sensitivity workloads.

New architecture features remain blocked from RTL until their semantic, gem5,
workload, cost, and ADR evidence passes the simulator-first gate. This page
records only verified results; incomplete work is never counted as evidence.
