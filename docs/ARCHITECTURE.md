# HolonNPU Architecture

This document describes the v1 architecture that later RTL phases must
implement. Interfaces and ABI details are frozen in `docs/INTERFACE.md`.

## Scope

v1.5 is a descriptor-driven INT8 GEMM accelerator controlled by a RISC-V CPU. The
CPU programs the device through AXI-Lite registers. The NPU fetches one GEMM
descriptor and matrix data over AXI4, computes with a systolic matrix engine,
and writes INT32 results back to system memory. v1.5 keeps ABI 2.0 values
unchanged while making the ABI generated from a schema and adding assertions and
coverage gates around the existing architecture.

## ABI Source Of Truth

The only editable ABI/register/descriptor source is:

- `spec/holon_npu_abi.json`

`tools/gen_abi.py` generates and checks:

- `rtl/common/npu_pkg.sv`
- `include/holon_npu_regs.h`
- `include/holon_npu_desc.h`
- `docs/INTERFACE.md`

Generated files are checked in for reviewability, but hand edits are not
allowed. Any public ABI change must update the schema first and regenerate all
outputs.

## Top-Level Blocks

### Control Plane

Responsibilities:

- Implement the 32-bit AXI-Lite register map in `docs/INTERFACE.md`.
- Store the 64-bit descriptor base address.
- Accept a doorbell write only when the design is not busy.
- Track `IDLE`, `BUSY`, `DONE`, and `ERROR` states.
- Maintain sticky done/error and interrupt status bits.
- Expose performance counters.
- Support software reset and clear pulses.

State model:

- Reset state is idle with no done, error, or IRQ pending.
- A valid doorbell moves the design to busy and starts descriptor fetch.
- Successful completion moves to done.
- Validation, DMA, or internal failures move to error.
- Done and error terminal states are cleared by software before the next command
  or automatically by a valid doorbell, as defined in the interface spec.

Phase ownership:

- ABI freeze in Phase 2.
- RTL implementation in Phase 6.

Phase 6 implementation:

- `npu_control_regs.sv` implements the documented AXI-Lite register map.
- AXI-Lite write address and write data channels are accepted independently and
  paired internally before a write response is generated.
- Read-only register writes, unmapped accesses, doorbell writes while busy,
  reserved-bit writes, and unsupported partial writes to pulse-control registers
  return `SLVERR`.
- `DESC_ADDR_LO` and `DESC_ADDR_HI` are writable with byte strobes.
- `DOORBELL.START` emits a one-cycle `command_start_o` pulse and sets busy.
- Backend inputs report command/datapath busy, completion, error, IRQ intent,
  and descriptor-requested performance-counter clear events.
- `soft_reset_o` and `clear_perf_o` expose architectural control pulses to the
  top-level backend wiring.
- `STATUS.IDLE` means `not busy`, so terminal done/error states report
  `IDLE=1` as specified in `docs/INTERFACE.md`.
- Done/error IRQ status bits are sticky until cleared by `IRQ_STATUS` RW1C or
  `CLEAR`.

### Command Processor

Responsibilities:

- Observe valid doorbell events from the control plane.
- Fetch exactly one 128-byte descriptor from the programmed 64-bit address.
- Validate descriptor size, version, opcode, flags, dimensions, alignment, and
  reserved fields.
- Issue one GEMM command to the datapath.
- Report completion or a documented error code.

Descriptor queue model:

- v1 supports a single descriptor in flight.
- Software owns descriptor storage and must not modify a descriptor after
  ringing the doorbell until hardware reaches done or error.
- Hardware does not prefetch a second descriptor.
- Doorbell writes while busy are rejected at the AXI-Lite control plane.

Phase ownership:

- ABI freeze in Phase 2.
- RTL implementation in Phase 8.

Phase 8 implementation:

- `npu_command_processor.sv` consumes `start_i` and a 64-bit descriptor address.
- Descriptor fetch uses `npu_axi4_read_dma.sv` for one aligned 128-byte AXI4
  read.
- Descriptor bytes are decoded according to `docs/INTERFACE.md`.
- Validation covers descriptor size, version, opcode, flags, M/N/K dimensions,
  tensor address and row-stride alignment, minimum row strides, and all reserved
  fields.
- Valid descriptors raise `command_valid_o` until `command_ready_i`.
- The decoded command exposes M, N, K, A/B/C base addresses, A/B/C row strides,
  and descriptor flags needed for IRQ/performance behavior.
- Invalid descriptors raise `error_o` with the documented error code and do not
  issue a command.

### DMA

Responsibilities:

- AXI4 read path for descriptor and input tensor data.
- AXI4 write path for output tensor data.
- Generate aligned 128-bit `INCR` bursts.
- Limit v1 to one outstanding read and one outstanding write.
- Convert AXI read/write response failures into documented error codes.

