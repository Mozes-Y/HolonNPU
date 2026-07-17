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
  boundaries and requires every SystemVerilog source to have exactly one
  consumed semantic CMake source target.

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

## ADR-0027: V2 Programmable NPU Tile

Status: Accepted.

Decision:

- V2 is a programmable NPU tile, not a GEMM post-processing extension.
- A replaceable frontend implementation runs Holon ISA programs and schedules
  DMA, frontend-control work, vector work, and matrix work.
- The V1 B-weight-stationary INT8 matrix engine is reused as compute IP, but
  the V1 hardcoded descriptor scheduler is not the V2 control model.
- V2 starts from ABI 3.0 program descriptors rather than extending the V1 GEMM
  descriptor shape.

Impact:

- V2 implementation begins with frontend boundary, program descriptor, local
  memory, engine issue fabric, and ISA planning.
- Program binaries remain compatible across conforming frontend
  implementations.
- V1.5 remains the stable ABI 2.0 GEMM accelerator release.

## ADR-0028: Holon-Owned Vector And Matrix ISA

Status: Accepted.

Decision:

- Holon owns the complete V2 program ISA, including frontend-control,
  predicate, vector, matrix, DMA, CSR/debug, synchronization, and system
  instructions.
- Holon vector and matrix instructions are not RVV-compatible encodings.
- RVC is intentionally rejected for the NPU instruction stream so encoding and
  decode space can be used by first-class vector, matrix, predicate, DMA, CSR,
  and synchronization instruction classes.
- Frontend implementations may borrow RV32-style microarchitecture ideas, but
  RISC-V compatibility does not define the V2 program binary or constrain Holon
  vector/matrix instruction encoding.
- V2 keeps vector-length-agnostic semantics while using instruction formats
  designed for NPU kernel expressiveness.

Impact:

- The ISA must be documented and eventually represented as machine-checkable
  metadata before decoder RTL is implemented.
- Frontend replacement is an implementation choice behind a stable Holon ISA
  boundary.
- RVV and RVC compatibility tests are not part of the V2 acceptance criteria.

## ADR-0029: V2 Explicit Scratchpad/DMA Program Model

Status: Accepted.

Decision:

- V2 uses explicit program/data local memory and AXI4 DMA.
- The first V2 release does not introduce coherent caches, IOMMU integration,
  multiple contexts, or multiple descriptor queues.
- Host software submits a program descriptor with code, arguments, entry PC,
  Holon ISA version requirements, program format, required capabilities, local
  memory request, completion/status location, and lifecycle flags.
- The first V2 release loads program code into local program memory and copies
  argument data into data scratchpad before frontend execution starts.
- Frontend firmware is responsible for explicit data movement and engine
  synchronization.
- DMA completion, local-memory visibility, and host cache-maintenance rules are
  architectural contracts, not implementation side effects.

Impact:

- The ABI 3.0 schema must describe program descriptors, frontend lifecycle
  registers, capabilities, and fault codes.
- Verification must cover SPM bounds, DMA command faults, frontend lifecycle,
  and program-level execution instead of only descriptor-driven GEMM.

## ADR-0030: V2 Schema-Generated RTL ABI Package And Control Skeleton

Status: Superseded by ADR-0039.

Decision:

- V2 RTL consumes ABI 3.0 constants from a generated SystemVerilog package
  `rtl/common/npu_v2_pkg.sv`.
- `spec/holon_npu_v2_abi.json` owns V2 register offsets, reset values,
  lifecycle bits, control bits, IRQ bits, capability bits, operation-class
  bits, and fault codes.
- The first V2 RTL implementation slice is an interface-native AXI-Lite
  control/lifecycle block, not a product-top swap.
- The V2 control block exposes doorbell, descriptor base address, lifecycle
  status, fault/debug state, IRQ enable/status/clear, halt/resume/debug-step,
  soft reset, and performance-counter views.
- `CONTROL` is a write-one command register; each accepted write may set at
  most one command bit, and multi-command writes return a slave error without
  partial execution.
- The V2 program loader is a separate interface-native AXI4 read master that
  fetches and validates the ABI 3.0 program descriptor, reads program image and
  argument blocks, and emits local program/data write streams.
- `npu_v2_local_memory_core` terminates those write streams into local program
  memory and data scratchpad, then exposes synchronous word read ports for the
  later frontend/local-memory fabric.
- A focused V2 control-plane integration slice connects AXI-Lite doorbell and
  lifecycle state to the program loader and initial local memory without
  becoming the final V2 product top.
- Flattened ports are allowed only in the simulation wrapper under `sim/rtl/`.

Impact:

- V2 RTL does not hand-maintain ABI constants in parallel with the C headers and
  documentation.
