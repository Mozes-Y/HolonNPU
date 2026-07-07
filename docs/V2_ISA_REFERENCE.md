<!-- Generated from spec/holon_npu_isa.json by tools/gen_isa.py. Do not edit. -->
# HolonNPU V2 ISA Reference

This file is generated from `spec/holon_npu_isa.json`. Edit the schema
and regenerate outputs instead of editing this file by hand.

## ISA Version

- ISA version: 1.0.
- Instruction width: 32 bits.
- Instruction alignment: 4 bytes.
- Ownership: Holon owns the complete V2 program ISA; frontend implementations are replaceable microarchitectures, not ABI owners.

## Instruction Classes

| Class | Value | Mask | Format | Fault | Coverage | Semantics | Description |
| ----- | ----- | ---- | ------ | ----- | -------- | --------- | ----------- |
| `FRONTEND_CONTROL` | `0x00000000u` | `0xF0000000u` | `rd_rs1_rs2_imm` | `ILLEGAL_INSTRUCTION` | `isa_class_frontend_control` | `frontend_control` | Integer ALU, branch, loop, address generation, and lightweight control instructions. |
| `PREDICATE` | `0x10000000u` | `0xF0000000u` | `pd_ps1_ps2_imm` | `ILLEGAL_INSTRUCTION` | `isa_class_predicate` | `predicate` | Predicate register operations, mask generation, mask combine, and active-lane tests. |
| `VECTOR_CONFIG` | `0x20000000u` | `0xF0000000u` | `vd_vl_vtype` | `VECTOR_CONFIG_FAULT` | `isa_class_vector_config` | `vector_config` | Vector length, element width, grouping, rounding, saturation, and predicate behavior. |
| `VECTOR_ALU` | `0x30000000u` | `0xF0000000u` | `vd_vs1_vs2_pred` | `UNSUPPORTED_OPERATION_CLASS` | `isa_class_vector_alu` | `vector_alu_integer` | Integer elementwise add, subtract, min, max, compare, select, shift, and fixed-point helpers. |
| `VECTOR_MEMORY` | `0x40000000u` | `0xF0000000u` | `vd_addr_stride_pred` | `LOCAL_MEMORY_BOUNDS` | `isa_class_vector_memory` | `vector_memory` | Scratchpad vector load, store, strided access, and local tile movement. |
| `VECTOR_PERMUTE` | `0x50000000u` | `0xF0000000u` | `vd_vs1_vs2_pred` | `UNSUPPORTED_OPERATION_CLASS` | `isa_class_vector_permute` | `vector_permute` | Lane shuffle, pack, unpack, and transpose helper instructions. |
| `VECTOR_REDUCTION` | `0x60000000u` | `0xF0000000u` | `vd_vs1_pred_mode` | `UNSUPPORTED_OPERATION_CLASS` | `isa_class_vector_reduction` | `vector_reduction` | Integer sum, min, max, any, all, and narrow or widen reduction helpers. |
| `QUANTIZATION` | `0x70000000u` | `0xF0000000u` | `vd_vs1_scale_pred` | `UNSUPPORTED_OPERATION_CLASS` | `isa_class_quantization` | `quantization` | Requantization, clip, saturate, scale, and zero-point helpers for integer tensors. |
| `MATRIX` | `0x80000000u` | `0xF0000000u` | `tile_operands_event` | `MATRIX_ISSUE_FAULT` | `isa_class_matrix` | `matrix_micro_op` | Matrix tile load, issue, accumulate, store, and completion-event instructions for the INT8 matrix engine. |
| `DMA` | `0x90000000u` | `0xF0000000u` | `sysaddr_spmaddr_size_event` | `DMA_REQUEST_FAULT` | `isa_class_dma` | `dma_command` | System-memory to scratchpad and scratchpad to system-memory transfer commands. |
| `CSR_DEBUG` | `0xA0000000u` | `0xF0000000u` | `rd_csr_rs1_imm` | `ILLEGAL_INSTRUCTION` | `isa_class_csr_debug` | `csr_debug` | Frontend CSRs, fault reporting, performance counters, and debug snapshots. |
| `SYNC` | `0xB0000000u` | `0xF0000000u` | `event_fence_scope` | `ILLEGAL_INSTRUCTION` | `isa_class_sync` | `sync` | Wait, fence.local, fence.dma, engine barrier, and event signaling instructions. |
| `SYSTEM` | `0xC0000000u` | `0xF0000000u` | `system_imm` | `EXPLICIT_PROGRAM_FAULT` | `isa_class_system` | `system` | Program exit, explicit fault, halt, and privileged lifecycle operations. |

## Reserved Classes

| Class | Value | Mask | Description |
| ----- | ----- | ---- | ----------- |
| `RESERVED_D` | `0xD0000000u` | `0xF0000000u` | Reserved for future floating-point or transformer extensions. |
| `RESERVED_E` | `0xE0000000u` | `0xF0000000u` | Reserved for future multi-engine and system-level extensions. |
| `RESERVED_F` | `0xF0000000u` | `0xF0000000u` | Reserved for implementation-independent expansion. |

## Architectural State

- `vector`: `vl`, `max_vl`, `element_width`, `predicate`, `rounding_mode`, `saturation_mode`.
- `frontend`: `pc`, `fault_code`, `halted`, `debug_snapshot`.
- `memory`: `program_memory`, `data_scratchpad`, `vector_register_file`, `matrix_buffers`.
