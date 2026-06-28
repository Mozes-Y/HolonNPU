# HolonNPU Interface Specification

This document defines the frozen v1 external ABI. RTL, C++ tests, and software
must match the values in this file. Any incompatible change requires a roadmap
update and a decision record before code changes.

## ABI Rules

- ABI version: 1.0.
- All multi-byte fields are little-endian.
- AXI-Lite data width is 32 bits.
- AXI-Lite register offsets are byte offsets and must be 32-bit aligned.
- System memory addresses in registers and descriptors are 64 bits.
- v1 supports one descriptor in flight.
- v1 descriptor size is 128 bytes.
- v1 descriptor base address must be 16-byte aligned.
- v1 tensor base addresses must be 16-byte aligned.
- v1 tensor row strides must be 16-byte aligned.
- Reserved register bits read as zero unless otherwise stated.
- Software must write zero to reserved fields and bits.
- Hardware must reject nonzero reserved descriptor fields with
  `ERR_RESERVED_NONZERO`.

## Constants

| Name | Value | Description |
| ---- | ----- | ----------- |
| `HOLON_NPU_ABI_MAJOR` | `1` | Major ABI version. |
| `HOLON_NPU_ABI_MINOR` | `0` | Minor ABI version. |
| `HOLON_NPU_DESC_SIZE` | `128` | GEMM descriptor size in bytes. |
| `HOLON_NPU_DESC_ALIGN` | `16` | Descriptor base alignment in bytes. |
| `HOLON_NPU_TENSOR_ALIGN` | `16` | Tensor base and row-stride alignment. |
| `HOLON_NPU_OPCODE_GEMM_I8I8I32` | `1` | Signed INT8 GEMM with INT32 output. |
| `HOLON_NPU_ARRAY_M` | `16` | v1 target systolic-array rows. |
| `HOLON_NPU_ARRAY_N` | `16` | v1 target systolic-array columns. |
| `HOLON_NPU_INPUT_BITS` | `8` | A and B operand width. |
| `HOLON_NPU_ACC_BITS` | `32` | Accumulator and output width. |

## AXI-Lite Control Interface

### Protocol

- Address width: at least 12 bits for the v1 4 KiB register aperture.
- Data width: 32 bits.
- Write strobes: byte strobes are supported.
- AW and W may arrive in the same cycle or on independent cycles. Hardware
  pairs one accepted address with one accepted data beat before issuing `B`.
- Supported responses:
  - `OKAY` for successful reads and writes.
  - `SLVERR` for unmapped accesses, writes to read-only registers, unsupported
    partial writes to control registers, and doorbell writes while busy.
- All registers reset to the values shown below.
- Writes to read-only registers have no side effect and return `SLVERR`.
- Reads from write-only registers return zero and `OKAY`.
- Unmapped reads and writes return `SLVERR`.

### Register Map

