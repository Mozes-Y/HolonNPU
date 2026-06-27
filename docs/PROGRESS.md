# my_npu Progress Log

This file records actual project progress. It must be updated after every phase
and whenever a verification result changes the engineering plan.

## Current Status

- Active phase: None; v1 release remediation complete.
- Last updated: 2026-06-27.
- Overall state: Phase 0 through Phase 11 complete; v1 source release gate
  passed with product-level top integration and release-facing documentation.

## Phase 0: Governance Bootstrap

Status: Complete.

Completed work:

- Created initial roadmap, progress log, decision log, architecture outline,
  interface outline, and verification outline.

Verification commands:

- `find docs -maxdepth 1 -type f -name '*.md' -printf '%f\n'`
- `rg '^### v[1-5]:' docs/ROADMAP.md`
- `rg 'Phase 0|Status:' docs/PROGRESS.md`
- `rg '^### Phase [0-9]+:' docs/ROADMAP.md`

Results:

- Required documentation files exist:
  `ROADMAP.md`, `PROGRESS.md`, `DECISIONS.md`, `ARCHITECTURE.md`,
  `INTERFACE.md`, and `VERIFICATION.md`.
- `docs/ROADMAP.md` defines v1, v2, v3, v4, and v5.
- `docs/ROADMAP.md` defines v1 Phase 0 through Phase 11.
- Phase 0 progress is recorded here.

Remaining issues:

- None for Phase 0.

Next step:

- Start Phase 1: Project Skeleton.

## Phase 1: Project Skeleton

Status: Complete.

Completed work:

- Added root `CMakeLists.txt`.
- Added `CMakePresets.json` with `debug` configure, build, and test presets.
- Added top-level `rtl/`, `sim/`, `sw/`, `include/`, and `tests/`
  directories.
- Added minimal SystemVerilog smoke top at `rtl/npu_smoke_top.sv`.
- Added C++26 Verilator smoke testbench at `sim/smoke_tb.cpp`.
- Added CTest test named `npu_smoke`.
- Added a target-specific GCC suppression for a C++26 deprecation warning emitted
  by Verilator 5.046 runtime sources.

Verification commands:

- `sed -n '1,260p' docs/ROADMAP.md`
- `sed -n '260,520p' docs/ROADMAP.md`
- `sed -n '520,700p' docs/ROADMAP.md`
- `sed -n '1,220p' docs/PROGRESS.md`
- `cmake --version`
- `verilator --version`
- `c++ --version`
- `cmake --preset debug`
- `cmake --build --preset debug`
- `ctest --preset debug`
- `find . -maxdepth 2 -type f -not -path './build/*' -printf '%p\n'`
- `find rtl sim sw include tests -maxdepth 1 -type d -printf '%p\n'`
- `rg -n 'TODO|FIXME|HACK' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs`

Results:

- Toolchain found:
  - CMake 4.3.4.
  - Verilator 5.046.
  - GCC C++ 15.3.0.
- `cmake --preset debug` configured successfully.
- `cmake --build --preset debug` built `npu_smoke_tb` successfully with no
  remaining build warnings after the target-specific Verilator runtime warning
  suppression.
- `ctest --preset debug` passed `1/1` tests.
- Required Phase 1 directories and files exist.
- The only `TODO` match is the roadmap discipline text itself, not an unfinished
  source marker.

Remaining issues:

- None for Phase 1.

Next step:

- Start Phase 2: Architecture And ABI Freeze For v1.

## Phase 2: Architecture And ABI Freeze For v1

Status: Complete.

Completed work:

- Replaced the placeholder interface outline with a frozen v1 ABI in
  `docs/INTERFACE.md`.
- Defined AXI-Lite register offsets, widths, access types, reset values, fields,
  and side effects.
- Defined capability registers, status bits, error codes, interrupt semantics,
  AXI4 constraints, descriptor layout, descriptor flags, and initial software API
  contract.
- Updated `docs/ARCHITECTURE.md` to match the single-descriptor ABI,
  output-stationary matrix engine, 128-bit DMA beat assumptions, and alignment
  rules.
- Added Phase 2 ABI review checklist to `docs/VERIFICATION.md`.
- Added decision records:
  - ADR-0004: v1 uses a single descriptor queue.
  - ADR-0005: v1 matrix dataflow and arithmetic.
  - ADR-0006: v1 ABI register and descriptor conventions.

Verification commands:

- `sed -n '1,220p' docs/ROADMAP.md`
- `sed -n '1,260p' docs/PROGRESS.md`
- `sed -n '1,260p' docs/INTERFACE.md`
- `sed -n '1,260p' docs/DECISIONS.md`
- `sed -n '1,260p' docs/ARCHITECTURE.md`
- `rg -n 'TBD|TODO|FIXME|HACK' docs/INTERFACE.md docs/ARCHITECTURE.md docs/DECISIONS.md docs/VERIFICATION.md`
- `rg -n 'DEVICE_ID|ABI_VERSION|CAP0|DESC_ADDR_LO|DOORBELL|PERF_ERROR_COUNT|MY_NPU_DESC_SIZE|ERR_INVALID_DESC_VERSION|IRQ_ON_DONE' docs/INTERFACE.md`
- `rg -n 'ADR-0004|ADR-0005|ADR-0006|output-stationary|signed INT8|signed INT32' docs/DECISIONS.md`
- `rg -n 'AXI-Lite Control Interface|Register Map|Register Fields|GEMM Descriptor ABI|Descriptor Layout|Status And Error Codes|Interrupt Semantics|Software API Contract|AXI4 Master Interface' docs/INTERFACE.md`
- `ctest --preset debug`

