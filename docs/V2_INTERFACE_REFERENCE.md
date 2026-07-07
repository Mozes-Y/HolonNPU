<!-- Generated from spec/holon_npu_v2_abi.json by tools/gen_v2_abi.py. Do not edit. -->
# HolonNPU V2 ABI 3.0 Reference

This file is generated from `spec/holon_npu_v2_abi.json`. Edit the schema
and regenerate outputs instead of editing this file by hand.

## ABI Version

- ABI version: 3.0.
- Reset value: `0x00030000`.
- Program descriptor size: `128` bytes.
- Program descriptor alignment: `16` bytes.
- Program image alignment: `4` bytes.
- Argument/completion alignment: `16` / `16` bytes.

## ABI Rules

- All multi-byte fields are little-endian.
- AXI-Lite data width is 32 bits.
- System memory addresses in registers and descriptors are 64 bits.
- One program may be active in the first V2 release.
- Program descriptor base addresses must be 16-byte aligned.
- Program images must be 4-byte aligned.
- Argument blocks and completion records must be 16-byte aligned.
- Program compatibility must be checked before frontend execution starts.
- Reserved fields must be zero.

## Register Map

| Offset | Name | Access | Reset | Description |
| ------ | ---- | ------ | ----- | ----------- |
| `0x000` | `DEVICE_ID` | `RO` | `0x4E505502` | ASCII-like device ID, NPU plus V2 marker. |
| `0x004` | `ABI_VERSION` | `RO` | `0x00030000` | Bits [31:16] ABI major, [15:0] ABI minor. |
| `0x008` | `ISA_VERSION` | `RO` | `0x00010000` | Bits [31:16] Holon ISA major, [15:0] Holon ISA minor. |
| `0x00C` | `CAP0_LO` | `RO` | `0x0000003F` | Implemented V2 capability bits [31:0]. |
| `0x010` | `CAP0_HI` | `RO` | `0x00000000` | Implemented V2 capability bits [63:32]. |
| `0x014` | `OP_CLASS_LO` | `RO` | `0x000001FF` | Implemented Holon operation classes [31:0]. |
| `0x018` | `OP_CLASS_HI` | `RO` | `0x00000000` | Implemented Holon operation classes [63:32]. |
| `0x01C` | `PROGRAM_MEM_BYTES` | `RO` | `0x00010000` | Maximum local program-memory bytes. |
| `0x020` | `LOCAL_MEM_BYTES` | `RO` | `0x00040000` | Maximum local data-scratchpad bytes. |
| `0x024` | `VECTOR_CAP0` | `RO` | `0x08100100` | Vector baseline: [15:0] max VL, [23:16] lanes, [31:24] predicate registers. |
| `0x028` | `MATRIX_CAP0` | `RO` | `0x08201010` | Matrix baseline: [7:0] ARRAY_K, [15:8] ARRAY_N, [23:16] ACC_BITS, [31:24] INPUT_BITS. |
| `0x030` | `CONTROL` | `WO` | `0x00000000` | Write-one lifecycle control pulses. At most one command bit may be set per write. |
| `0x034` | `STATUS` | `RO` | `0x00000001` | Lifecycle state bits and sticky IRQ pending bit. |
| `0x038` | `FAULT_CODE` | `RO` | `0x00000000` | Last terminal V2 fault code. |
| `0x03C` | `DEBUG_PC` | `RO` | `0x00000000` | Frontend PC snapshot for halt, fault, or debug step. |
| `0x040` | `PROGRAM_DESC_ADDR_LO` | `RW` | `0x00000000` | Program descriptor system address bits [31:0]. |
| `0x044` | `PROGRAM_DESC_ADDR_HI` | `RW` | `0x00000000` | Program descriptor system address bits [63:32]. |
| `0x048` | `DOORBELL` | `WO` | `0x00000000` | Write start pulse after descriptor address is programmed. |
| `0x04C` | `IRQ_ENABLE` | `RW` | `0x00000000` | IRQ enable bits for done, fault, halt, and debug events. |
| `0x050` | `IRQ_STATUS` | `RO` | `0x00000000` | Sticky IRQ cause bits. |
| `0x054` | `IRQ_CLEAR` | `WO` | `0x00000000` | Write-one IRQ clear bits. |
| `0x060` | `PERF_CYCLE_LO` | `RO` | `0x00000000` | Program cycle counter bits [31:0]. |
| `0x064` | `PERF_CYCLE_HI` | `RO` | `0x00000000` | Program cycle counter bits [63:32]. |
| `0x068` | `PERF_INSTRET_LO` | `RO` | `0x00000000` | Retired Holon instruction counter bits [31:0]. |
| `0x06C` | `PERF_INSTRET_HI` | `RO` | `0x00000000` | Retired Holon instruction counter bits [63:32]. |