| Offset | Name | Width | Access | Reset | Description |
| ------ | ---- | ----- | ------ | ----- | ----------- |
| `0x000` | `DEVICE_ID` | 32 | RO | `0x4E505501` | ASCII-like device ID, `NPU` plus v1 marker. |
| `0x004` | `ABI_VERSION` | 32 | RO | `0x00010000` | Bits `[31:16]` major, `[15:0]` minor. |
| `0x008` | `CAP0` | 32 | RO | `0x0000003F` | Feature capability bits. |
| `0x00C` | `CAP1` | 32 | RO | `0x08201010` | Array and arithmetic widths. |
| `0x010` | `CONTROL` | 32 | WO | `0x00000000` | Write-one pulse controls. |
| `0x014` | `STATUS` | 32 | RO | `0x00000001` | Current state and sticky indicators. |
| `0x018` | `ERROR_CODE` | 32 | RO | `0x00000000` | Last terminal error code. |
| `0x01C` | `IRQ_ENABLE` | 32 | RW | `0x00000000` | Interrupt enable bits. |
| `0x020` | `IRQ_STATUS` | 32 | RW1C | `0x00000000` | Sticky interrupt causes. |
| `0x024` | `DOORBELL` | 32 | WO | `0x00000000` | Write `START=1` to submit descriptor. |
| `0x028` | `DESC_ADDR_LO` | 32 | RW | `0x00000000` | Descriptor base address bits `[31:0]`. |
| `0x02C` | `DESC_ADDR_HI` | 32 | RW | `0x00000000` | Descriptor base address bits `[63:32]`. |
| `0x030` | `CLEAR` | 32 | WO | `0x00000000` | Write-one pulse clear controls. |
| `0x034` | `RESERVED_034` | 32 | RO | `0x00000000` | Reserved. |
| `0x038` | `RESERVED_038` | 32 | RO | `0x00000000` | Reserved. |
| `0x03C` | `RESERVED_03C` | 32 | RO | `0x00000000` | Reserved. |
| `0x040` | `PERF_CYCLES_LO` | 32 | RO | `0x00000000` | Descriptor-in-flight cycles low word. |
| `0x044` | `PERF_CYCLES_HI` | 32 | RO | `0x00000000` | Descriptor-in-flight cycles high word. |
| `0x048` | `PERF_BUSY_CYCLES_LO` | 32 | RO | `0x00000000` | Backend-busy cycles low word. |
| `0x04C` | `PERF_BUSY_CYCLES_HI` | 32 | RO | `0x00000000` | Backend-busy cycles high word. |
| `0x050` | `PERF_DESC_COUNT` | 32 | RO | `0x00000000` | Completed descriptor count. |
| `0x054` | `PERF_ERROR_COUNT` | 32 | RO | `0x00000000` | Terminal error count. |

### Register Fields

| Register | Bits | Name | Reset | Description |
| -------- | ---- | ---- | ----- | ----------- |
| `CAP0` | `[0]` | `INT8_GEMM` | `1` | Signed INT8 GEMM supported. |
| `CAP0` | `[1]` | `INT32_OUTPUT` | `1` | Signed INT32 output supported. |
| `CAP0` | `[2]` | `DESC_DMA` | `1` | Descriptor fetch over AXI4 supported. |
| `CAP0` | `[3]` | `IRQ` | `1` | Interrupt output supported. |
| `CAP0` | `[4]` | `PERF_COUNTERS` | `1` | Performance counters supported. |
| `CAP0` | `[5]` | `SINGLE_QUEUE` | `1` | Single descriptor queue/in-flight model. |
| `CAP0` | `[31:6]` | `RESERVED` | `0` | Reserved. |
| `CAP1` | `[7:0]` | `ARRAY_M` | `16` | Systolic-array row count. |
| `CAP1` | `[15:8]` | `ARRAY_N` | `16` | Systolic-array column count. |
| `CAP1` | `[23:16]` | `ACC_BITS` | `32` | Accumulator/output bit width. |
| `CAP1` | `[31:24]` | `INPUT_BITS` | `8` | A/B operand bit width. |
| `CONTROL` | `[0]` | `SOFT_RESET` | `0` | Write `1` to reset internal control state and clear interrupts. Self-clearing pulse. |
| `CONTROL` | `[31:1]` | `RESERVED` | `0` | Must be written as zero. |
| `STATUS` | `[0]` | `IDLE` | `1` | No descriptor is active and no terminal state is pending. |
| `STATUS` | `[1]` | `BUSY` | `0` | Descriptor fetch, compute, or writeback is active. |
| `STATUS` | `[2]` | `DONE` | `0` | Last descriptor completed successfully. Sticky until `CLEAR.DONE`. |
| `STATUS` | `[3]` | `ERROR` | `0` | Last descriptor or illegal access failed. Sticky until `CLEAR.ERROR`. |
| `STATUS` | `[4]` | `IRQ_PENDING` | `0` | Any enabled interrupt status bit is set. |
| `STATUS` | `[31:5]` | `RESERVED` | `0` | Reserved. |
| `IRQ_ENABLE` | `[0]` | `DONE_IRQ_EN` | `0` | Enable done interrupt. |
| `IRQ_ENABLE` | `[1]` | `ERROR_IRQ_EN` | `0` | Enable error interrupt. |
| `IRQ_ENABLE` | `[31:2]` | `RESERVED` | `0` | Must be written as zero. |
| `IRQ_STATUS` | `[0]` | `DONE_IRQ` | `0` | Set when a descriptor completes and descriptor flag `IRQ_ON_DONE` is set. |
| `IRQ_STATUS` | `[1]` | `ERROR_IRQ` | `0` | Set when an error occurs and descriptor flag `IRQ_ON_ERROR` is set, or when the error happens before flags are available. |
| `IRQ_STATUS` | `[31:2]` | `RESERVED` | `0` | Reserved. |
| `DOORBELL` | `[0]` | `START` | `0` | Write `1` to start fetching descriptor at `DESC_ADDR`. |
| `DOORBELL` | `[31:1]` | `RESERVED` | `0` | Must be written as zero. |
| `CLEAR` | `[0]` | `DONE` | `0` | Write `1` to clear `STATUS.DONE` and `IRQ_STATUS.DONE_IRQ`. |
| `CLEAR` | `[1]` | `ERROR` | `0` | Write `1` to clear `STATUS.ERROR`, `ERROR_CODE`, and `IRQ_STATUS.ERROR_IRQ`. |
| `CLEAR` | `[2]` | `PERF` | `0` | Write `1` to clear performance counters. |
| `CLEAR` | `[31:3]` | `RESERVED` | `0` | Must be written as zero. |