Results:

- `docs/INTERFACE.md` contains the required Phase 2 ABI sections and concrete
  values.
- Placeholder scan returned no matches after removing a false-positive example
  string from the verification checklist.
- `docs/DECISIONS.md` contains ADR-0004, ADR-0005, and ADR-0006.
- `ctest --preset debug` passed `1/1` tests after documentation-only changes.

Remaining issues:

- None for Phase 2.

Next step:

- Start Phase 3: Common RTL Infrastructure.

## Phase 3: Common RTL Infrastructure

Status: Complete.

Completed work:

- Added `rtl/common/npu_pkg.sv` with frozen v1 ABI constants and common error
  enum values.
- Added `rtl/common/npu_vr_if.sv` valid-ready stream interface.
- Added `rtl/common/npu_axi_lite_if.sv` AXI-Lite interface shell.
- Added `rtl/common/npu_axi4_if.sv` AXI4 interface shell.
- Added `rtl/common/npu_fifo.sv` parameterized valid-ready FIFO.
- Added `rtl/common/npu_skid_buffer.sv` single-entry skid buffer.
- Added `rtl/common/npu_register_slice.sv` one-entry register slice.
- Added `rtl/common/npu_common_smoke_top.sv` to instantiate and test common
  modules and ABI package constants.
- Added `sim/common_tb.cpp` focused C++26 tests for package constants, FIFO,
  skid buffer, and register slice behavior.
- Added CMake/CTest integration for `npu_common_tb` and `common_rtl_lint`.
- Updated `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, and
  `docs/VERIFICATION.md` with the clock/reset, valid-ready, and Phase 3
  verification conventions.

Verification commands:

- `sed -n '130,230p' docs/ROADMAP.md`
- `sed -n '220,275p' docs/ROADMAP.md`
- `sed -n '1,360p' docs/PROGRESS.md`
- `sed -n '1,220p' docs/ARCHITECTURE.md`
- `sed -n '1,140p' docs/VERIFICATION.md`
- `cmake --preset debug`
- `cmake --build --preset debug`
- `cmake --build --preset debug --target common_rtl_lint`
- `ctest --preset debug`
- `find rtl/common -maxdepth 1 -type f -printf '%f\n'`
- `rg -n 'npu_pkg|npu_axi_lite_if|npu_axi4_if|npu_vr_if|npu_fifo|npu_skid_buffer|npu_register_slice|npu_common_smoke_top' CMakeLists.txt rtl/common sim/common_tb.cpp docs/ARCHITECTURE.md docs/VERIFICATION.md`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully.
- `cmake --build --preset debug` built `npu_smoke_tb` and `npu_common_tb`
  successfully with no build warnings.
- `cmake --build --preset debug --target common_rtl_lint` passed with
  Verilator 5.046.
- `ctest --preset debug` passed `3/3` tests:
  - `npu_smoke`
  - `npu_common`
  - `common_rtl_lint`
- Required common RTL files exist.
- Source/document marker scan returned no unfinished `TODO`, `FIXME`, `HACK`, or
  `TBD` entries in the checked paths.

Remaining issues:

- None for Phase 3.

Next step:

- Start Phase 4: PE And Systolic Array.

## Phase 4: PE And Systolic Array

Status: Complete.

Completed work:

- Added `rtl/matrix/npu_pe_i8.sv`, a parameterized signed INT8 PE with signed
  INT32 accumulation for the v1 configuration.
- Added `rtl/matrix/npu_systolic_array.sv`, a parameterized
  `ARRAY_M x ARRAY_N` matrix core built from PE instances with row/column masks.
- Added `rtl/matrix/npu_systolic_array_test_top.sv`, a deterministic test
  wrapper for golden-model array verification without introducing Phase 5
  scratchpad or tiling logic.
- Added `sim/pe_tb.cpp` covering reset, masked input, positive operands,
  negative operands, zero multiplication, and INT32 wraparound boundary behavior.
- Added `sim/array_tb.cpp` covering `1x1x1`, `16x16x16`, and `17x19x23` GEMM
  shapes with per-output C++ golden-model comparison.
- Added CMake/CTest integration for `npu_pe_tb`, `npu_array_tb`, and
  `matrix_rtl_lint`.
- Updated `docs/VERIFICATION.md` with the Phase 4 matrix core checklist.

Verification commands:

- `cmake --preset debug`
- `cmake --build --preset debug`
- `cmake --build --preset debug --target matrix_rtl_lint`
- `ctest --preset debug`
- `find rtl/matrix -maxdepth 1 -type f -printf '%f\n'`
- `rg -n 'test_positive_negative_zero|test_int32_wrap_boundary|1x1x1|16x16x16|17x19x23|golden' sim/pe_tb.cpp sim/array_tb.cpp`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully.
- `cmake --build --preset debug` built all simulation targets successfully with
  no build warnings.
- `cmake --build --preset debug --target matrix_rtl_lint` passed with
  Verilator 5.046.
- `ctest --preset debug` passed `6/6` tests:
  - `npu_smoke`
  - `npu_common`
  - `common_rtl_lint`
  - `npu_pe`
  - `npu_array`
  - `matrix_rtl_lint`
- Required matrix RTL files exist.
- Source/document marker scan returned no unfinished `TODO`, `FIXME`, `HACK`, or
  `TBD` entries in the checked paths.

Remaining issues:

- None for Phase 4.

Next step:

- Start Phase 5: Scratchpad And Tiling Datapath.

## Phase 5: Scratchpad And Tiling Datapath

Status: Complete.

Completed work:

- Added `rtl/datapath/npu_tile_mask.sv` for M/N/K tile mask generation.
- Added `rtl/datapath/npu_banked_scratchpad.sv` with explicit bank/address
  range error flags.
- Added `rtl/datapath/npu_i8_tile_buffer.sv` for A/B tile storage.
- Added `rtl/datapath/npu_c_accum_buffer.sv` for C accumulator/output storage.
- Added `rtl/datapath/npu_ping_pong_ctrl.sv` with observable load, compute,
  store, done, and bank-toggle behavior.
- Added `rtl/datapath/npu_tiling_datapath_test_top.sv` to instantiate tile masks,
  A/B/C buffers, ping-pong scheduling, and a masked compute path using the
  Phase 4 array.
- Added `sim/tiling_tb.cpp` covering masks, valid buffer read/write, illegal
  access detection, ping-pong schedule transitions, and the `1x3x7` tail tile
  for a `17x19x23` GEMM edge case.
- Added CMake/CTest integration for `npu_tiling_tb` and `datapath_rtl_lint`.
- Updated `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, and
  `docs/VERIFICATION.md` with Phase 5 scratchpad, tile-mask, and ping-pong
  conventions.