v1 constraints:

- AXI4 address width is 64 bits.
- AXI4 data width is 128 bits.
- Descriptor fetch is a single aligned 128-byte read.
- Tensor base addresses and row strides are 16-byte aligned.
- Non-16 matrix dimensions are supported through padded strides and tile masks.
- DMA requests are whole 128-bit beats: base address must be 16-byte aligned and
  byte count must be a nonzero multiple of 16.
- v1 Phase 7 supports one outstanding burst at a time per DMA engine.

Phase 7 implementation:

- `npu_axi4_read_dma.sv` issues aligned AXI4 read bursts, splits transfers at
  16-beat maximum burst length, streams 128-bit read data, and reports read
  response errors as `ERR_AXI_READ`.
- `npu_axi4_write_dma.sv` issues aligned AXI4 write bursts, consumes a 128-bit
  input stream, writes full-beat strobes, and reports write response errors as
  `ERR_AXI_WRITE`.
- Both DMA engines reject unaligned base addresses, zero byte counts, and byte
  counts that are not multiples of 16 before issuing AXI traffic.
- `sim/rtl/dma/npu_read_dma_test_top.sv` and
  `sim/rtl/dma/npu_write_dma_test_top.sv` expose 128-bit data as two 64-bit
  halves for C++ memory-model tests.

Phase 9 integration:

- `npu_gemm_accelerator.sv` uses one shared read DMA for A tile row loads and B
  stationary-weight row loads.
- `npu_top_core` arbitrates the single external AXI4 read channel between
  descriptor fetch and GEMM tensor reads through `npu_axi4_if`. Because v1 has
  one outstanding read, the arbiter records the owner of each accepted AR
  request and routes R beats back to the matching client until `RLAST`.
- The integration scheduler issues one aligned 16-byte DMA read for each active
  A row and each active B K-row in the current tile.
- One write DMA writes C in 16-byte chunks, four INT32 outputs per beat.
- Edge tiles write only the active 4-column chunks needed for the logical N
  dimension; padding lanes inside the final chunk are written as zero.

Phase ownership:

- Interface assumptions frozen in Phase 2.
- RTL implementation and simulated memory model in Phase 7.

### Scratchpad And Tile Buffers

Responsibilities:

- Store and time local tile data needed by the matrix engine.
- Feed the matrix engine at the cadence required by the B-weight-stationary
  systolic array.
- Generate masks for partial M, N, and K edge tiles.

v1 data layout:

- A is row-major signed INT8 with byte stride `a_row_stride_bytes`.
- B is row-major signed INT8 with byte stride `b_row_stride_bytes`.
- C is row-major signed INT32 with byte stride `c_row_stride_bytes`.

Phase 5 reusable infrastructure:

- `npu_banked_scratchpad.sv` provides a parameterized banked storage primitive
  with explicit write/read range error flags.
- `npu_i8_tile_buffer.sv` wraps the scratchpad for A and B signed INT8 tile
  storage.
- `npu_c_accum_buffer.sv` wraps the scratchpad for signed INT32 C accumulation
  and output storage.
- `npu_tile_mask.sv` generates row, column, and K masks for partial edge tiles.
- `npu_ping_pong_ctrl.sv` exposes an observable load, compute, store, done
  schedule and toggles between two banks after each stored tile.

Product-active v1.1 datapath:

- `npu_gemm_tile_scratchpad.sv` stores A tile rows and generates K-lane A
  wavefront values, column masks, K masks, and zero psum injection timing.
- B tile rows are not stored in `npu_gemm_tile_scratchpad.sv`; the GEMM
  scheduler loads them directly into stationary PE weight registers.
- C tile partial sums are accumulated inside `npu_gemm_accelerator.sv` before
  writeback. The standalone C accumulator buffer remains reusable Phase 5
  infrastructure covered by the tiling tests, not the product top's active C
  storage.

Phase 5 test geometry:

- Two ping-pong banks.
- 256 entries per bank.
- 8-bit A/B tile entries.
- 32-bit C tile entries.
- `16x16x16` tile mask granularity.

Later phases may tune internal capacity, but the valid behaviors, range-error
signaling, and mask semantics must remain compatible with the Phase 5 tests.

Phase 9 integration:

- `npu_gemm_accelerator.sv` instantiates `npu_gemm_tile_scratchpad.sv` for
  explicit `16x16` A tile storage and wavefront generation.
- A tile row is loaded from `A[m_base + row][k_base + 0..15]`.
- B tile rows are loaded from `B[k_base + k][n_base + 0..15]` and written into
  stationary PE weight registers before compute starts.
- The scratchpad produces the A wavefront, K masks, column masks, and top psum
  valid bits for the systolic array.
- The scheduler clears the C tile accumulator once per M/N output tile, then
  accumulates streamed partial sums across all K tiles before writeback.