### Register Side Effects

- `DOORBELL.START=1` is accepted only when `STATUS.BUSY=0`.
- A valid doorbell write clears `STATUS.DONE`, clears `STATUS.ERROR`, clears
  `ERROR_CODE`, and starts descriptor fetch.
- A doorbell write while busy returns `SLVERR` and has no state side effect.
- A doorbell write with reserved bits set returns `SLVERR` and has no state side
  effect.
- `CONTROL.SOFT_RESET=1` returns the control plane to reset state and cancels
  any active descriptor. The write returns `OKAY`.
- `CLEAR.DONE=1` clears `STATUS.DONE` and `IRQ_STATUS.DONE_IRQ`.
- `CLEAR.ERROR=1` clears `STATUS.ERROR`, `ERROR_CODE`, and
  `IRQ_STATUS.ERROR_IRQ`.
- `CLEAR.PERF=1` clears all performance counters.
- `PERF_CYCLES` increments while a descriptor is architecturally in flight after
  an accepted doorbell and before terminal done/error.
- `PERF_BUSY_CYCLES` increments only on in-flight cycles where the descriptor
  fetch or GEMM backend reports active work.
- `irq_o` is asserted when `(IRQ_ENABLE & IRQ_STATUS) != 0`.

## Status And Error Codes

`STATUS` is bit-based, not an enum. Legal terminal combinations are:

- Idle: `IDLE=1`, `BUSY=0`, `DONE=0`, `ERROR=0`.
- Busy: `IDLE=0`, `BUSY=1`, `DONE=0`, `ERROR=0`.
- Done: `IDLE=1`, `BUSY=0`, `DONE=1`, `ERROR=0`.
- Error: `IDLE=1`, `BUSY=0`, `DONE=0`, `ERROR=1`.