Verification commands:

- `sed -n '270,335p' docs/ROADMAP.md`
- `sed -n '1,520p' docs/PROGRESS.md`
- `sed -n '150,230p' docs/ARCHITECTURE.md`
- `sed -n '120,210p' docs/VERIFICATION.md`
- `cmake --preset debug`
- `cmake --build --preset debug`
- `cmake --build --preset debug --target datapath_rtl_lint`
- `ctest --preset debug`
- `find rtl/datapath -maxdepth 1 -type f -printf '%f\n'`
- `rg -n 'test_masks|test_buffers|test_masked_tail_compute|test_schedule|1x3x7|datapath_rtl_lint|npu_tiling' sim/tiling_tb.cpp CMakeLists.txt docs/VERIFICATION.md`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully.
- `cmake --build --preset debug` built all simulation targets successfully with
  no build warnings.
- `cmake --build --preset debug --target datapath_rtl_lint` passed with
  Verilator 5.046.
- `ctest --preset debug` passed `8/8` tests:
  - `npu_smoke`
  - `npu_common`
  - `common_rtl_lint`
  - `npu_pe`
  - `npu_array`
  - `matrix_rtl_lint`
  - `npu_tiling`
  - `datapath_rtl_lint`
- Required datapath RTL files exist.
- Source/document marker scan returned no unfinished `TODO`, `FIXME`, `HACK`, or
  `TBD` entries in the checked paths.

Remaining issues:

- None for Phase 5.

Next step:

- Start Phase 6: AXI-Lite Control Plane.

## Phase 6: AXI-Lite Control Plane

Status: Complete.

Completed work:

- Added `rtl/control/npu_control_regs.sv`, implementing the frozen v1 AXI-Lite
  register map.
- Implemented reset values, read-only registers, write-only pulse register
  reads, descriptor address registers, IRQ enable/status, doorbell, clear,
  soft-reset, status, error code, IRQ output, and initial performance counters.
- Implemented `SLVERR` responses for read-only writes, unmapped accesses,
  reserved-bit writes, unsupported partial pulse-control writes, and doorbell
  writes while busy.
- Added backend test inputs for done/error events until the Phase 8 command
  processor exists.
- Added `sim/control_tb.cpp`, a C++ AXI-Lite driver test for reset values,
  register read/write behavior, illegal access responses, busy/done/error state
  transitions, IRQ behavior, clear behavior, and soft reset.
