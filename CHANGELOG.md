# Changelog

All notable project-level release changes are recorded here.

## Unreleased

### Changed

- Renamed the project identity to HolonNPU.
- Renamed the public C API, ABI headers, driver targets, tests, documentation,
  and CI branding to use `holon_npu_*` / `HOLON_NPU_*` names.
- Kept internal RTL `npu_*` names unchanged because they describe generic
  hardware blocks rather than the project brand.

## v1 - 2026-06-27

Initial roadmap-complete release.

### Added

- Roadmap-first project governance documentation.
- SystemVerilog RTL for the v1 INT8 GEMM NPU.
- AXI-Lite control plane, descriptor command processor, AXI4 DMA, tiled GEMM
  datapath, scratchpad buffers, and product top.
- Parameterized `16x16` systolic array with signed INT8 input and signed INT32
  output.
- Public C ABI headers and minimal C driver.
- Verilator/C++26 testbenches and CTest integration.
- ABI consistency checker across RTL package constants and public C headers.
- Aggregate `v1_lint` and regression targets.

### Fixed During Release Hardening

- AXI-Lite write channel handling now accepts AW-before-W and W-before-AW.
- AXI read DMA drains errored bursts through `RLAST` before reporting
  `ERR_AXI_READ`.
- Terminal done/error outputs are restart-epoch aware so clear-and-resubmit
  flows do not observe stale backend terminal state.
- Public C clear-mask constants now match the frozen RTL/documented ABI.
- Product-top tests now cover `16x16x16`, `64x64x64`, descriptor-read error
  recovery, GEMM read/write errors, reset-in-flight recovery, and AXI-Lite
  write-channel skew.

### Known Limits

- v1 supports one descriptor in flight and one outstanding AXI4 burst per DMA
  engine.
- v1 requires 16-byte aligned descriptors, tensor bases, and row strides.
- v1 implements signed INT8 GEMM to signed INT32 output only.
- Vector post-processing, BF16, FP8, graph scheduling, convolution, full
  softmax, LayerNorm, GELU, multiple queues, multiple contexts, and address
  translation are out of scope for v1.