- M, N, and K edge behavior is handled by row, column, and K masks plus padded
  16-byte row strides.

Phase ownership:

- RTL implementation in Phase 5.

### Matrix Engine

Responsibilities:

- Signed INT8 multiply.
- Signed INT32 partial-sum propagation and accumulation.
- Parameterized `ARRAY_K x ARRAY_N` B-weight-stationary systolic array.
- Target v1 configuration of `16x16`.
- Valid and mask propagation.

Dataflow:

- v1.1 uses B-weight-stationary dataflow.
- Each PE keeps one signed INT8 B weight for the active K/N tile position.
- A operands enter the left boundary by K lane. `A[m,k]` is injected on cycle
  `m + k` and propagates right across N columns.
- Zero partial sums enter the top boundary by N column. A psum for `C[m,n]`
  enters on cycle `m + n` and propagates down K lanes.
- Each PE emits `psum_out = psum_in + A * B_weight` when its A and psum inputs
  are valid and masks are active; otherwise it passes the psum through.
- `C_partial[m,n]` leaves the bottom of the array on cycle
  `m + ARRAY_K - 1 + n` and is accumulated in the C tile buffer.
- The GEMM scheduler runs a 47-cycle wavefront for each physical `16x16x16`
  tile, including inactive tail lanes, so streamed outputs clear the array.
- Edge masks suppress invalid columns and K lanes while inactive rows simply do
  not inject A/psum work.
- C writeback emits signed INT32 values; no saturation, scaling, activation, or
  accumulation with existing memory is performed in v1.

Phase ownership:

- PE and array implementation in Phase 4.
- Integration in Phase 9.

Phase 9 implementation:

- `npu_gemm_accelerator.sv` consumes the Phase 8 decoded command valid/ready
  interface rather than re-decoding descriptors.
- The scheduler iterates M/N/K in `16x16x16` tiles.
- The systolic array is cleared for each output tile, B weights are reloaded for
  each K tile, and streamed partial sums are accumulated in the C tile buffer.
- The product core `npu_top_core` connects AXI-Lite control, descriptor command
  processing, GEMM execution, completion/error status, IRQ generation, soft
  reset, performance-counter clear, and AXI4 read/write channels with
  SystemVerilog interfaces.
- The public product top `npu_top.sv` is the SoC pin boundary. It only converts
  external flattened pins into `npu_axi_lite_if` and `npu_axi4_if`, then
  instantiates `npu_top_core`.
- `stage_o` exposes idle, load A, load B, compute, store, done, and error stages
  so wave dumps and tests can locate each major operation.

### Software Driver

Responsibilities:

- Provide a minimal C API for firmware and host tests.
- Share the frozen register and descriptor ABI through common headers.
- Build 128-byte GEMM descriptors with all reserved fields zeroed.
- Reject invalid arguments before touching MMIO where software can validate the
  condition locally.
- Submit one descriptor by writing descriptor address registers and
  `DOORBELL.START`.
- Poll or wait for done/error, read terminal error code, clear sticky state, and
  read performance counters.

Phase 10 implementation:

- `include/holon_npu_regs.h` defines the v1 register offsets, status bits,
  interrupt bits, clear masks, reset constants, and hardware error codes.
- `include/holon_npu_desc.h` defines `holon_npu_gemm_desc_t` and
  `holon_npu_gemm_config_t`, with compile-time descriptor size and offset checks.
- `sw/holon_npu_driver.h` exposes the driver API and result/status/performance
  types.
- `sw/holon_npu_driver.c` implements init, capability reads, descriptor build,
  submit, poll, wait, error read, clear, and performance read operations.
- `holon_npu_submit` checks descriptor physical address alignment and
  `STATUS.BUSY` before writing descriptor address registers or doorbell.
- `holon_npu_build_gemm_desc` validates descriptor flags, dimensions, tensor
  address alignment, row-stride alignment, and minimum row strides before
  filling the descriptor.

### Future Vector Engine

Responsibilities:

- Vector-length-agnostic post-processing after v1.
- Candidate v2 operations include bias, ReLU, requantization, transpose,
  reduction, and block softmax helpers.

Phase ownership:

- Not part of v1.
- No RTL implementation is allowed before roadmap and decision-log updates.

## v1 Execution Flow

1. Software writes `DESC_ADDR_LO` and `DESC_ADDR_HI`.
2. Software optionally enables interrupts in `IRQ_ENABLE`.
3. Software writes `DOORBELL.START=1`.
4. Control plane rejects the doorbell if busy; otherwise it enters busy state.
5. Command processor fetches the 128-byte descriptor through AXI4 read DMA.
6. Command processor validates descriptor fields.
7. DMA loads A and B tiles into scratchpad/tile buffers.
8. Matrix engine computes signed INT8 x signed INT8 into signed INT32 using
   B-weight-stationary PE weights and streamed psum accumulation.