- Added CMake/CTest integration for `npu_control_tb` and `control_rtl_lint`.
- Updated `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, and
  `docs/VERIFICATION.md` with Phase 6 control-plane behavior.

Verification commands:

- `sed -n '335,395p' docs/ROADMAP.md`
- `sed -n '1,620p' docs/PROGRESS.md`
- `sed -n '39,158p' docs/INTERFACE.md`
- `sed -n '244,275p' docs/INTERFACE.md`
- `cmake --preset debug`
- `cmake --build --preset debug`
- `cmake --build --preset debug --target control_rtl_lint`
- `ctest --preset debug`
- `find rtl/control -maxdepth 1 -type f -printf '%f\n'`
- `rg -n 'test_reset_values|test_rw_and_illegal_access|test_done_flow|test_error_and_soft_reset|control_rtl_lint|npu_control' sim/control_tb.cpp CMakeLists.txt docs/VERIFICATION.md`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully.
- `cmake --build --preset debug` built all simulation targets successfully with
  no build warnings.
- `cmake --build --preset debug --target control_rtl_lint` passed with
  Verilator 5.046.
- `ctest --preset debug` passed `10/10` tests:
  - `npu_smoke`
  - `npu_common`
  - `common_rtl_lint`
  - `npu_pe`
  - `npu_array`
  - `matrix_rtl_lint`
  - `npu_tiling`
  - `datapath_rtl_lint`
  - `npu_control`
  - `control_rtl_lint`
- Required control RTL file exists.
- Source/document marker scan returned no unfinished `TODO`, `FIXME`, `HACK`, or
  `TBD` entries in the checked paths.

Remaining issues:

- None for Phase 6.

Next step:

- Start Phase 7: AXI4 DMA And Memory Model.

## Phase 7: AXI4 DMA And Memory Model

Status: Complete.

Completed work:

- Added `rtl/dma/npu_axi4_read_dma.sv`, a 128-bit AXI4 read DMA engine with
  aligned request validation, burst splitting, stream output, response handling,
  and error-code reporting.
- Added `rtl/dma/npu_axi4_write_dma.sv`, a 128-bit AXI4 write DMA engine with
  aligned request validation, full-beat write strobes, burst splitting, response
  handling, and error-code reporting.
- Added `rtl/dma/npu_read_dma_test_top.sv` and
  `rtl/dma/npu_write_dma_test_top.sv` wrappers that split 128-bit data into
  64-bit halves for C++ tests.
- Added `sim/read_dma_tb.cpp`, including a C++ AXI4 read memory model covering
  single-burst reads, multi-burst/cross-boundary reads, unaligned/zero-byte
  rejection, and AXI read error injection.
- Added `sim/write_dma_tb.cpp`, including a C++ AXI4 write memory model covering
  single-burst writes, multi-burst/cross-boundary writes, unaligned/zero-byte
  rejection, and AXI write error injection.
- Added CMake/CTest integration for `npu_read_dma_tb`, `npu_write_dma_tb`,
  `dma_rtl_lint_read`, `dma_rtl_lint_write`, and the `dma_rtl_lint` build
  target.
- Updated `docs/INTERFACE.md`, `docs/ARCHITECTURE.md`,
  `docs/DECISIONS.md`, and `docs/VERIFICATION.md` with Phase 7 DMA alignment,
  burst, response, and verification policy.

Verification commands:

- `sed -n '360,410p' docs/ROADMAP.md`
- `sed -n '1,760p' docs/PROGRESS.md`
- `sed -n '159,183p' docs/INTERFACE.md`
- `rg -n "AXI4|DMA|unaligned|alignment|Response Mapping" docs/INTERFACE.md docs/ARCHITECTURE.md docs/DECISIONS.md docs/VERIFICATION.md`
- `cmake --preset debug`
- `cmake --build --preset debug`
- `cmake --build --preset debug --target dma_rtl_lint`
- `ctest --preset debug`
- `find rtl/dma -maxdepth 1 -type f -printf '%f\n'`
- `rg -n 'test_single_burst|test_multi_burst_cross_tile|test_alignment_error|test_axi_error|zero byte|dma_rtl_lint|npu_read_dma|npu_write_dma' sim/read_dma_tb.cpp sim/write_dma_tb.cpp CMakeLists.txt docs/VERIFICATION.md`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully.
- `cmake --build --preset debug` built all simulation targets successfully with
  no build warnings.
- `cmake --build --preset debug --target dma_rtl_lint` passed for both read and
  write DMA wrappers with Verilator 5.046.
- `ctest --preset debug` passed `14/14` tests:
  - `npu_smoke`
  - `npu_common`
  - `common_rtl_lint`
  - `npu_pe`
  - `npu_array`
  - `matrix_rtl_lint`
  - `npu_tiling`
  - `datapath_rtl_lint`
  - `npu_control`
  - `control_rtl_lint`
  - `npu_read_dma`
  - `npu_write_dma`
  - `dma_rtl_lint_read`
  - `dma_rtl_lint_write`
- Required DMA RTL files exist.
- Source/document marker scan returned no unfinished `TODO`, `FIXME`, `HACK`, or
  `TBD` entries in the checked paths.

Remaining issues:

- None for Phase 7.

Next step:

- Start Phase 8: Command Processor.

## Phase 8: Command Processor

Status: Complete.

Completed work:

- Added `rtl/command/npu_command_processor.sv`, implementing descriptor fetch,
  decode, validation, and GEMM command issue.
- Descriptor fetch uses the Phase 7 AXI4 read DMA for one aligned 128-byte read.
- Validation covers descriptor size, version, opcode, flags, dimensions,
  tensor address and row-stride constraints, and reserved fields.
- Valid descriptors assert a valid/ready decoded GEMM command with M/N/K,
  A/B/C base addresses, A/B/C row strides, and descriptor flag outputs.
- Invalid descriptors assert documented error codes and do not issue commands.
- Added `rtl/command/npu_command_processor_test_top.sv` for 64-bit split data
  access from C++ tests.
- Added `sim/command_tb.cpp`, including a descriptor memory model and tests for
  valid issue, invalid size/version/opcode/flags/dimensions/alignment/reserved
  fields, unaligned descriptor address, and AXI read error injection.
- Added CMake/CTest integration for `npu_command_tb` and `command_rtl_lint`.
- Updated `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, and
  `docs/VERIFICATION.md` with Phase 8 descriptor validation and command issue
  behavior.

Verification commands:

- `sed -n '410,465p' docs/ROADMAP.md`
- `sed -n '1,900p' docs/PROGRESS.md`
- `sed -n '184,243p' docs/INTERFACE.md`
- `rg -n "Descriptor|opcode|version|ERR_INVALID|ERR_RESERVED|ERR_DIMENSION|MY_NPU_OPCODE" docs/INTERFACE.md docs/DECISIONS.md docs/ARCHITECTURE.md docs/VERIFICATION.md`
- `cmake --preset debug`
- `cmake --build --preset debug`
- `cmake --build --preset debug --target command_rtl_lint`
- `ctest --preset debug`
- `find rtl/command -maxdepth 1 -type f -printf '%f\n'`
- `rg -n 'test_valid_descriptor|test_invalid_descriptors|test_descriptor_fetch_errors|bad size|bad version|bad opcode|bad flags|reserved nonzero|command_rtl_lint|npu_command' sim/command_tb.cpp CMakeLists.txt docs/VERIFICATION.md`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully.
- `cmake --build --preset debug` built all simulation targets successfully with
  no build warnings.
- `cmake --build --preset debug --target command_rtl_lint` passed with
  Verilator 5.046.
- `ctest --preset debug` passed `16/16` tests:
  - `npu_smoke`
  - `npu_common`
  - `common_rtl_lint`
  - `npu_pe`
  - `npu_array`
  - `matrix_rtl_lint`
  - `npu_tiling`
  - `datapath_rtl_lint`
  - `npu_control`
  - `control_rtl_lint`
  - `npu_read_dma`
  - `npu_write_dma`
  - `dma_rtl_lint_read`
  - `dma_rtl_lint_write`
  - `npu_command`
  - `command_rtl_lint`
- Required command RTL files exist.
- Source/document marker scan returned no unfinished `TODO`, `FIXME`, `HACK`, or
  `TBD` entries in the checked paths.

Remaining issues:

- None for Phase 8.

Next step:

- Start Phase 9: Integrated GEMM Accelerator.

## Phase 9: Integrated GEMM Accelerator

Status: Complete.

Completed work:

- Added `rtl/integration/npu_gemm_accelerator.sv`, a decoded-command GEMM
  accelerator that consumes the Phase 8 command valid/ready interface.
- Integrated one shared AXI4 read DMA for A/B tile loads and one AXI4 write DMA
  for C writeback.
- Added local `16x16` A/B tile buffers, row/column/K masking, and a
  `16x16x16` tiled scheduler.
- Reused the Phase 4 systolic array for output-stationary INT8 x INT8 to INT32
  accumulation across all K tiles.
- Implemented active C chunk writeback, completion/error IRQ output, stage
  observability, and datapath performance counters.
- Added `sim/gemm_tb.cpp`, a combined AXI4 memory-model testbench with exact
  C++ golden-model comparison.
- Added CMake/CTest integration for `npu_gemm_tb` and
  `integration_rtl_lint`.
- Updated `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, and
  `docs/VERIFICATION.md` with Phase 9 integration behavior.
- During focused testing, fixed a stale `write_dma_done` scheduling issue that
  advanced C store chunks before the new write DMA beat was consumed.

Verification commands:

- `sed -n '220,520p' docs/ROADMAP.md`
- `sed -n '260,620p' docs/PROGRESS.md`
- `verilator --lint-only --sv -Wall rtl/common/npu_pkg.sv rtl/dma/npu_axi4_read_dma.sv rtl/dma/npu_axi4_write_dma.sv rtl/matrix/npu_pe_i8.sv rtl/matrix/npu_systolic_array.sv rtl/integration/npu_gemm_accelerator.sv --top-module npu_gemm_accelerator`
- `cmake --preset debug`
- `cmake --build --preset debug --target npu_gemm_tb`
- `cmake --build --preset debug --target integration_rtl_lint`
- `./build/debug/npu_gemm_tb`
- `cmake --build --preset debug`
- `ctest --preset debug`
- `find rtl/integration -maxdepth 1 -type f -printf '%f\n'`
- `rg -n 'npu_gemm_accelerator|npu_gemm|integration_rtl_lint|17x19x23|64x64x64|ADR-0012|Phase 9 Integrated' CMakeLists.txt rtl/integration sim/gemm_tb.cpp docs/ARCHITECTURE.md docs/DECISIONS.md docs/VERIFICATION.md docs/PROGRESS.md`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully.
- Isolated Verilator lint for `npu_gemm_accelerator` passed with Verilator
  5.046.
- `cmake --build --preset debug --target npu_gemm_tb` built successfully with
  no build warnings.
- `cmake --build --preset debug --target integration_rtl_lint` passed with
  Verilator 5.046.
- `./build/debug/npu_gemm_tb` passed.
- `ctest --preset debug` passed `18/18` tests:
  - `npu_smoke`
  - `npu_common`
  - `common_rtl_lint`
  - `npu_pe`
  - `npu_array`
  - `matrix_rtl_lint`
  - `npu_tiling`
  - `datapath_rtl_lint`
  - `npu_control`
  - `control_rtl_lint`
  - `npu_read_dma`
  - `npu_write_dma`
  - `dma_rtl_lint_read`
  - `dma_rtl_lint_write`
  - `npu_command`
  - `command_rtl_lint`
  - `npu_gemm`
  - `integration_rtl_lint`
- GEMM coverage includes `1x1x1`, `16x16x16`, `17x19x23`, `64x64x64`, and
  deterministic signed INT8 randomized cases with seeds `101`, `202`, and
  `303`.
- The C++ golden model matched every logical INT32 C output exactly.
- Stage checks observed load A, load B, compute, store, and done stages.
- Required integration RTL file exists.
- Source/document marker scan returned no unfinished `TODO`, `FIXME`, `HACK`, or
  `TBD` entries in the checked paths.

Remaining issues:

- None for Phase 9.

Next step:

- Start Phase 10: Software Driver.

## Phase 10: Software Driver

Status: Complete.

Completed work:

- Added `include/my_npu_regs.h` with v1 register offsets, reset constants,
  status bits, IRQ masks, clear masks, ABI constants, and hardware error codes.
- Added `include/my_npu_desc.h` with the v1 GEMM descriptor layout,
  descriptor flags, descriptor config type, and compile-time size/offset checks.
- Added `sw/my_npu_driver.h` with the minimal C driver API and public driver
  result, status, capability, and performance types.
- Added `sw/my_npu_driver.c` implementing init, capability reads, descriptor
  build, submit, poll, wait, error read, clear, and performance-counter reads.
- Added `tests/driver_test.cpp`, a host-side C++26 driver test using a volatile
  register aperture.
- Added CMake integration for the C driver library and `my_npu_driver` CTest.
- Updated `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, and
  `docs/VERIFICATION.md` with Phase 10 software-driver behavior.