- V1.5 product top remains stable while V2 RTL modules are built and verified
  behind focused tests.
- Later V2 frontend, vector, and matrix issue modules must connect to the
  control/loader blocks through stable RTL contracts and extend the first data
  scratchpad arbitration contract instead of adding ad hoc muxes.

## ADR-0031: V2 Reference Frontend And Restart-Epoch Terminal Events

Status: Superseded by ADR-0038.

Decision:

- V2 introduces `npu_frontend_if` as the stable frontend boundary for lifecycle
  control, local program-memory fetch, debug/fault state, and future DMA,
  vector, matrix, and sync issue channels.
- `npu_reference_frontend_core` is the first replaceable frontend
  implementation. It executes the Holon ISA system terminal subset:
  `system.exit`, `system.fault`, and illegal-instruction faulting.
- The focused `npu_v2_frontend_tile_core` integration slice starts the frontend
  only after the current program descriptor has been loaded into local program
  memory.
- Loader and frontend terminal events are restart-epoch aware. Stale `done` or
  `fault` levels from a previous launch must not advance the current lifecycle,
  start a frontend run, or satisfy a new test observation.
- ISA class constants consumed by RTL come from generated
  `rtl/common/npu_isa_pkg.sv`, which is generated from
  `spec/holon_npu_isa.json`.

Impact:

- Program binaries are already fetched through local program memory, but only
  the system terminal subset is executable in RTL today.
- Future frontend, vector, DMA, and matrix implementations must extend the
  stable frontend/issue contracts rather than adding private decode paths.
- Tests must model AXI ready/valid ownership explicitly; a memory-model ready
  signal may not remain asserted while the memory model is not observing
  requests.

## ADR-0032: V2 Initial Frontend-Issued DMA Load/Store Fabric

Status: Accepted.

Decision:

- The first executable V2 DMA instructions are `dma.load` and `dma.store`.
- Concrete executable instruction opcodes must be represented in
  `spec/holon_npu_isa.json` and generated into both C/C++ and SystemVerilog
  constants before RTL consumes them.
- `dma.load` and `dma.store` use a fixed 32-bit register-addressed layout: DMA
  class, generated opcode, two scalar registers containing a full 64-bit
  system address, one scalar register containing a 32-bit local byte address,
  and a 12-bit word-count-minus-one field.
- Immediate-only system addressing was rejected because it would make the
  program ISA unusable outside a tiny host-memory window. Register addressing
  keeps the instruction compact while preserving the ABI 3.0 64-bit address
  model.
- The reference frontend converts DMA instructions into the stable DMA issue
  payload `{direction, byte_count, local_addr, system_addr}` and waits for a DMA
  completion or fault event before retiring the instruction.
- `npu_v2_dma_fabric_core` is the first DMA issue consumer. It accepts one
  command at a time, issues 32-bit AXI4 read or write bursts, writes returned
  load words into data scratchpad through `npu_v2_localmem_wr_if`, and reads
  store words through `npu_v2_localmem_rd_if`.
- Store commands prefetch the local write-burst payload before issuing AXI AW,
  so a local scratchpad read fault cannot leave a partially issued AXI write
  transaction without W/B completion.
- `npu_v2_data_port_arbiter_core` defines the first shared data scratchpad
  contract. Loader writes have priority over DMA/client writes. Host/debug data
  reads and DMA/client data reads use the `npu_v2_localmem_rd_if` valid-ready
  request interface and receive owner-routed one-cycle responses.
- The read arbiter uses round-robin selection between host/debug and client
  read requests when both are valid.
- `sync.wait_dma`, `sync.fence.local`, and `sync.fence.dma` are executable
  metadata-owned instructions. Their first RTL/model implementation retires
  them as precise ordering points because the current DMA fabric is single
  command, in order, and blocks DMA instruction retirement until completion.
- Sync instruction operand and immediate fields must be zero in this ISA slice;
  non-zero fields are illegal instructions until a later ISA revision assigns
  explicit event or scope operands.
- Local scratchpad range errors report the local-memory-bounds fault class;
  malformed DMA requests report the DMA-request fault class; AXI read response
  errors report the AXI-read fault class; AXI write response errors report the
  AXI-write fault class.
- Vector and matrix local-memory clients are integrated through
  interface-native arbitration. Multi-entry DMA queues remain future work.

Impact:

- The V2 C++ architectural simulator now executes `dma.load`, `dma.store`,
  `sync.wait_dma`, `sync.fence.local`, and `sync.fence.dma` instructions in
  addition to direct DMA helper APIs.
- Frontend tile tests prove a Holon program can load system-memory words into
  scratchpad and store scratchpad words back to system memory through the DMA
  issue path.