| Code | Name | Description |
| ---- | ---- | ----------- |
| `0` | `ERR_NONE` | No error. |
| `1` | `ERR_INVALID_DESC_VERSION` | Descriptor `version` is not `1`. |
| `2` | `ERR_INVALID_OPCODE` | Descriptor `opcode` is not `HOLON_NPU_OPCODE_GEMM_I8I8I32`. |
| `3` | `ERR_INVALID_DESC_SIZE` | Descriptor `size_bytes` is not `128`. |
| `4` | `ERR_INVALID_FLAGS` | Descriptor flags contain unsupported bits. |
| `5` | `ERR_UNSUPPORTED_ALIGNMENT` | Descriptor, tensor base, or row stride alignment is unsupported. |
| `6` | `ERR_AXI_READ` | AXI read response was not `OKAY`. |
| `7` | `ERR_AXI_WRITE` | AXI write response was not `OKAY`. |
| `8` | `ERR_INTERNAL_PROTOCOL` | Internal valid-ready or scheduler invariant failed. |
| `9` | `ERR_DOORBELL_BUSY` | Doorbell write was attempted while busy. Used only if the implementation chooses to count the rejected write. |
| `10` | `ERR_RESERVED_NONZERO` | A reserved descriptor field was nonzero. |
| `11` | `ERR_DIMENSION_ZERO` | M, N, or K is zero. |
| `12` | `ERR_DIMENSION_UNSUPPORTED` | A dimension exceeds the v1 implementation limit. |

## AXI4 Master Interface

### Protocol

- Address width: 64 bits.
- Data width: 128 bits.
- Burst type: `INCR` only.
- Burst length: 1 to 16 beats.
- Maximum burst payload: 256 bytes.
- Outstanding reads: 1 in v1.
- Outstanding writes: 1 in v1.
- Descriptor fetch: one aligned 128-byte read.
- Tensor reads and writes: generated as aligned 16-byte beat accesses. Edge
  tiles use byte-lane masking internally; system memory accesses remain aligned
  to the documented tensor base and row-stride constraints.
- Phase 7 DMA requests must use a 16-byte aligned base address and a nonzero
  byte count that is a multiple of 16 bytes.
- Requests that violate Phase 7 DMA alignment or size constraints fail before
  issuing AXI traffic and report `ERR_UNSUPPORTED_ALIGNMENT`.

### Response Mapping

| AXI Response | v1 Handling |
| ------------ | ----------- |
| `OKAY` | Continue. |
| `EXOKAY` | Treat as `OKAY`; exclusive access is not generated by v1. |
| `SLVERR` | Set `ERR_AXI_READ` or `ERR_AXI_WRITE`. |
| `DECERR` | Set `ERR_AXI_READ` or `ERR_AXI_WRITE`. |

## GEMM Descriptor ABI

The v1 command processor fetches exactly one 128-byte descriptor from
`DESC_ADDR_HI:DESC_ADDR_LO` after a valid doorbell write.

### Descriptor Layout

| Byte Offset | Field | Width | Required Value | Description |
| ----------- | ----- | ----- | -------------- | ----------- |
| `0x00` | `size_bytes` | 16 | `128` | Descriptor size in bytes. |
| `0x02` | `version` | 8 | `1` | Descriptor ABI version. |
| `0x03` | `opcode` | 8 | `1` | `HOLON_NPU_OPCODE_GEMM_I8I8I32`. |
| `0x04` | `flags` | 32 | See flag table | Per-command options. |
| `0x08` | `m` | 32 | `1..65535` | Rows of A and C. |
| `0x0C` | `n` | 32 | `1..65535` | Columns of B and C. |
| `0x10` | `k` | 32 | `1..65535` | Columns of A and rows of B. |
| `0x14` | `reserved_14` | 32 | `0` | Reserved. |
| `0x18` | `a_addr` | 64 | 16-byte aligned | Base address of row-major signed INT8 A matrix. |
| `0x20` | `b_addr` | 64 | 16-byte aligned | Base address of row-major signed INT8 B matrix. |
| `0x28` | `c_addr` | 64 | 16-byte aligned | Base address of row-major signed INT32 C matrix. |
| `0x30` | `a_row_stride_bytes` | 32 | 16-byte aligned | Byte stride between A rows. Must be at least `k`. |
| `0x34` | `b_row_stride_bytes` | 32 | 16-byte aligned | Byte stride between B rows. Must be at least `n`. |
| `0x38` | `c_row_stride_bytes` | 32 | 16-byte aligned | Byte stride between C rows. Must be at least `4*n`. |
| `0x3C` | `reserved_3c` | 32 | `0` | Reserved. |
| `0x40` | `reserved_40` | 64 | `0` | Reserved. |
| `0x48` | `reserved_48` | 64 | `0` | Reserved. |
| `0x50` | `reserved_50` | 64 | `0` | Reserved. |
| `0x58` | `reserved_58` | 64 | `0` | Reserved. |
| `0x60` | `reserved_60` | 64 | `0` | Reserved. |
| `0x68` | `reserved_68` | 64 | `0` | Reserved. |
| `0x70` | `reserved_70` | 64 | `0` | Reserved. |
| `0x78` | `reserved_78` | 64 | `0` | Reserved. |