- Updated `include/README.md` and `sw/README.md` to describe the new files.

Verification commands:

- `sed -n '1,760p' docs/ROADMAP.md`
- `sed -n '1,760p' docs/PROGRESS.md`
- `sed -n '1,360p' docs/INTERFACE.md`
- `cmake --preset debug`
- `cmake --build --preset debug --target my_npu_driver_test`
- `ctest --preset debug -R my_npu_driver --output-on-failure`
- `cmake --build --preset debug`
- `ctest --preset debug`
- `find include sw tests -maxdepth 1 -type f -printf '%p\n'`
- `rg -n 'my_npu_regs|my_npu_desc|my_npu_driver|my_npu_submit|my_npu_wait|my_npu_read_perf|ADR-0013|Phase 10 Software' include sw tests CMakeLists.txt docs/ARCHITECTURE.md docs/DECISIONS.md docs/VERIFICATION.md`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully with C and C++ enabled.
- `cmake --build --preset debug --target my_npu_driver_test` built the C driver
  library and host-side driver test successfully.
- `ctest --preset debug -R my_npu_driver --output-on-failure` passed.
- `cmake --build --preset debug` built all targets successfully with no build
  warnings.
- `ctest --preset debug` passed `19/19` tests:
  - `npu_smoke`
  - `npu_common`
  - `common_rtl_lint`
  - `npu_pe`
  - `npu_array`
  - `matrix_rtl_lint`
  - `npu_tiling`
  - `datapath_rtl_lint`
  - `npu_control`
  - `control_rtl_lint`
  - `npu_read_dma`
  - `npu_write_dma`
  - `dma_rtl_lint_read`
  - `dma_rtl_lint_write`
  - `npu_command`
  - `command_rtl_lint`
  - `npu_gemm`
  - `integration_rtl_lint`
  - `my_npu_driver`
- Driver test coverage includes descriptor layout static checks, descriptor
  construction, invalid argument rejection, submit/busy handling, poll, wait,
  error read, clear, and performance counter reads.
- Required driver and shared ABI files exist.
- Source/document marker scan returned no unfinished `TODO`, `FIXME`, `HACK`, or
  `TBD` entries in the checked paths.

Remaining issues:

- None for Phase 10.

Next step:

- Start Phase 11: Verification Hardening.

## Phase 11: Verification Hardening

Status: Complete.

Completed work:

- Extended `sim/command_tb.cpp` with deterministic descriptor fuzzing across 64
  fixed seed indices and expected validation error codes.
- Extended `sim/gemm_tb.cpp` with reset-in-flight recovery testing.
- Extended `sim/gemm_tb.cpp` with AXI read error injection and AXI write error
  injection against the integrated GEMM accelerator.
- Added the aggregate `v1_lint` build target for all v1 RTL lint targets.
- Added the aggregate `v1_regression` build target.
- Added `regression` build and test presets in `CMakePresets.json`.
- Updated `docs/DECISIONS.md` with ADR-0014 for deterministic hardening and
  regression policy.
- Updated `docs/VERIFICATION.md` with the Phase 11 hardening checklist and v1
  release checklist.

Verification commands:

- `sed -n '465,540p' docs/ROADMAP.md`
- `sed -n '1,80p' docs/PROGRESS.md`
- `cmake --preset debug`
- `cmake --build --preset debug --target npu_command_tb`
- `cmake --build --preset debug --target npu_gemm_tb`
- `cmake --build --preset debug --target v1_lint`
- `./build/debug/npu_command_tb`
- `./build/debug/npu_gemm_tb`
- `cmake --build --preset regression`
- `ctest --preset regression`
- `cmake --build --preset debug`
- `ctest --preset debug`
- `find rtl sim sw include tests docs -maxdepth 2 -type f -not -path './build/*' -printf '%p\n'`
- `rg -n 'descriptor fuzz seed|test_reset_in_flight|test_axi_read_error|test_axi_write_error|v1_lint|v1_regression|regression|ADR-0014|Phase 11 Hardening|v1 Release Checklist' CMakeLists.txt CMakePresets.json sim docs`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully.
- `cmake --build --preset debug --target npu_command_tb` built successfully.
- `cmake --build --preset debug --target npu_gemm_tb` built successfully.
- `cmake --build --preset debug --target v1_lint` passed all v1 RTL lint
  targets with Verilator 5.046 and no critical warnings.
