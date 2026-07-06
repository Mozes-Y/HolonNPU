# HolonNPU Decision Log

This file keeps the active architectural and engineering decisions that govern
the current codebase. Superseded details are intentionally not repeated here;
Git history and `CHANGELOG.md` provide long-form historical traceability.

## ADR-0001: Roadmap-First Development

Status: Accepted.

Decision:

- `docs/ROADMAP.md` is the planning source for staged work.
- `docs/PROGRESS.md` records the current baseline and latest verification
  result, not every historical command transcript.
- Architecture, ABI, and verification changes must update docs before or with
  code changes.

Impact:

- Future feature work starts by extending the roadmap and decision log.
- Implementation must not silently add unplanned public features.

## ADR-0002: v1 Scope Is INT8 GEMM Only

Status: Accepted.

Decision:

- v1 implements descriptor-driven signed INT8 GEMM with signed INT32 output.
- The public hardware interface is AXI-Lite control plus AXI4 master DMA.
- Vector, BF16, FP8, convolution, graph scheduling, and post-processing engines
  are out of scope until explicitly planned.

Impact:

- Public ABI and tests optimize for GEMM correctness and protocol discipline.
- Future accelerator features require new roadmap and ADR entries.

## ADR-0003: Toolchain Baseline

Status: Accepted.

Decision:

- Require CMake 4.0+, Ninja, Verilator, C23 for C driver/API headers, C++26 for
  simulation and host tests, and Python 3 for generators/checkers.
- Use CTest presets for debug, lint, regression, and coverage.

Impact:

- The project favors modern language features and target-centric build rules.
- Compatibility with older C/C++ standards is intentionally not preserved.

## ADR-0004: Single Descriptor Queue

Status: Accepted.

Decision:

- v1 accepts one descriptor in flight.
- Software writes descriptor base registers and rings a doorbell.
- A busy doorbell is rejected by the control plane.

Impact:

- No queue depth, context ID, or multi-tenant scheduling appears in ABI 2.0.
- Performance scaling beyond one descriptor is future system-level work.

## ADR-0005: ABI Single Source Of Truth

Status: Accepted.

Decision:

- `spec/holon_npu_abi.json` is the source for register offsets, descriptor
  layout, constants, errors, and generated interface documentation.
- `tools/gen_abi.py --check` byte-compares generated RTL/C/docs outputs against
  the schema.
- Generated files are checked in for reviewability but must not be edited by
  hand.

Impact:

- ABI changes flow through schema review and regeneration.
- Manual RTL/C/document drift is treated as a verification failure.

## ADR-0006: ABI 2.0 Register And Descriptor Contract

Status: Accepted.

Decision:

- ABI 2.0 keeps the current AXI-Lite register offsets and descriptor layout.
- Descriptor `version` must equal ABI major version `2`.
- Capability register semantics use `ARRAY_K` and `ARRAY_N` for the
  B-weight-stationary physical array.

Impact:

- ABI values are stable unless a future public ABI decision updates the schema.
- Older source-level names from pre-HolonNPU or pre-ABI-2.0 releases are not
  kept as compatibility aliases.

## ADR-0007: Interface-Native Product RTL

Status: Accepted.

Decision:

- Product/internal RTL uses `npu_vr_if`, `npu_axi_lite_if`, and `npu_axi4_if`
  modports for protocol boundaries.
- Flattened wrappers exist only at the product pin boundary or inside
  simulation-only harnesses.
- `tools/check_rtl_interface_usage.py` enforces product/test ownership
  boundaries.

Impact:

- Core module connectivity remains protocol-explicit.
- C++/Verilator convenience wrappers must stay under `sim/rtl/`.

## ADR-0008: AXI-Lite Control Semantics

Status: Accepted.

Decision:

- AXI-Lite AW and W channels may arrive together or skewed.
- Reset values, access permissions, read-only/write-only behavior, clear bits,
  IRQ status, and error transitions follow generated ABI documentation.
- Illegal or unsupported writes return a defined slave error or are explicitly
  documented as ignored.

Impact:

- Control tests must cover AW/W skew and documented side effects.
- RTL cannot rely on simultaneous AW/W arrival.

## ADR-0009: AXI4 DMA Policy

Status: Accepted.

Decision:

- AXI4 data width is 128 bits and v1 tensor accesses are 16-byte aligned.
- DMA issues INCR bursts with at most 16 beats and one outstanding transfer per
  engine.
- Read errors drain through `RLAST`; write errors terminate through the write
  response path.

Impact:

- Unaligned or unsupported byte counts fail before AXI traffic.
- Multi-outstanding DMA and address translation are future work.

## ADR-0010: B-Weight-Stationary Matrix Engine

Status: Accepted.

Decision:

- The active matrix engine is B-weight-stationary.
- B tile values are loaded into PE weight registers by K row and N column.
- A values enter as a wavefront, psums stream through K rows, and C partials
  accumulate outside PE-local state.
- INT8 multiply and INT32 psum accumulation use deterministic wraparound
  behavior matching the C++ golden model.

Impact:

- The public capability naming is `ARRAY_K`/`ARRAY_N`.
- The former output-stationary design is not kept as a parallel RTL path.

## ADR-0011: Tiled GEMM Scheduler