- Future DMA expansion must extend the ISA metadata and stable issue/event
  contracts rather than adding private frontend encodings.

## ADR-0033: V2 Initial Vector Engine Slice

Status: Superseded by ADR-0035.

Decision:

- Concrete V2 vector config, vector memory, and vector ALU opcodes are owned by
  `spec/holon_npu_isa.json` and generated into C/C++ and SystemVerilog
  constants before model or RTL decode consumes them.
- The first vector RTL slice is `npu_v2_vector_engine_core`. It uses
  interface-native issue/result streams.
- The first supported operations are `set_vl`, contiguous local `i32`
  load/store, and `i32` add/sub/min/max/equality/less-than/logical-shift-left,
  logical-shift-right, and arithmetic-shift-right.
- Arithmetic add/sub use two's-complement wraparound. Comparisons are signed and
  write `i32` 1 or 0. Shift counts use the low five bits of the RHS lane.
- The first RTL slice assumes all lanes from 0 to `VL-1` are active. Predicate
  register operations and inactive-lane preservation remain for a later vector
  slice.
- The vector engine retains its focused module test through
  `sim/rtl/vector/npu_v2_vector_engine_test_wrapper.sv`, and is also integrated
  into `npu_v2_frontend_tile_core` through backpressured issue/result streams.
- `npu_v2_engine_data_arbiter_core` merges DMA and vector scratchpad traffic
  with round-robin selection and owner-routed local read responses.
- Program-level vector RTL tests use the same program words and initial
  scratchpad data as the C++26 architectural simulator and compare final data
  scratchpad values.

## ADR-0034: V2 Capability Registers Report Implemented Hardware

Status: Accepted.

Decision:

- ABI 3.0 capability and operation-class reset values describe the RTL present
  in the integrated tile, not the eventual V2 roadmap.
- Baseline integer-vector support and quant-vector support use separate
  capability bits. A design may advertise the integer subset without claiming
  clip, saturate, requant, or other quant helpers.
- The current integration advertises program descriptor, local program memory,
  argument copy, in-order DMA, integer vector, quant vector, matrix micro-op,
  and one predicate register, plus frontend-control, predicate, vector,
  quantization, matrix, DMA, CSR/debug, sync, and system operation classes.
- `VECTOR_CAP0` reports the implemented max VL, physical execution lanes, and
  predicate-register count. `MATRIX_CAP0` reports the implemented tile shape,
  accumulator width, and operand width.
- `tools/check_v2_abi.py` checks that capability reset values are defined and
  consistent with their operation-class and detail registers.

Impact:

- Program descriptor validation rejects programs that require unimplemented
  capabilities or operation classes before frontend start.
- Future engine phases must update the schema, generated artifacts, tests, and
  capability reset values in the same change that makes the feature executable.

## ADR-0035: Type-Orthogonal Vector ISA And Dedicated Local-Memory Writes

Status: Accepted.

Decision:

- Vector element width and signedness are architectural configuration state,
  not separate opcode identities. Generic vector memory and ALU opcodes consume
  the active configuration, preserving opcode space for predicate, quant,
  reduction, and permute classes.
- The initial implemented widths are signed and unsigned 8, 16, and 32 bits.
  Add/sub wrap at element width; min/max and less-than use configured
  signedness; shift counts are reduced modulo element width.
- Local writes use `npu_v2_localmem_wr_if`, which carries address, 32-bit data,
  byte strobes, and an explicit response. Generic valid-ready streams are not a
  local-memory request protocol and must not be used to pack address/data.
- Program and data memory writes complete only after the response. Arbiters
  preserve request and response ownership, and narrow vector stores preserve
  bytes outside the selected lane.

Impact:

- ISA metadata, generated C/SystemVerilog constants, the C++ architectural
  simulator, RTL vector engine, and tests share one type-orthogonal encoding.
- Loader, DMA, vector, integration, and local-memory RTL use the dedicated write
  interface; the interface-usage checker makes this ownership contract
  machine-checkable.
- Predicate and quant behavior can extend vector state and opcode classes
  without duplicating every arithmetic opcode per data type.

## ADR-0036: Explicit VLA Predicate State

Status: Accepted.

Decision:

- Predicate state is an architectural resource selected explicitly by vector
  ALU and memory instructions; it is not hidden testbench state.
- `predicate.ptrue` derives active lanes from VL. `predicate.load` imports an
  aligned bit-packed 32-bit mask from data scratchpad.
- Inactive ALU and load lanes preserve their destination. Inactive stores do
  not issue local-memory writes.
- The first implementation exposes one predicate register while retaining
  encoded register fields and a capability count for future scaling.

Impact:

- Directed and deterministic-random tests exercise predicate state through
  executable Holon instructions, never through simulator-only mutation APIs.