### Descriptor Flags

| Bit | Name | Reset/Required | Description |
| --- | ---- | -------------- | ----------- |
| `[0]` | `IRQ_ON_DONE` | Optional | Set `IRQ_STATUS.DONE_IRQ` when command completes. |
| `[1]` | `IRQ_ON_ERROR` | Optional | Set `IRQ_STATUS.ERROR_IRQ` when command fails. |
| `[2]` | `CLEAR_PERF_ON_START` | Optional | Clear performance counters before command execution. |
| `[31:3]` | `RESERVED` | `0` | Nonzero value raises `ERR_INVALID_FLAGS`. |

### GEMM Semantics

The operation is:

```text
C[m,n] = sum(k: 0..K-1) int32(A[m,k]) * int32(B[k,n])
```

- A and B elements are signed INT8.
- C elements are signed INT32.
- Accumulation uses signed INT32 wraparound semantics matching two's-complement
  hardware arithmetic.
- A, B, and C are row-major.
- v1 does not add bias, scaling, activation, transposition, saturation, or
  accumulation with an existing C matrix.
- Non-multiple tile dimensions are valid. The implementation must mask elements
  outside M, N, or K.

## Interrupt Semantics

- `IRQ_STATUS.DONE_IRQ` is set only if descriptor flag `IRQ_ON_DONE` is set.
- `IRQ_STATUS.ERROR_IRQ` is set if descriptor flag `IRQ_ON_ERROR` is set.
- If an error occurs before descriptor flags are available, `ERROR_IRQ` is set.
- The external interrupt line is level-sensitive and asserted while any enabled
  IRQ status bit is set.
- Software clears interrupt causes with `IRQ_STATUS` write-one-to-clear or with
  matching `CLEAR` bits.

## Software API Contract

Phase 10 must implement a C API with these operations and semantics:

| Function | Purpose |
| -------- | ------- |
| `holon_npu_init(base)` | Bind a driver instance to an MMIO base pointer. |
| `holon_npu_get_caps(dev, caps)` | Read `DEVICE_ID`, `ABI_VERSION`, `CAP0`, and `CAP1`. |
| `holon_npu_build_gemm_desc(desc, cfg)` | Fill a 128-byte v1 GEMM descriptor and zero reserved fields. |
| `holon_npu_submit(dev, desc_pa)` | Write descriptor address and doorbell. |
| `holon_npu_poll(dev)` | Read `STATUS` once and return decoded state. |
| `holon_npu_wait(dev, timeout)` | Poll until done, error, or timeout. |
| `holon_npu_error(dev)` | Read `ERROR_CODE`. |
| `holon_npu_clear(dev, mask)` | Clear done, error, or performance counters. `CLEAR.DONE` and `CLEAR.ERROR` also clear their matching IRQ causes. |
| `holon_npu_read_perf(dev, perf)` | Read performance counters coherently enough for v1 software diagnostics. |

The driver must not submit a descriptor while `STATUS.BUSY=1`. The driver must
align descriptor and tensor addresses according to this ABI or return an
argument error before touching hardware. Software that needs to clear interrupt
causes without clearing terminal status writes `IRQ_STATUS` directly.