Status: Accepted.

Decision:

- For each output tile, C accumulation is cleared, K tiles are loaded and
  computed, then active C chunks are written back.
- `npu_gemm_tile_scratchpad.sv` owns A wavefront/masks and psum timing for the
  product path.
- Reusable Phase 5 scratchpad modules remain tested infrastructure but are not
  all active in the product GEMM datapath.

Impact:

- Tests must distinguish product-active datapath modules from reusable support
  modules.
- Edge tiles must prove masked dimensions do not corrupt inactive C elements.

## ADR-0012: Command Processor Validation

Status: Accepted.

Decision:

- Descriptor fetch validates ABI version, opcode, size, flags, dimensions,
  alignment, stride constraints, and reserved fields before issuing GEMM.
- Invalid descriptors never issue a GEMM command.

Impact:

- Descriptor fuzz and negative tests are required regression coverage.
- Error codes must remain aligned with generated interface documentation.

## ADR-0013: C Driver ABI Sharing

Status: Accepted.

Decision:

- The C driver consumes generated public ABI headers.
- Descriptor construction zeroes reserved fields and validates software-visible
  argument constraints before touching hardware.
- Host-side driver tests statically check descriptor size and field offsets.

Impact:

- Driver and RTL share ABI constants through generated artifacts.
- C consumers must compile public headers as C23 or C++.

## ADR-0014: Deterministic Regression Gate

Status: Accepted.

Decision:

- All randomized tests are deterministic and record seeds.
- Debug, lint, regression, and coverage presets are distinct CTest workflows.
- Build targets compile artifacts; CTest presets schedule tests.

Impact:

- Local debug remains fast enough for development.
- Release confidence comes from full regression and coverage gates.

## ADR-0015: Project Rename To HolonNPU

Status: Accepted.

Decision:

- The project name is HolonNPU.
- Public C symbols and headers use `holon_npu_*` and `HOLON_NPU_*`.
- RTL module/file names may keep generic `npu_*` prefixes.

Impact:

- The rename was a public source-level breaking change.
- Old public aliases are not maintained.

## ADR-0016: Minimal CMake Presets

Status: Accepted.

Decision:

- Keep only `debug`, `regression`, and `coverage` configure/build presets plus
  `debug`, `lint`, `regression`, and `coverage` test presets.
- Use Ninja and explicit `--parallel`/CTest `-j` when callers need higher
  concurrency.
- Do not encode architecture version names into build target names.

Impact:

- Single-test selection uses CTest `-R`, not a large preset matrix.
- The generic lint convenience target is named `lint`.

## ADR-0017: Simulation-Only RTL Harness Directory

Status: Accepted.

Decision:

- Simulation-only SystemVerilog harnesses live under `sim/rtl/`.
- Product RTL lives under `rtl/`.
- Product source targets must not reference simulation harness files.

Impact:

- The RTL tree remains synthesizable/product-focused.
- Test-only wrappers are visible but cannot become hidden architecture
  dependencies.

## ADR-0023: Protocol-First Native SVA

Status: Accepted.

Decision:

- Use native named `assert property` and `cover property` directly in RTL.
- Enable assertions through Verilator `--assert`; do not use project-owned
  verification behavior macros.
- Keep one expected-fail assertion smoke test in CTest to prove assertions are
  active.

Impact:

- Protocol and local design invariants live next to the RTL they protect.
- Macro policy checks prevent old verification wrapper patterns from returning.

## ADR-0024: Functional Coverage Gate

Status: Accepted.

Decision:

- Coverage uses a dedicated build tree because Verilator coverage changes
  generated models.
- Functional coverage points are hard-gated; structural line/toggle/FSM/user
  coverage reports are generated but not threshold-gated yet.
- `tools/check_coverage.py` verifies required and hit coverage manifests.

Impact:

- Release verification includes coverage without slowing ordinary debug builds.
- Structural thresholds can be added later when enough data exists.

## ADR-0025: C++ Typed Coverage Runtime

Status: Accepted.

Decision:

- C++ testbenches use `holon_npu_tb::test_run` and typed
  `coverage_point` values.
- Coverage point names live in a `constexpr` C++ registry.
- CTest passes `--tb-coverage-root` explicitly in coverage builds.
- The runtime writes per-test functional manifests and Verilator raw coverage
  data.

Impact:

- Coverage semantics are not string literals scattered through testbenches.
- Python coverage checking derives requirements from emitted manifests.

## ADR-0026: C23 Macro-Free ABI Constants And Target-Centric CMake

Status: Accepted.

Decision:

- Generated C ABI constants use C23 `static constexpr`, not project-owned
  constant macros.
- Public header targets propagate the C23 compile-feature requirement.
- `__cplusplus` linkage branches remain for C++ consumers.
- Source ownership is declared on semantic targets with `target_sources()`.
- Public headers are declared with `FILE_SET HEADERS`.
- Verilator executable creation is separate from CTest registration.
- Lint target creation is separate from lint CTest registration.

Impact:

- The root CMake file remains explicit without directory-wide source-list
  variables.
- Future test targets can be added without coupling build behavior to test
  scheduling.
- C source compatibility requires C23.
- ABI values, register map, descriptor layout, and function names are
  unchanged.