9. DMA writes C output tiles to system memory.
10. Control plane updates done/error, interrupt status, and performance
    counters.

## Clock And Reset

v1 convention:

- v1 uses one clock domain.
- Internal non-AXI RTL uses `clk_i` and active-low asynchronous reset
  `rst_ni`.
- AXI-facing interfaces use `aclk_i` and active-low `aresetn_i`, matching common
  AXI naming.
- Common storage modules reset to empty/invalid state. Stored data resets to
  zero when the storage is externally observable.
- All stateful modules must document reset values in RTL or the interface
  specification.

## Assertions And Coverage Hooks

v1.5 treats internal self-checking as part of the architecture contract:

- RTL uses native named SystemVerilog `assert property` and `cover property`
  declarations; project-owned macros must not control verification behavior.
- `npu_vr_if`, `npu_axi_lite_if`, and `npu_axi4_if` assert stable payloads
  while a source is valid and backpressured.
- Control, DMA, command, GEMM, and top modules assert local invariants such as
  legal terminal states, valid burst profiles, invalid descriptor suppression,
  stage legality, and stable AXI read ownership.
- Coverage builds enable Verilator structural coverage plus named functional
  coverage points collected by C++ testbenches through the typed
  `coverage_point` registry and `test_run` runtime.

Assertions are enabled in normal debug, regression, and coverage builds. User
coverage points are enabled only in the coverage build tree.

## Common RTL Infrastructure

Phase 3 provides reusable infrastructure only:

- `npu_pkg.sv`: generated ABI constants and common error enum values.
- `npu_vr_if.sv`: canonical valid-ready stream interface used by FIFO, skid
  buffer, register slice, DMA stream ports, command issue, GEMM, and top-level
  internal command plumbing.
- `npu_axi_lite_if.sv`: canonical AXI-Lite interface used by
  `npu_control_regs_core` and `npu_top_core`.
- `npu_axi4_if.sv`: canonical AXI4 interface used by read/write DMA cores,
  command descriptor fetch, GEMM tensor DMA, and `npu_top_core` arbitration.
- `npu_fifo.sv`: parameterized valid-ready FIFO using `npu_vr_if`.
- `npu_skid_buffer.sv`: single-entry skid buffer for backpressure cuts.
- `npu_register_slice.sv`: one-entry registered valid-ready slice.

Core boundary convention:

- `rtl/` contains product/core RTL and common protocol definitions.
- `sim/rtl/` contains simulation-only SystemVerilog harnesses used by
  Verilator/C++ tests.
- Product/internal RTL connects bus-like protocols through SystemVerilog
  interfaces and modports, not ad hoc flattened signal bundles.
- `npu_top.sv` is the only product pin-boundary adapter. It is not used as an
  internal connection scheme.
- Flattened `*_test_wrapper.sv` modules exist only for Verilator/C++ testbench
  access under `sim/rtl/` and are not part of the product RTL architecture.
- `tools/check_rtl_interface_usage.py` enforces interface usage and prevents
  test harnesses from entering `rtl/` or product source sets.

Valid-ready convention:

- A transfer happens when `valid && ready` is high on a rising clock edge.
- A source must keep `valid` and `data` stable while `valid=1` and `ready=0`.
- A sink may change `ready` at any cycle.
- Reset clears all internally stored valid bits.
- Combinational pass-through is allowed only in modules that document it, such as
  the skid buffer when empty.

## Performance Counters

v1 exposes these counters:

- Total cycles.
- Busy cycles.
- Completed descriptor count.
- Terminal error count.

Counters reset on hardware reset, `CONTROL.SOFT_RESET`, `CLEAR.PERF`, or a
descriptor with `CLEAR_PERF_ON_START`.

Phase 9 integration:

- The control-plane register file is the architectural counter view.
- `PERF_CYCLES` counts cycles while a descriptor is architecturally in flight.
- `PERF_BUSY_CYCLES` counts cycles where the command or GEMM backend reports
  active work.
- `CLEAR_PERF_ON_START` clears architectural counters when the decoded
  descriptor is accepted by the GEMM backend.
- The GEMM accelerator also keeps datapath-local counters for direct module
  testing; `npu_top.sv` exposes the architectural counters through AXI-Lite.

## Phase 3+ Implementation Notes

- Common interfaces and modules must be created before control, DMA, command, or
  matrix engine RTL depends on them.
- Later phases must not change register offsets or descriptor layout without a
  new decision record.
- ABI constants must be generated from `spec/holon_npu_abi.json`. Generated
  RTL/C/doc outputs are byte-compared in CI.

## Open Items

- None for v1. Future work must enter through the roadmap and decision log.
