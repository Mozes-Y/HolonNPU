# HolonNPU Progress

Last updated: 2026-09-06.

## Current Status

The programmable NPU single-mainline convergence remains the released `v2.0`
baseline. The v2.x Simulation Foundation semantic, gem5 fast, bare-metal, and
Linux full-system paths are implemented. The active destination is now
self-hosted execution (ADR-0058), not the existing Host/DmaDevice system.
Autonomous functional boot and budgeted execution are implemented and verified;
complete Transformer execution and autonomous gem5 performance modeling are
not yet implemented.

The target is RV32IM + Zicsr, ILP32, no C extension, single-hart M-mode, and
coordinated vector/matrix redesign (ADR-0059). Typed 32/64-bit framing and
standard scalar decoding are implemented under ADR-0060. ADR-0061 adds shared
scalar effect evaluation and typed memory/CSR/control requests. The canonical
ISA schema generates internal scalar metadata without changing RTL capability.
ADR-0062 adds shared M-mode state, trap/MRET/WFI, interrupts and transactional
scalar completion. ADR-0063 adds physical routing, parcel fetch and synchronous
memory servicing, verified with compiled RV32 C23/C++26 probes. The canonical
mixed-width program machine, NPU operands and ELF startup remain next;
the released RTL/ABI are unchanged.
The next scalar execution contract uses a confirmed unified 32-bit physical
address space with scalar access to mapped scratchpad/system memory and DMA
for bulk tensor movement. The checked router and hart completion exist; the
complete Holon program fetch/execute cutover is not implemented yet.
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
| C++ standard audit | 27,569/27,569 translation units use effective C++26 |
| gem5 build | complete `RISCV/gem5.opt` built with 16 SCons jobs |
| gem5 fast gate | `7/7` passed, including upstream scalar toolchain oracle |
| RISC-V bare-metal | vector, matrix, DMA, completion, IRQ, fault, and reset passed |
| Idle checkpoint | separate capture/restore processes preserve descriptor, IRQ, and cycle state; a second program completes after restore |
| RISC-V Linux full-system | locked Ubuntu 24.04, Linux 6.8.12, matching module, DMA/IRQ smoke, and PASS sentinel completed in 1516.59 s |
| DMA profile | 256-byte maximum and 4 KiB splitting exercised at page-edge addresses |
| Timing calibration | vector config/ALU 1 cycle, 4-lane load/store 9 cycles, `2x2x2` matrix 93 cycles |

Build metadata, gem5 stats, and Linux resource/kernel/guest evidence are
generated under `build/gem5/`. The current build uses host GCC 16.2 and RISC-V
GCC 16.2. gem5 `stable` warns that host GCC 16.2 is newer than its listed support
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

## Scalar Decode Foundation Verification

Completed on 2026-09-05: schema-generated scalar patterns, four-byte-aligned
little-endian 32/64-bit framing, typed operand extraction, and disassembly.
Tests observe all 56 RV32IM/Zicsr/MRET/WFI patterns, 57,344 deterministic operand
cases, and exhaustive I/S/B/J immediate reconstruction. Negative schema tests
cover overlap, malformed profiles, missing instructions, unchanged RTL/ABI
outputs, and unchanged-file timestamps during regeneration.

| Command/gate | Result |
| ------------ | ------ |
| `cmake --build --preset debug --parallel 2` | passed |
| `ctest --preset debug --output-on-failure` | 26/26 passed |
| `ctest --preset lint --output-on-failure` | 11/11 passed |
| New instruction sources `-Wall -Wextra -Wpedantic -Werror` | passed |
| Instruction test with ASan/UBSan | passed |
| `cmake --build --preset gem5 --parallel 16` | passed; stable SHA and C++26 audit above |
| `ctest --preset gem5 --output-on-failure` | 7/7 passed |
| Upstream GCC/G++ 16.2 toolchain oracle | 56/56 assembly encodings and real C23/C++26 output decoded |
| ABI/ISA generation, schema, ownership, macro policy, `git diff --check` | passed |

The repeatable toolchain gate replaces the earlier manual compiler probe;
artifacts live under `build/gem5/scalar-toolchain/`. Current RTL, public ABI
headers, and program-machine behavior are unchanged. Regression/coverage and
Linux FS were not rerun for this feature. RV32 execution, CSR/trap effects,
ELF startup, and NPU 64-bit opcode semantics remained unimplemented at that
checkpoint; framing and compilation are not evidence of those capabilities.

## Scalar Effects Verification

Completed and verified on 2026-09-05: ADR-0061 RV32 integer/branch results, physical load/store requests,
load completion, CSR/FENCE enables and synchronous trap/machine-control
requests. The existing program machine reuses this evaluator for MOVI/ADD/ADDI;
there is no additional interpreter, memory owner or timing model.

| Command/gate | Result |
| ------------ | ------ |
| `ctest --preset debug --output-on-failure` | 27/27 passed |
| `ctest --preset lint --output-on-failure` | 11/11 passed |
| `ctest --preset debug -R '^holon_npu_scalar$' --verbose` | 56/56 effects, 122,960 arithmetic scoreboard cases, exhaustive byte/halfword loads passed |
| `ctest --preset regression --output-on-failure` | 38/38 passed |
| `ctest --preset coverage --output-on-failure` | 40/40 passed |
| `python3 tools/check_coverage.py --build-dir build/coverage` | 12 raw files, 137/137 functional events, 56/56 product RTL covers; structural baseline unchanged |
| Scalar source/test strict warnings and ASan/UBSan | passed |
| `cmake --build --preset gem5 --parallel 16` | passed; current stable SHA above |
| `ctest --preset gem5 --output-on-failure` | 7/7 passed |