## Lifecycle Status Bits

| Name | Value | Description |
| ---- | ----- | ----------- |
| `IDLE` | `0x00000001` | No active program and no sticky terminal state is blocking a start. |
| `LOADING` | `0x00000002` | Program descriptor, program image, or argument setup is in progress. |
| `RUNNING` | `0x00000004` | Frontend is executing the Holon program ISA. |
| `HALTED` | `0x00000008` | Frontend is halted at a precise architectural boundary. |
| `DONE` | `0x00000010` | Program completed successfully. Sticky until clear or accepted restart. |
| `FAULT` | `0x00000020` | Program, descriptor, DMA, memory, or engine fault occurred. Sticky until clear or accepted restart. |
| `IRQ_PENDING` | `0x00000040` | At least one enabled sticky IRQ cause is pending. |

## Program Descriptor Layout

| Offset | Field | C Type | Required | Description |
| ------ | ----- | ------ | -------- | ----------- |
| `0x00` | `size_bytes` | `uint16_t` | `128` | Program descriptor size in bytes. |
| `0x02` | `version` | `uint8_t` | `3` | Program descriptor ABI major version. |
| `0x03` | `program_format` | `uint8_t` | `1` | Holon V2 program image format identifier. |
| `0x04` | `holon_isa_major` | `uint16_t` | `1` | Required Holon ISA major version. |
| `0x06` | `holon_isa_minor` | `uint16_t` | `0..implemented` | Minimum Holon ISA minor version required by the program. |
| `0x08` | `required_caps` | `uint64_t` | `subset of hardware caps` | Required V2 capability bits. |
| `0x10` | `required_op_classes` | `uint64_t` | `subset of hardware op classes` | Required V2 operation class bits. |
| `0x18` | `code_addr` | `uint64_t` | `4-byte aligned` | System-memory address of the Holon program image. |
| `0x20` | `code_size_bytes` | `uint32_t` | `nonzero multiple of 4` | Program image size in bytes. |
| `0x24` | `entry_pc` | `uint32_t` | `4-byte aligned local PC` | Frontend entry PC relative to local program memory. |
| `0x28` | `arg_addr` | `uint64_t` | `16-byte aligned` | System-memory address of the argument block. |
| `0x30` | `arg_size_bytes` | `uint32_t` | `16-byte aligned size` | Argument block size in bytes. |
| `0x34` | `local_mem_bytes` | `uint32_t` | `<= local max` | Requested data scratchpad bytes. |
| `0x38` | `program_mem_bytes` | `uint32_t` | `<= program max` | Requested local program memory bytes. |
| `0x3C` | `stack_bytes` | `uint32_t` | `<= stack max` | Requested frontend stack/control bytes. |
| `0x40` | `completion_addr` | `uint64_t` | `16-byte aligned or 0` | Optional system-memory completion/status record address. |
| `0x48` | `flags` | `uint32_t` | `See flag table` | Program lifecycle, IRQ, debug, and performance flags. |
| `0x4C` | `reserved_4c` | `uint32_t` | `0` | Reserved. |
| `0x50` | `reserved_50` | `uint64_t` | `0` | Reserved. |
| `0x58` | `reserved_58` | `uint64_t` | `0` | Reserved. |
| `0x60` | `reserved_60` | `uint64_t` | `0` | Reserved. |
| `0x68` | `reserved_68` | `uint64_t` | `0` | Reserved. |
| `0x70` | `reserved_70` | `uint64_t` | `0` | Reserved. |
| `0x78` | `reserved_78` | `uint64_t` | `0` | Reserved. |

## Program Flags

| Name | Value | Description |
| ---- | ----- | ----------- |
| `IRQ_ON_DONE` | `0x00000001` | Set IRQ status when a program completes successfully. |
| `IRQ_ON_FAULT` | `0x00000002` | Set IRQ status when a program faults. |
| `CLEAR_PERF_ON_START` | `0x00000004` | Clear performance counters before program execution. |
| `DEBUG_SNAPSHOT_ON_FAULT` | `0x00000008` | Capture frontend debug snapshot on fault. |
| `VALID_MASK` | `0x0000000F` | OR of all defined program flags. |

