# HolonNPU Progress

This page records the current implementation state and latest verified gate.
Git history and `CHANGELOG.md` retain detailed chronology.

## Current Status

- Last updated: 2026-07-16.
- Released baseline: v1.5, ABI 2.0 descriptor-driven INT8 GEMM.
- Development baseline: V2 programmable NPU tile, ABI 3.0 and Holon ISA 1.0.
- V2 roadmap phases V2.0 through V2.9 are implemented; release hardening is
  green in Debug, lint, Regression, and Coverage configurations.

## Implemented V2 Baseline

- `spec/holon_npu_isa.json` and `spec/holon_npu_v2_abi.json` own the V2 ISA,
  register map, program descriptor, capabilities, faults, flags, and completion
  record. Generated C23/SystemVerilog/docs outputs byte-check cleanly.
- The C++26 architectural simulator models descriptor loading, frontend state,
  local memory, DMA ordering, predicate/vector state, quant helpers, and matrix
  micro-ops. The public `holon_npu_runtime` target owns encoders,
  `program_builder`, capability metadata, and example kernels.
- `npu_v2_top` integrates ABI 3.0 AXI-Lite control, program loader, local
  program/data memory, reference Holon frontend, DMA, integer/quant vector
  helpers, B-weight-stationary matrix micro-ops, completion writer, and AXI
  arbitration.
- Frontend control supports scalar arithmetic, aligned local load/store,
  branches, read-only architectural CSR/debug state, precise fault state,
  halt/resume, debug stepping, and explicit backpressured sync-issue retirement.
- Frontend-issued DMA uses register-provided 64-bit system and 32-bit local
  addresses with 1-to-4096-word commands; integrated tests exercise addresses
  beyond the former 12-bit immediate range and reject wrapped system ranges.
- Descriptor validation rejects wrapped descriptor/code/argument/completion
  ranges and requires arguments plus the reserved frontend stack to fit within
  the requested local-memory allocation. RTL, driver, and model apply the same
  rules.
- Vector/helper execution supports signed/unsigned 8-, 16-, and 32-bit data,
  explicit predicates and tails, wrap/saturating arithmetic, compare/select,
  shifts, gather, zip/unzip, 4x4 transpose, reductions, and requantization.
- Matrix execution reuses the V1 systolic array behind a tile-level
  clear/accumulate/store micro-op contract; firmware-visible behavior does not
  depend on internal wavefront timing. The public runtime emits M/N/K tile
  traversal, including K-tile accumulation, for arbitrary valid GEMM shapes.
- Optional 32-byte completion records are acknowledged in system memory before
  terminal MMIO status or IRQ. AXI completion write failure becomes a precise
  `AXI_WRITE` fault.
- Product RTL is interface-native. Flattened SystemVerilog exists only at the
  SoC pin boundary or in simulation harnesses under `sim/rtl/`.
- Every product and harness SystemVerilog file has exactly one consumed
  semantic CMake source owner; direct interface dependencies are declared on
  the target that uses them.
- ISA metadata coverage names are checked against the typed C++ registry.
  Integrated tests execute deterministic random vector programs, random signed
  INT8 matrix tiles with padded strides, public example images, and
  `17x19x23`/`64x64x64` runtime-generated tiled GEMM programs against reference
  results.

## Latest Verification

| Gate | Result |
| ---- | ------ |
| Generated V1 ABI | `python3 tools/gen_abi.py --check` passed |
| Generated V2 ISA | `python3 tools/gen_isa.py --check` passed |
| ISA metadata | `python3 tools/check_isa.py` passed |
| Generated V2 ABI | `python3 tools/gen_v2_abi.py --check` passed |
| V2 ABI schema | `python3 tools/check_v2_abi.py` passed |
| RTL ownership | `python3 tools/check_rtl_interface_usage.py` passed |
| Macro policy | `python3 tools/check_macro_policy.py` passed |
| Whitespace | `git diff --check` passed |
| Debug build/tests | Build passed; `30/30` tests passed |
| RTL lint | `18/18` tests passed |
| Regression | RelWithDebInfo build passed; `50/50` tests passed |
| Coverage | Instrumented build passed; `51/51` tests passed |
| Coverage gate | `21` raw files; all `168` required functional points hit |

## Known Limits

- V2 supports integer/quant execution only; BF16, FP8, coherent cache, IOMMU,
  multiple contexts, multiple program queues, graph scheduling, and multi-NPU
  scaling remain out of first-release scope.
- One program is active at a time. DMA engines execute one command/AXI
  transaction at a time with in-order completion.
- Host software owns physical allocation, address translation, and platform
  cache maintenance around descriptors, code, arguments, tensors, and
  completion records.
- Structural coverage reports are generated but have no percentage threshold;
  required functional coverage is the hard gate.
- Verification uses Verilator simulation/lint. FPGA/ASIC synthesis, timing, CDC,
  and physical implementation are not part of the current gate.

## Next Step

Prepare the V2 release audit: independent code review, synthesis-oriented lint
and CDC planning, performance characterization, and a formal v2.0 release
checklist. New architectural scope must enter the roadmap and decision log
before implementation.