- `./build/debug/npu_command_tb` passed, including descriptor fuzz seeds
  `0..63`.
- `./build/debug/npu_gemm_tb` passed, including reset-in-flight, AXI read error,
  and AXI write error hardening cases.
- `cmake --build --preset debug` built all simulation and software targets
  successfully with no build warnings.
- `ctest --preset debug` passed `19/19` tests.
- `cmake --build --preset regression` passed and ran the full CTest suite.
- `ctest --preset regression` passed `19/19` tests.
- Source inventory contains the expected RTL, simulation, software, include,
  test, and documentation files.
- Source/document marker scan returned no unfinished placeholder entries in the
  checked paths.
- v1 release checklist in `docs/VERIFICATION.md` passed.

Known limitations:

- v1 intentionally supports one descriptor in flight and one outstanding AXI4
  read/write burst per DMA engine.
- v1 requires 16-byte aligned descriptor, tensor base, and row-stride values.
- v1 implements signed INT8 GEMM with signed INT32 output only; vector
  post-processing, BF16, FP8, graph scheduling, convolution, full softmax,
  LayerNorm, and GELU remain outside v1 scope.

Remaining issues:

- None for the documented v1 phase plan.

Next step:

- v1 plan is complete. Future work must start with a roadmap and decision-log
  update.

## Post-Review v1 Hardening

Status: Complete.

Completed work:

- Initialized repository hygiene with `.gitignore` so generated `build/`
  artifacts are not tracked.
- Added `rtl/integration/npu_top.sv` as the product-level v1 top connecting
  AXI-Lite control, descriptor command processing, GEMM execution, IRQ/status,
  soft reset, performance-counter clear, AXI4 read arbitration, and AXI4
  writeback.
- Fixed AXI-Lite write handling in `npu_control_regs.sv` so AW and W can arrive
  independently.
- Added backend-visible `soft_reset_o` and `clear_perf_o` pulses plus backend
  busy and descriptor-requested performance clear inputs.
- Clarified architectural counter semantics: `PERF_CYCLES` counts
  descriptor-in-flight cycles and `PERF_BUSY_CYCLES` counts backend-active
  cycles.
- Reworked `npu_systolic_array.sv` into an A-left/B-top boundary-fed systolic
  array with explicit boundary valid propagation.
- Updated the GEMM scheduler and matrix tests for the full systolic wavefront.
- Added `rtl/datapath/npu_gemm_tile_scratchpad.sv` and integrated it into the
  GEMM datapath for A/B tile storage.
- Extended CMake and CTest with `npu_top_tb` and `abi_consistency`.
- Added `tools/check_abi_consistency.py` to compare RTL ABI constants against
  public C headers.
- Updated architecture, interface, verification, and decision docs. Added
  ADR-0015.

Verification commands:

