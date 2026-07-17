# HolonNPU Progress

Last updated: 2026-07-18.

## Current Status

The programmable NPU single-mainline convergence is complete and validated for
the `v2.0` release.

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

Fresh configurations were used on 2026-07-18.

| Gate | Result |
| ---- | ------ |
| ABI generation | `python3 tools/gen_abi.py --check` passed |
| ISA generation | `python3 tools/gen_isa.py --check` passed |
| ISA metadata | `python3 tools/check_isa.py` passed |
| ABI schema | `python3 tools/check_abi.py` passed |
| RTL ownership/interfaces | 21 product targets reachable; passed |
| Macro policy | passed |
| Debug | `22/22` passed |
| RTL lint | `11/11` passed |
| Regression | `33/33` passed |
| Coverage preset | `35/35` passed |
| Coverage evidence | 12 raw files, 137/137 functional events, 56/56 RTL covers |

Structural coverage:

| Metric | Result | Baseline |
| ------ | ------ | -------- |
| Line | 65.1% (958/1472) | 65% |
| Branch | 61.0% (1126/1846) | 60% |
| Toggle | 31.4% (28885/91908) | 31% |
| Expression | 54.6% (1052/1925) | 54% |

FSM is not assigned a threshold because Verilator reports no FSM denominator.

## Known Limits

- One active program and one synchronous engine command at a time.
- One active AXI transaction per DMA engine.
- Explicit scratchpad/DMA memory management; no coherent cache or IOMMU.
- Integer/quantized data paths only; no BF16 or FP8.
- No multiple contexts, program queues, graph scheduler, or multi-tile scaling.
- Formal verification, CDC signoff, synthesis timing, power, and physical design
  are not yet release gates.

## Next Work

After `v2.0`, prioritize larger random program differential testing, generated
assembler/disassembler diagnostics, synthesis/CDC planning, and measured
performance optimization behind the existing ISA ordering contracts.
