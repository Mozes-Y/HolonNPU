# HolonNPU Progress

Last updated: 2026-09-05.

## Current Status

The programmable NPU single-mainline convergence remains the released `v2.0`
baseline. The v2.x Simulation Foundation semantic, gem5 fast, bare-metal, and
Linux full-system paths are implemented. The active destination is now
self-hosted execution (ADR-0058), not the existing Host/DmaDevice system.
Autonomous functional boot and budgeted execution are implemented and verified;
complete Transformer execution and autonomous gem5 performance modeling are
not yet implemented.

Active prerequisite: confirmed RV32IM + Zicsr, ILP32, no C extension, and a
coordinated vector/matrix ISA redesign (ADR-0059). The selected envelope is
32-bit standard scalar plus fixed 64-bit Holon instructions in reclaimed non-RVC
space. Operand/opcode details are not frozen. No decoder, schema, or hardware
capability has been changed at this architecture checkpoint.
The autonomous boot/runner feature is committed as `84551c8` and remains the
functional migration starting point.

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

Fast gates verified locally on 2026-09-05; Linux evidence is from 2026-07-22
and has not been rerun with the updated upstream/compiler:

| Gate | Result |
| ---- | ------ |
| Semantic core | migrated model/runtime/frontend differential tests passed; 13/13 typed required events observed |
| Typed completion protocol | wrong, duplicate, and invalid completions rejected transactionally |
| gem5 upstream | official `stable` SHA `cbc94c1a773e94118070294750dbe2c9c75898cb` |
| C++ standard audit | 27,551/27,551 translation units use effective C++26 |
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

## Autonomous Execution Verification

Completed on 2026-09-05: transactional cold boot, non-recycled program tokens,
caller-owned mapped memory, and resumable budgeted execution directly on
`program_machine`. No descriptor/device is used by this entry point. Shared
memory service preserves accelerator differential tests without duplicating ISA
arithmetic. RTL, schemas, generated headers, and the driver are unchanged.

| Command/gate | Result |
| ------------ | ------ |
| `cmake --build --preset debug --parallel 2` | passed |
| `ctest --preset debug --output-on-failure` | 24/24 passed |
| `ctest --preset lint --output-on-failure` | 11/11 passed |
| `ctest --preset debug -R '^holon_npu_execution$' --verbose` | boot/errors/tokens/budgets, 64 random vector programs, four tiled GEMM shapes passed |
| `ctest --test-dir build/semantic-sanitize -R '^holon_npu_(execution\|semantic\|runtime)$' --output-on-failure` | 3/3 passed with ASan/UBSan |
| New execution sources `-Wall -Wextra -Wpedantic -Werror` | passed |
| `cmake --build --preset gem5 --parallel 16` | passed; current stable SHA above |
| `ctest --preset gem5 --output-on-failure` | 6/6 passed |
| ABI/ISA generation, ISA metadata, ownership, macro policy, `git diff --check` | passed |

Regression/coverage and Linux FS were not rerun for this feature; their older
results above are not evidence of a new release gate. ASan/UBSan was configured
in ignored `build/semantic-sanitize` with RTL disabled; no preset was added.

## ISA Direction Checkpoint

Confirmed on 2026-09-05: RV32IM + Zicsr / ILP32 without C, 32-bit standard scalar
instructions, and fixed 64-bit Holon NPU instructions using reclaimed non-RVC
prefixes. ADR-0059 and `docs/ISA_REDESIGN.md` define the remaining contract work.

- Local GCC/G++ 16.1 compile/link probe passed with `-march=rv32im_zicsr`
  and `-mabi=ilp32`, C23/C++26, freestanding code, and relaxation disabled.
- `readelf -h -A` verified ELF32 little-endian RISC-V, flags `0x0`, the requested
  extension attributes, and 16-byte stack alignment. `objdump -d -s` showed
  32-bit scalar calls/branches/MUL/DIVU and unchanged 8-byte raw payload.
- The probe lives in ignored `build/rv32-contract-probe`; it is manual toolchain
  feasibility evidence, not a persistent CI gate or an executable Holon opcode.
- ABI/ISA generation and metadata, ownership, macro policy, and
  `git diff --check` passed. No implementation/build files changed; full RTL and
  gem5 suites were not rerun for this documentation checkpoint.

RV32 semantic execution, ELF startup, and Holon 64-bit decoding are still
unimplemented. Successful compilation is not evidence of those capabilities.

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

Simulation Foundation proceeds through autonomous boot and execution, complete
Transformer functional verification, then a no-Host gem5 execution model and
whole-program performance calibration. Each completed feature is tested,
documented, and committed before starting the next one.

New architecture features remain blocked from RTL until their semantic, gem5,
workload, cost, and ADR evidence passes the simulator-first gate. This page
records only verified results; incomplete work is never counted as evidence.