- `cmake --preset debug`
- `cmake --build --preset debug`
- `ctest --preset debug --output-on-failure`
- `cmake --build --preset debug --target v1_lint`
- `cmake --build --preset regression`
- `ctest --preset regression`
- `python3 tools/check_abi_consistency.py`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json rtl sim include sw tests tools docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md`

Results:

- `cmake --preset debug` configured successfully.
- `cmake --build --preset debug` built all simulation and software targets
  successfully.
- `ctest --preset debug --output-on-failure` passed `21/21` tests.
- `cmake --build --preset debug --target v1_lint` passed all aggregate RTL
  lint targets with Verilator 5.046.
- `cmake --build --preset regression` passed and ran the full CTest suite.
- `ctest --preset regression --output-on-failure` passed `21/21` tests.
- `python3 tools/check_abi_consistency.py` passed and checked 50 ABI constants.
- Source/document marker scan returned no unfinished `TODO`, `FIXME`, `HACK`,
  or `TBD` entries in the checked paths.

Known limitations:

- v1 intentionally supports one descriptor in flight and one outstanding AXI4
  read/write burst per DMA engine.
- v1 requires 16-byte aligned descriptor, tensor base, and row-stride values.
- v1 implements signed INT8 GEMM with signed INT32 output only; vector
  post-processing, BF16, FP8, graph scheduling, convolution, full softmax,
  LayerNorm, and GELU remain outside v1 scope.

Remaining issues:

- None.

Next step:

- v1 plan is complete. Future work must start with a roadmap and decision-log
  update.

## Release Review Remediation

Status: Complete.

Completed work:

- Reviewed the gpt-5.5 xhigh subagent release-readiness findings and confirmed
  the valid issues.
- Fixed `npu_axi4_read_dma.sv` so AXI read response errors drain through
  `RLAST` before reporting `ERR_AXI_READ`, preventing the public top read owner
  from being stranded.
- Added restart-epoch terminal suppression in DMA, command, and GEMM blocks, and
  masked stale GEMM terminal status in `npu_top.sv` while a new descriptor is
  being fetched or issued.
- Extended `sim/top_tb.cpp` with public-boundary coverage for `16x16x16`,
  `64x64x64`, descriptor-read error drain and retry, GEMM read error, GEMM
  write error, reset-in-flight recovery, and AXI-Lite AW/W skew.
- Corrected public clear-mask constants and the C driver clear implementation
  so `include/my_npu_regs.h`, `sw/my_npu_driver.c`, RTL, and
  `docs/INTERFACE.md` agree.
- Marked ADR-0009 superseded for final v1 AW/W timing, added ADR-0016 for AXI
  error drain and terminal epoch semantics, and updated verification/interface
  documentation.
- Replaced the stale `tests/README.md` placeholder.
- Added root `README.md`, `CHANGELOG.md`, and `LICENSE`.

Verification commands:

- `cmake --build --preset debug --target npu_read_dma_tb npu_top_tb my_npu_driver_test abi_consistency_check`
- `ctest --preset debug -R 'npu_read_dma|npu_top|my_npu_driver|abi_consistency' --output-on-failure`
- `cmake --build --preset debug --target npu_read_dma_tb npu_write_dma_tb npu_command_tb npu_gemm_tb npu_top_tb dma_rtl_lint command_rtl_lint integration_rtl_lint`
- `ctest --preset debug -R 'npu_read_dma|npu_write_dma|npu_command|npu_gemm|npu_top|my_npu_driver|abi_consistency' --output-on-failure`
- `cmake --build --preset debug`
- `ctest --preset debug --output-on-failure`
- `cmake --build --preset debug --target v1_lint`
- `cmake --build --preset regression`
- `ctest --preset regression --output-on-failure`
- `rg -n 'TODO|FIXME|HACK|TBD' CMakeLists.txt CMakePresets.json README.md CHANGELOG.md LICENSE rtl sim include sw tests tools docs/ARCHITECTURE.md docs/INTERFACE.md docs/DECISIONS.md docs/VERIFICATION.md docs/ROADMAP.md`

Results:

- Focused affected-target builds passed.
- Focused affected tests passed `7/7` after the read-drain and terminal-epoch
  fixes.
- `cmake --build --preset debug` built all simulation and software targets
  successfully.
- `ctest --preset debug --output-on-failure` passed `21/21` tests.
- `cmake --build --preset debug --target v1_lint` passed all aggregate RTL lint
  targets with Verilator 5.046.
- `cmake --build --preset regression` passed and ran the full CTest suite.
- `ctest --preset regression --output-on-failure` passed `21/21` tests.
- Source/document marker scan found only the intentional roadmap discipline line
  describing the no-unexplained-TODO rule.

Known limitations:

- v1 intentionally supports one descriptor in flight and one outstanding AXI4
  read/write burst per DMA engine.
- v1 requires 16-byte aligned descriptor, tensor base, and row-stride values.
- v1 implements signed INT8 GEMM with signed INT32 output only; vector
  post-processing, BF16, FP8, graph scheduling, convolution, full softmax,
  LayerNorm, and GELU remain outside v1 scope.

Remaining issues:

- No source, documentation, build, lint, or test issues remain for local v1
  release.
- Remote publishing is pending because no Git remote is configured.

Next step:

- Create the v1 release commit and local `v1` tag, then publish after a remote
  URL is configured.

## CI Workflow And Getting Started Documentation

Status: Complete.

Completed work:

- Added `.github/workflows/cmake-single-platform.yml` based on GitHub's CMake
  single-platform workflow and customized it for the project v1 gate.
- Configured CI to run on pushes and pull requests to `master`, `v*` tags, and
  manual dispatch.
- Configured the `ubuntu-latest` CI runner to install Verilator, GCC/G++ 14,
  Python 3, and CMake 4.x before running the project presets.
- Aligned CI commands with the local release gate:
  - `cmake --preset debug`
  - `cmake --build --preset debug --parallel`
  - `ctest --preset debug --output-on-failure`
  - `cmake --build --preset debug --target v1_lint --parallel`
  - `cmake --build --preset regression --parallel`
  - `ctest --preset regression --output-on-failure`
- Added failure log artifact upload for CTest logs.
- Expanded `docs/GETTING_STARTED.md` with a GitHub Actions CI section covering
  trigger conditions, cloud toolchain, commands, failure triage, and local
  reproduction.
- Added a root `README.md` pointer to the getting-started guide.

Verification commands:

- `git diff --check`
- `python3 -c 'import sys, yaml; yaml.safe_load(open(sys.argv[1], encoding="utf-8")); print("workflow yaml parsed")' .github/workflows/cmake-single-platform.yml`
- `cmake --build --preset debug`
- `ctest --preset debug --output-on-failure`
- `cmake --build --preset debug --target v1_lint`

Results:

- Patch whitespace check passed.
- Workflow YAML parsed successfully with PyYAML.
- `cmake --build --preset debug` built all simulation and software targets
  successfully.
- `ctest --preset debug --output-on-failure` passed `21/21` tests.
- `cmake --build --preset debug --target v1_lint` passed all aggregate RTL lint
  targets with Verilator 5.046.

Remaining issues:

- None for the CI workflow and getting-started documentation update.