## Control Bits

| Name | Value | Description |
| ---- | ----- | ----------- |
| `SOFT_RESET` | `0x00000001` | Cancel active work and return the V2 lifecycle to IDLE. |
| `CLEAR_TERMINAL` | `0x00000002` | Clear sticky DONE or FAULT terminal state and return to IDLE. |
| `HALT` | `0x00000004` | Request a precise frontend halt from RUNNING. |
| `RESUME` | `0x00000008` | Resume frontend execution from HALTED. |
| `DEBUG_STEP` | `0x00000010` | Request one precise debug step from HALTED if implemented. |
| `VALID_MASK` | `0x0000001F` | OR of all defined control bits. |

## IRQ Bits

| Name | Value | Description |
| ---- | ----- | ----------- |
| `DONE` | `0x00000001` | Program completed successfully. |
| `FAULT` | `0x00000002` | Program or descriptor fault occurred. |
| `HALTED` | `0x00000004` | Frontend reached a precise halted state. |
| `DEBUG_STEP` | `0x00000008` | A debug-step request was accepted. |
| `VALID_MASK` | `0x0000000F` | OR of all defined irq bits. |

## Required Operation Classes

| Name | Value | Description |
| ---- | ----- | ----------- |
| `FRONTEND_CONTROL` | `0x0000000000000001` | Frontend-control instruction class required. |
| `PREDICATE` | `0x0000000000000002` | Predicate instruction class required. |
| `VECTOR` | `0x0000000000000004` | Vector instruction classes required. |
| `QUANTIZATION` | `0x0000000000000008` | Quantization helper class required. |
| `MATRIX` | `0x0000000000000010` | Matrix micro-op class required. |
| `DMA` | `0x0000000000000020` | DMA command class required. |
| `CSR_DEBUG` | `0x0000000000000040` | CSR/debug class required. |
| `SYNC` | `0x0000000000000080` | Sync, wait, and fence class required. |
| `SYSTEM` | `0x0000000000000100` | System class required. |

## Capability Bits

| Name | Value | Description |
| ---- | ----- | ----------- |
| `PROGRAM_DESCRIPTOR` | `0x0000000000000001` | ABI 3.0 program descriptor supported. |
| `LOCAL_PROGRAM_MEMORY` | `0x0000000000000002` | Program image is loaded into local program memory before RUNNING. |
| `ARGUMENT_SCRATCHPAD_COPY` | `0x0000000000000004` | Argument block is copied into data scratchpad before RUNNING. |
| `IN_ORDER_DMA_QUEUE` | `0x0000000000000008` | Frontend-issued DMA commands complete in queue order. |
| `MATRIX_MICRO_OP` | `0x0000000000000010` | Frontend-issued INT8 matrix micro-ops supported. |
| `INTEGER_QUANT_VECTOR` | `0x0000000000000020` | Integer and quantized vector/helper operations supported. |

## Fault Codes

| Name | Value | Description |
| ---- | ----- | ----------- |
| `NONE` | `0x00` | No fault. |
| `INVALID_PROGRAM_DESCRIPTOR` | `0x01` | Program descriptor fields are invalid. |
| `UNSUPPORTED_ABI_OR_ISA` | `0x02` | ABI or Holon ISA version is unsupported. |
| `UNSUPPORTED_PROGRAM_FORMAT` | `0x03` | Program image format is unsupported. |
| `UNSUPPORTED_CAPABILITY` | `0x04` | Program requires unsupported capability bits. |
| `UNSUPPORTED_OPERATION_CLASS` | `0x05` | Program requires unsupported operation classes. |
| `ALIGNMENT` | `0x06` | Descriptor, code, argument, or completion alignment is unsupported. |
| `LOCAL_MEMORY_BOUNDS` | `0x07` | Program requested or accessed local memory out of range. |
| `ILLEGAL_INSTRUCTION` | `0x08` | Frontend decoded an illegal instruction. |
| `VECTOR_CONFIG` | `0x09` | Vector configuration is unsupported. |
| `MATRIX_ISSUE` | `0x0A` | Matrix micro-op issue failed. |
| `DMA_REQUEST` | `0x0B` | DMA command request is invalid. |
| `AXI_READ` | `0x0C` | AXI read response failed. |
| `AXI_WRITE` | `0x0D` | AXI write response failed. |
| `EXPLICIT_PROGRAM_FAULT` | `0x0E` | Program explicitly raised a fault. |