Debug and regression were configured and built with two jobs; coverage with
eight jobs. All builds passed. ABI/ISA
generation, schema, ownership, macro policy, Markdown links and whitespace
checks pass. Public generated headers, RTL, ABI schema and driver are unchanged.
GCC reduced variable-tracking detail in two large Verilator-generated coverage
functions; this is a debug-information note, not a warning in Holon sources.
Linux FS has not been rerun. This evidence does not establish physical routing,
M-mode CSR/trap commit, full RV32 boot or Transformer execution.

## Shared M-Mode State Verification

Implemented on 2026-09-06 under ADR-0062. `program_machine` owns one
`scalar::hart_state` for registers, PC and retirement; matching arithmetic
commits through it. Internal schema metadata defines machine CSR inventory,
reset values, masks and zero HPM ranges. No second interpreter was introduced.

| Command/gate | Result |
| ------------ | ------ |
| Debug configure/build; `ctest --preset debug --output-on-failure` | 28/28 passed |
| `ctest --preset lint --output-on-failure` | 11/11 passed |
| Regression configure/build; `ctest --preset regression --output-on-failure` | 39/39 passed |
| Coverage configure/build; `ctest --preset coverage --output-on-failure` | 41/41 passed |
| `python3 tools/check_coverage.py --build-dir build/coverage` | 12 raw, 137/137 functional, 56/56 product RTL covers; structural baseline unchanged |
| Hart strict warnings and ASan/UBSan | passed |
| `cmake --preset gem5`; `cmake --build --preset gem5 --parallel 16` | passed; 27,578/27,578 translation units audited as C++26 |
| `ctest --preset gem5 --output-on-failure` | 7/7 passed |

`holon_npu_hart` independently checks all 4096 CSR addresses, 8192 register
commits (seed `0x52454732`), 8192 CSR operations (seed `0x43535232`), precise
trap PC, MRET/WFI, interrupt priority, counter writes/inhibition, delayed
memory/fence results, invalid payloads/tokens and external-reset token lifetime.
ABI/ISA generation, schema, ownership, macro policy and local Markdown links
pass. These component results do not establish physical routing, complete RV32
boot, Transformer execution or autonomous gem5 performance modeling.
The gem5 build uses upstream stable
`cbc94c1a773e94118070294750dbe2c9c75898cb`. Existing warnings about GCC 16.2
outside upstream's supported compiler range and optional HDF5 support remain
unchanged. Linux FS was not rerun. Debug/regression builds used two jobs;
coverage used eight and gem5 used sixteen. Public generated headers, RTL,
ABI schema and driver have no changes in this feature.

## Physical Routing Verification

Implemented on 2026-09-06 under ADR-0063. Maps own no memory; typed regions
select local program bytes, scratchpad or identity-mapped external system
memory. Reads/writes validate permissions, region and backing bounds before
mutation. Fetch reports the failing parcel separately from the instruction PC.
The synchronous service consumes only the hart's live pending operation.

| Command/gate | Result |
| ------------ | ------ |
| Debug configure/build; `ctest --preset debug --output-on-failure` | 29/29 passed |
| `ctest --preset lint --output-on-failure` | 11/11 passed |
| Regression configure/build; `ctest --preset regression --output-on-failure` | 40/40 passed |
| Coverage configure/build; `ctest --preset coverage --output-on-failure` | 42/42 passed |
| `python3 tools/check_coverage.py --build-dir build/coverage` | 12 raw, 137 functional events, 56 product RTL covers; structural baseline unchanged |
| New memory/execution/hart/scalar sources and memory test: strict warnings, ASan/UBSan | passed, including both compiled probes |
| `cmake --preset gem5`; `cmake --build --preset gem5 --parallel 16` | passed; 27,587/27,587 translation units audited as C++26 |
| `ctest --preset gem5 --output-on-failure` | 7/7 passed |
| `ctest --preset gem5 -R '^scalar_toolchain_check$' --verbose` | 56/56 encoding oracle plus both executed RV32 probes passed |

The memory test verifies directed permission, ownership, fetch and completion
cases plus 8192 interval-scoreboard cases (seed `0x4d415033`). The toolchain
fixture compiles the same control workload in C23 and C++26 and verifies all
16 values, checksum 261 and publication before WFI wait. C23 retires 536
instructions with 110 stack/35 system accesses; C++26 retires 737 with 174/35,
including a compiler-generated memset executed by the guest. These are
instruction/access counts, not cycle or performance measurements.

Portable guest memory support moved to `sim/guest/freestanding.c` without
duplication; CI watches the guest/probe sources and uploads toolchain artifacts.
ABI/ISA generation, ownership, macro policy, workflow YAML parsing, local links
and whitespace checks pass. Public RTL, ABI/ISA schemas, headers and driver
are unchanged. No production ELF loader, second interpreter or autonomous
gem5 model is claimed by these component integration tests.
gem5 remains on stable `cbc94c1a773e94118070294750dbe2c9c75898cb` with the
existing GCC-version and optional-HDF5 warnings. Linux FS was not rerun.
Debug/regression builds used two jobs, coverage eight, and gem5 sixteen.
Workflow YAML was parsed locally; GitHub Actions itself has not been run for
this unpushed feature. `actionlint` is not installed in this environment.

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