- ISA metadata, generated constants, ABI capability bits, simulator semantics,
  frontend dispatch, vector RTL, and coverage change together.

## ADR-0037: V2 Integer/Quant Helper Completion

Status: Accepted.

Decision:

- The V2 first-release vector state is type-orthogonal across signed/unsigned
  8-, 16-, and 32-bit elements.
- Saturating add/sub is selected by vector configuration; wraparound remains
  the default.
- Select, gather, zip/unzip, 4x4 transpose, sum/min/max reductions, and
  fixed-point requantization are first-release helper operations.
- Requantization owns multiplier, nearest-even shift, zero point, and clamp
  bounds through a local command record. RTL and the C++ model share the same
  semantics.

## ADR-0038: Complete Holon Frontend Control Path

Status: Accepted.

Decision:

- The reference frontend executes the Holon ISA; it is not a RISC-V binary
  compatibility layer.
- Sixteen 32-bit scalar registers, with `s0` hardwired to zero, support
  immediate materialization, add/add-immediate, aligned local load/store, and
  equality/inequality branches.
- Engine instructions retire only after their result handshake. Local-memory,
  engine, branch-target, and illegal-instruction faults are precise at the
  issuing PC.
- Halt is sticky until a precise instruction boundary. Resume and debug-step
  cannot be mistaken for a new halt event.

## ADR-0039: V2 Product Top And Ordered Completion Records

Status: Accepted.

Decision:

- `npu_v2_top` is the ABI 3.0 SoC pin boundary; internal product connections
  remain interface-native.
- The tile integrates loader, local memory, reference frontend, DMA, vector,
  matrix, AXI arbitration, control registers, and completion writer.
- A nonzero completion address requests a schema-defined 32-byte record. The
  write response must complete before terminal MMIO state or IRQ is visible.
- Completion write failure becomes `AXI_WRITE` fault. Pre-execution descriptor
  and load faults never write through an untrusted completion address.
- Descriptor flags gate done/program-fault IRQ, optional performance reset, and
  fault-PC snapshot. Validation/load faults raise fault IRQ independently of
  untrusted descriptor flags.

## ADR-0040: Public C++26 Program Runtime

Status: Accepted.

Decision:

- ISA encoders, `program_builder`, program capability metadata, and example
  kernels live in the public `holon_npu_runtime` target.
- The architectural simulator consumes the runtime; product program
  construction never depends on simulator-private code.
- Example programs cover vector add, ReLU, reduction, requantization,
  transpose, and INT8 GEMM. Tests execute the same public image objects through
  the architectural simulator and integrated RTL tile.

## ADR-0041: Executable CSR/Debug Class And Metadata-Owned Coverage

Status: Accepted.

Decision:

- ISA 1.0 implements `csr_debug.read` for PC, retired-instruction count,
  program-image size, and active local-memory size. Unknown selectors fault
  precisely and do not retire.
- Every implemented ISA class must contain at least one instruction, and every
  class/instruction coverage name in `spec/holon_npu_isa.json` must exist in the
  typed C++ coverage registry. `tools/check_isa.py` enforces both rules.
- ABI operation-class capability bits advertise CSR/debug only in the same
  change that connects model, frontend RTL, tests, and generated artifacts.

## ADR-0042: Firmware-Owned Matrix Tile Traversal

Status: Accepted.

Decision:

- `matrix.gemm` remains a tile-level synchronous micro-op; full GEMM traversal
  is program/runtime policy, not a descriptor-specific RTL scheduler.
- The public C++26 runtime validates non-overlapping local-memory regions and
  emits one 32-byte command plus one matrix instruction per M/N/K tile. The
  first K tile clears, later K tiles accumulate, and the last K tile stores.
- The runtime planner and integrated RTL are checked at `1x1x1`, `16x16x16`,
  `17x19x23`, and `64x64x64`; module tests additionally cover deterministic
  random signed INT8 tiles and padded strides.

## ADR-0043: Precise Sync And Address-Range Contracts

Status: Accepted.

Decision:

- Sync instructions retire through the frontend `sync_issue` valid-ready
  contract. A serial tile may acknowledge immediately, but the interface
  remains an architectural replacement boundary.
- Descriptor, program image, argument, completion-record, and DMA system ranges
  must not wrap the 64-bit physical address space.
- Descriptor arguments occupy the bottom of requested data memory and the
  reserved frontend stack occupies the top; their combined size must not exceed
  the requested allocation.

Impact:

- RTL, the C23 driver, the C++26 architectural model, schema checks, and tests
  reject the same malformed ranges before issuing memory traffic.
- Future asynchronous engine implementations can apply ordering behind the
  existing sync handshake without changing the program ISA.
