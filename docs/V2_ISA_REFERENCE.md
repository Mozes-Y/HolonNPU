<!-- Generated from spec/holon_npu_isa.json by tools/gen_isa.py. Do not edit. -->
# HolonNPU V2 ISA Reference

This file is generated from `spec/holon_npu_isa.json`. Edit the schema
and regenerate outputs instead of editing this file by hand.

## ISA Version

- ISA version: 1.0.
- Instruction width: 32 bits.
- Instruction alignment: 4 bytes.
- Ownership: Holon owns the complete V2 program ISA; frontend implementations are replaceable microarchitectures, not ABI owners.

## Initial Field Layout

- Opcode shift: 24.
- Destination/local-offset field shift: 20.
- Source/count field shift: 16.
- Source/reserved field shift: 12.
- Class mask: `0xF0000000u`.
- 4-bit field mask: `0x0000000Fu`.
- 12-bit immediate mask: `0x00000FFFu`.

## Instruction Classes

| Class | Value | Mask | Format | Fault | Coverage | Semantics | Description |
| ----- | ----- | ---- | ------ | ----- | -------- | --------- | ----------- |
| `FRONTEND_CONTROL` | `0x00000000u` | `0xF0000000u` | `rd_rs1_rs2_imm` | `ILLEGAL_INSTRUCTION` | `isa_class_frontend_control` | `frontend_control` | Integer ALU, branch, loop, address generation, and lightweight control instructions. |
| `PREDICATE` | `0x10000000u` | `0xF0000000u` | `pd_ps1_ps2_imm` | `ILLEGAL_INSTRUCTION` | `isa_class_predicate` | `predicate` | Predicate register initialization and aligned bit-packed mask load. |
| `VECTOR_CONFIG` | `0x20000000u` | `0xF0000000u` | `vd_vl_vtype` | `VECTOR_CONFIG_FAULT` | `isa_class_vector_config` | `vector_config` | Vector length, element width, signedness, rounding, and saturation configuration. |
| `VECTOR_ALU` | `0x30000000u` | `0xF0000000u` | `vd_vs1_vs2_pred` | `UNSUPPORTED_OPERATION_CLASS` | `isa_class_vector_alu` | `vector_alu_integer` | Integer elementwise add, subtract, min, max, compare, select, shift, and fixed-point helpers. |
| `VECTOR_MEMORY` | `0x40000000u` | `0xF0000000u` | `vd_addr_stride_pred` | `LOCAL_MEMORY_BOUNDS` | `isa_class_vector_memory` | `vector_memory` | Contiguous scratchpad vector load and store under explicit predication. |
| `VECTOR_PERMUTE` | `0x50000000u` | `0xF0000000u` | `vd_vs1_vs2_pred` | `UNSUPPORTED_OPERATION_CLASS` | `isa_class_vector_permute` | `vector_permute` | Lane shuffle, pack, unpack, and transpose helper instructions. |
| `VECTOR_REDUCTION` | `0x60000000u` | `0xF0000000u` | `vd_vs1_pred_mode` | `UNSUPPORTED_OPERATION_CLASS` | `isa_class_vector_reduction` | `vector_reduction` | Predicated integer sum, minimum, and maximum reductions. |
| `QUANTIZATION` | `0x70000000u` | `0xF0000000u` | `vd_vs1_scale_pred` | `UNSUPPORTED_OPERATION_CLASS` | `isa_class_quantization` | `quantization` | Requantization, clip, saturate, scale, and zero-point helpers for integer tensors. |
| `MATRIX` | `0x80000000u` | `0xF0000000u` | `tile_operands_event` | `MATRIX_ISSUE_FAULT` | `isa_class_matrix` | `matrix_micro_op` | Local-command-block issue for synchronous INT8 matrix tile operations. |
| `DMA` | `0x90000000u` | `0xF0000000u` | `sysaddr64_regs_spmaddr_reg_words12` | `DMA_REQUEST_FAULT` | `isa_class_dma` | `dma_command` | System-memory to scratchpad and scratchpad to system-memory transfer commands. |
| `CSR_DEBUG` | `0xA0000000u` | `0xF0000000u` | `scalar_rd_csr_imm12` | `ILLEGAL_INSTRUCTION` | `isa_class_csr_debug` | `csr_debug` | Read-only frontend architectural state and performance-counter snapshots. |
| `SYNC` | `0xB0000000u` | `0xF0000000u` | `event_fence_scope` | `ILLEGAL_INSTRUCTION` | `isa_class_sync` | `sync` | DMA wait plus local-memory and DMA ordering instructions. |
| `SYSTEM` | `0xC0000000u` | `0xF0000000u` | `system_imm` | `EXPLICIT_PROGRAM_FAULT` | `isa_class_system` | `system` | Program exit and explicit-fault operations; host MMIO owns halt and debug lifecycle control. |

## Encoding Constants

| Name | Value | Description |
| ---- | ----- | ----------- |
| `SCALAR_REGISTER_COUNT` | `0x00000010u` | Number of 32-bit scalar registers in the V2 architectural frontend state; s0 is hardwired to zero. |
| `SCALAR_BRANCH_SCALE` | `0x00000004u` | Byte scale applied to signed imm12 branch displacements relative to the current PC. |
| `CSR_PC` | `0x00000000u` | Read-only CSR containing the PC of the executing csr.read instruction. |
| `CSR_INSTRET_LO` | `0x00000001u` | Read-only CSR containing retired-instruction counter bits [31:0] before csr.read retires. |
| `CSR_INSTRET_HI` | `0x00000002u` | Read-only CSR containing retired-instruction counter bits [63:32] before csr.read retires. |
| `CSR_PROGRAM_SIZE_BYTES` | `0x00000003u` | Read-only CSR containing the loaded program image size in bytes. |
| `CSR_LOCAL_MEM_BYTES` | `0x00000004u` | Read-only CSR containing the active program's local scratchpad allocation in bytes. |
| `VTYPE_VL_MINUS_ONE_MASK` | `0x0000003Fu` | Vector length minus one in vector_config.set immediate bits [5:0]. |
| `VTYPE_SEW_SHIFT` | `0x00000006u` | Selected element-width encoding shift in vector_config.set immediate. |
| `VTYPE_SEW_MASK` | `0x000000C0u` | Selected element-width encoding mask in vector_config.set immediate. |
| `VTYPE_SIGNED` | `0x00000100u` | Signed integer interpretation bit in vector_config.set immediate. |
| `VTYPE_ROUND_SHIFT` | `0x00000009u` | Rounding-mode encoding shift in vector_config.set immediate. |
| `VTYPE_ROUND_MASK` | `0x00000600u` | Rounding-mode encoding mask in vector_config.set immediate. |
| `VTYPE_SATURATE` | `0x00000800u` | Saturating arithmetic enable bit in vector_config.set immediate. |
| `VTYPE_SEW_8` | `0x00000000u` | Eight-bit vector element-width encoding. |
| `VTYPE_SEW_16` | `0x00000001u` | Sixteen-bit vector element-width encoding. |
| `VTYPE_SEW_32` | `0x00000002u` | Thirty-two-bit vector element-width encoding. |
| `VTYPE_ROUND_RNE` | `0x00000000u` | Round-to-nearest, ties-to-even mode encoding. |
| `VECTOR_PREDICATE_MASK` | `0x0000000Fu` | Predicate-register selector in vector ALU immediate bits [3:0]. |
| `VECTOR_PREDICATE_RESERVED_MASK` | `0x00000FF0u` | Reserved vector ALU immediate bits that must be zero. |
| `PREDICATE_WORD_BYTES` | `0x00000004u` | Bytes loaded by the initial bit-packed predicate load instruction. |
| `DMA_WORD_BYTES` | `0x00000004u` | Granularity of frontend-issued DMA transfers. |
| `DMA_MAX_WORDS` | `0x00001000u` | Maximum words transferred by one DMA instruction using the 12-bit count-minus-one field. |
| `QUANT_COMMAND_BYTES` | `0x00000018u` | Size of the local per-tensor requantization command block. |
| `QUANT_COMMAND_ALIGN` | `0x00000004u` | Required local alignment of a requantization command block. |
| `QUANT_COMMAND_MULTIPLIER_OFFSET` | `0x00000000u` | Byte offset of the signed fixed-point multiplier. |
| `QUANT_COMMAND_SHIFT_OFFSET` | `0x00000004u` | Byte offset of the unsigned right shift in the range 0 through 31. |
| `QUANT_COMMAND_ZERO_POINT_OFFSET` | `0x00000008u` | Byte offset of the signed output zero point. |
| `QUANT_COMMAND_CLAMP_MIN_OFFSET` | `0x0000000Cu` | Byte offset of the signed inclusive clamp minimum. |
| `QUANT_COMMAND_CLAMP_MAX_OFFSET` | `0x00000010u` | Byte offset of the signed inclusive clamp maximum. |
| `QUANT_COMMAND_RESERVED_OFFSET` | `0x00000014u` | Byte offset of the required-zero reserved command word. |
| `MATRIX_COMMAND_BYTES` | `0x00000020u` | Size and required alignment of a local matrix command block. |
| `MATRIX_COMMAND_A_OFFSET` | `0x00000000u` | Byte offset of local A base in a matrix command block. |
| `MATRIX_COMMAND_B_OFFSET` | `0x00000004u` | Byte offset of local B base in a matrix command block. |
| `MATRIX_COMMAND_C_OFFSET` | `0x00000008u` | Byte offset of local C store base in a matrix command block. |
| `MATRIX_COMMAND_A_STRIDE_OFFSET` | `0x0000000Cu` | Byte offset of A row stride in a matrix command block. |
| `MATRIX_COMMAND_B_STRIDE_OFFSET` | `0x00000010u` | Byte offset of B row stride in a matrix command block. |
| `MATRIX_COMMAND_C_STRIDE_OFFSET` | `0x00000014u` | Byte offset of C row stride in a matrix command block. |
| `MATRIX_COMMAND_SHAPE_OFFSET` | `0x00000018u` | Byte offset of packed M/N/K/flags in a matrix command block. |
| `MATRIX_COMMAND_RESERVED_OFFSET` | `0x0000001Cu` | Byte offset of the required-zero reserved command word. |
| `MATRIX_SHAPE_M_SHIFT` | `0x00000000u` | Active M shift in the packed matrix shape word. |
| `MATRIX_SHAPE_N_SHIFT` | `0x00000008u` | Active N shift in the packed matrix shape word. |
| `MATRIX_SHAPE_K_SHIFT` | `0x00000010u` | Active K shift in the packed matrix shape word. |
| `MATRIX_SHAPE_FLAGS_SHIFT` | `0x00000018u` | Command flags shift in the packed matrix shape word. |
| `MATRIX_DIMENSION_MASK` | `0x000000FFu` | Mask for each active matrix tile dimension. |
| `MATRIX_MAX_DIMENSION` | `0x00000010u` | Maximum active M, N, or K in the first matrix implementation. |
| `MATRIX_FLAG_CLEAR` | `0x00000001u` | Clear the selected accumulator before this matrix product. |
| `MATRIX_FLAG_ACCUMULATE` | `0x00000002u` | Accumulate this product into an already-valid selected accumulator. |
| `MATRIX_FLAG_STORE` | `0x00000004u` | Store the selected accumulator to local C after this product. |
| `MATRIX_FLAGS_VALID_MASK` | `0x00000007u` | Valid first-generation matrix command flag bits. |

## Implemented Instructions

| Instruction | Class | Opcode | Format | Fault | Coverage | Semantics | Description |
| ----------- | ----- | ------ | ------ | ----- | -------- | --------- | ----------- |
| `FRONTEND_CONTROL_MOVI` | `FRONTEND_CONTROL` | `0x00u` | `scalar_rd_simm12` | `ILLEGAL_INSTRUCTION` | `v2_frontend_control_movi` | Write the sign-extended imm12 value to scalar register rd; writes to hardwired-zero s0 are discarded and rs1/rs2 must be zero. | Materialize a small signed scalar constant without consuming program-local data memory. |
| `FRONTEND_CONTROL_ADD` | `FRONTEND_CONTROL` | `0x01u` | `scalar_rd_rs1_rs2` | `ILLEGAL_INSTRUCTION` | `v2_frontend_control_add` | Write scalar rs1 plus scalar rs2 modulo 2^32 to rd; imm12 must be zero and writes to s0 are discarded. | Type-independent scalar address and loop arithmetic. |
| `FRONTEND_CONTROL_ADDI` | `FRONTEND_CONTROL` | `0x02u` | `scalar_rd_rs1_simm12` | `ILLEGAL_INSTRUCTION` | `v2_frontend_control_addi` | Write scalar rs1 plus sign-extended imm12 modulo 2^32 to rd; rs2 must be zero and writes to s0 are discarded. | Scalar pointer increment and loop-counter update. |
| `FRONTEND_CONTROL_LOAD` | `FRONTEND_CONTROL` | `0x03u` | `scalar_rd_base_simm12` | `LOCAL_MEMORY_BOUNDS` | `v2_frontend_control_load` | Load one aligned little-endian 32-bit word from data scratchpad address scalar rs1 plus sign-extended imm12 into rd; rs2 must be zero and writes to s0 are discarded. | Frontend scalar access to program arguments, loop metadata, and command construction state. |
| `FRONTEND_CONTROL_STORE` | `FRONTEND_CONTROL` | `0x04u` | `scalar_rs2_base_simm12` | `LOCAL_MEMORY_BOUNDS` | `v2_frontend_control_store` | Store scalar rs2 as one aligned little-endian 32-bit word to data scratchpad address scalar rs1 plus sign-extended imm12; rd must be zero. | Frontend scalar write path for kernel metadata and scalar results. |
| `FRONTEND_CONTROL_BEQ` | `FRONTEND_CONTROL` | `0x05u` | `scalar_branch_rs1_rs2_simm12` | `ILLEGAL_INSTRUCTION` | `v2_frontend_control_beq` | If scalar rs1 equals scalar rs2, set PC to current PC plus sign-extended imm12 multiplied by four; otherwise advance by one instruction. rd must be zero and the taken target must identify a complete instruction in the current program image. | General scalar equality branch with instruction-scaled PC-relative displacement. |
| `FRONTEND_CONTROL_BNE` | `FRONTEND_CONTROL` | `0x06u` | `scalar_branch_rs1_rs2_simm12` | `ILLEGAL_INSTRUCTION` | `v2_frontend_control_bne` | If scalar rs1 differs from scalar rs2, set PC to current PC plus sign-extended imm12 multiplied by four; otherwise advance by one instruction. rd must be zero and the taken target must identify a complete instruction in the current program image. | General scalar inequality branch used for deterministic frontend loops. |
| `PREDICATE_PTRUE` | `PREDICATE` | `0x00u` | `predicate_rd` | `VECTOR_CONFIG_FAULT` | `v2_predicate_ptrue` | Set predicate register rd active for lanes [0, VL) and inactive for all other implementation lanes. | VLA all-active predicate generation using current vector length. |
| `PREDICATE_LOAD` | `PREDICATE` | `0x01u` | `predicate_mem_imm12` | `LOCAL_MEMORY_BOUNDS` | `v2_predicate_load` | Load one little-endian 32-bit bit-packed predicate word from aligned local scratchpad address imm into predicate register rd. | Initial scalable predicate materialization path; implementations with VL max above 32 will extend the memory format in a later ISA minor revision. |
| `VECTOR_CONFIG_SET` | `VECTOR_CONFIG` | `0x00u` | `vector_config_imm12` | `VECTOR_CONFIG_FAULT` | `v2_vector_config_set` | Set VL, element width, signedness, rounding, and saturation state from one explicit immediate configuration word. | Vector configuration is orthogonal to operation opcodes so data-width expansion does not consume opcode space. |
| `VECTOR_MEMORY_LOAD` | `VECTOR_MEMORY` | `0x00u` | `vector_mem_vd_pred_imm12` | `LOCAL_MEMORY_BOUNDS` | `v2_vector_load` | Load active lanes selected by predicate register rs1 from VL contiguous little-endian elements using configured SEW and signedness; inactive destination lanes are preserved. | Width-generic V2 vector scratchpad load instruction. |
| `VECTOR_MEMORY_STORE` | `VECTOR_MEMORY` | `0x01u` | `vector_mem_vs_pred_imm12` | `LOCAL_MEMORY_BOUNDS` | `v2_vector_store` | Store active lanes selected by predicate register rs1 using configured SEW and byte strobes; inactive lanes perform no memory write. | Width-generic V2 vector scratchpad store instruction. |
| `VECTOR_ALU_ADD` | `VECTOR_ALU` | `0x00u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_add` | Per-active-lane integer add selected by immediate predicate register bits [3:0], using configured SEW and signedness with wraparound unless saturation is enabled. | Width-generic V2 integer vector add instruction. |
| `VECTOR_ALU_SUB` | `VECTOR_ALU` | `0x01u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_sub` | Per-active-lane integer subtract using configured SEW and signedness with wraparound unless saturation is enabled. | Width-generic V2 integer vector subtract instruction. |
| `VECTOR_ALU_MIN` | `VECTOR_ALU` | `0x02u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_min` | Per-active-lane minimum using configured SEW and signedness. | Width-generic V2 integer vector minimum instruction. |
| `VECTOR_ALU_MAX` | `VECTOR_ALU` | `0x03u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_max` | Per-active-lane maximum using configured SEW and signedness. | Width-generic V2 integer vector maximum instruction. |
| `VECTOR_ALU_EQ` | `VECTOR_ALU` | `0x04u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_eq` | Per-active-lane equality compare using configured SEW, writing canonical element value one or zero. | Width-generic V2 integer vector equality compare instruction. |
| `VECTOR_ALU_LT` | `VECTOR_ALU` | `0x05u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_lt` | Per-active-lane less-than compare using configured SEW and signedness, writing canonical one or zero. | Width-generic V2 integer vector less-than compare instruction. |
| `VECTOR_ALU_SHL` | `VECTOR_ALU` | `0x06u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_shl` | Per-active-lane logical left shift; shift-count width follows configured SEW. | Width-generic V2 integer vector shift-left instruction. |
| `VECTOR_ALU_SRL` | `VECTOR_ALU` | `0x07u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_srl` | Per-active-lane logical right shift; shift-count width follows configured SEW. | Width-generic V2 integer vector logical shift-right instruction. |
| `VECTOR_ALU_SRA` | `VECTOR_ALU` | `0x08u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_sra` | Per-active-lane arithmetic right shift of the configured signed element width. | Width-generic V2 signed vector arithmetic shift-right instruction. |
| `VECTOR_ALU_SELECT` | `VECTOR_ALU` | `0x09u` | `vector_alu_vd_vs1_vs2` | `ILLEGAL_INSTRUCTION` | `v2_vector_alu_select` | For every lane below VL, select vs1 when the immediate predicate bit is active and vs2 otherwise; unlike governing-predicate ALU operations, every lane below VL is written. | SVE-style predicate select with no implicit merge dependency on the old destination. |
| `VECTOR_PERMUTE_GATHER` | `VECTOR_PERMUTE` | `0x00u` | `vector_alu_vd_vs1_vs2` | `VECTOR_CONFIG_FAULT` | `v2_vector_permute_gather` | For each active lane selected by the immediate predicate, read vs1 at the unsigned lane index held in vs2; every active index must be less than VL and inactive destination lanes are preserved. | General register-lane gather used to express shuffle, transpose, and tile-movement permutations. |
| `VECTOR_PERMUTE_ZIP_LO` | `VECTOR_PERMUTE` | `0x01u` | `vector_alu_vd_vs1_vs2` | `VECTOR_CONFIG_FAULT` | `v2_vector_permute_zip_lo` | For even VL, interleave the lower VL/2 lanes of vs1 and vs2 into vd under the immediate predicate; inactive destination lanes are preserved. | Lower-half lane packing primitive analogous to the first half of a scalable zip operation. |
| `VECTOR_PERMUTE_ZIP_HI` | `VECTOR_PERMUTE` | `0x02u` | `vector_alu_vd_vs1_vs2` | `VECTOR_CONFIG_FAULT` | `v2_vector_permute_zip_hi` | For even VL, interleave the upper VL/2 lanes of vs1 and vs2 into vd under the immediate predicate; inactive destination lanes are preserved. | Upper-half lane packing primitive paired with zip.lo. |
| `VECTOR_PERMUTE_UNZIP_EVEN` | `VECTOR_PERMUTE` | `0x03u` | `vector_alu_vd_vs1_vs2` | `VECTOR_CONFIG_FAULT` | `v2_vector_permute_unzip_even` | For even VL, concatenate the even lanes of vs1 followed by the even lanes of vs2 into vd under the immediate predicate; inactive destination lanes are preserved. | Lane unpacking primitive that reconstructs the first source from zip.lo and zip.hi results. |
| `VECTOR_PERMUTE_UNZIP_ODD` | `VECTOR_PERMUTE` | `0x04u` | `vector_alu_vd_vs1_vs2` | `VECTOR_CONFIG_FAULT` | `v2_vector_permute_unzip_odd` | For even VL, concatenate the odd lanes of vs1 followed by the odd lanes of vs2 into vd under the immediate predicate; inactive destination lanes are preserved. | Lane unpacking primitive that reconstructs the second source from zip.lo and zip.hi results. |
| `VECTOR_PERMUTE_TRANSPOSE4` | `VECTOR_PERMUTE` | `0x05u` | `vector_alu_vd_vs1_vs2` | `VECTOR_CONFIG_FAULT` | `v2_vector_permute_transpose4` | With VL exactly 16 and rs2 equal to zero, treat vs1 as one row-major 4x4 tile and write its transpose to vd under the immediate predicate; inactive destination lanes are preserved. | Explicit in-register 4x4 transpose helper for tensor tile movement. |
| `VECTOR_REDUCTION_SUM` | `VECTOR_REDUCTION` | `0x00u` | `vector_reduce_vd_vs_pred` | `VECTOR_CONFIG_FAULT` | `v2_vector_reduce_sum` | Reduce active source lanes selected by the immediate predicate with configured-width wraparound addition into destination lane zero; an empty active set produces zero. | Predicate-aware integer sum reduction. |
| `VECTOR_REDUCTION_MIN` | `VECTOR_REDUCTION` | `0x01u` | `vector_reduce_vd_vs_pred` | `VECTOR_CONFIG_FAULT` | `v2_vector_reduce_min` | Reduce active source lanes using configured signedness into destination lane zero; an empty active set produces the greatest representable configured element. | Predicate-aware integer minimum reduction. |
| `VECTOR_REDUCTION_MAX` | `VECTOR_REDUCTION` | `0x02u` | `vector_reduce_vd_vs_pred` | `VECTOR_CONFIG_FAULT` | `v2_vector_reduce_max` | Reduce active source lanes using configured signedness into destination lane zero; an empty active set produces the least representable configured element. | Predicate-aware integer maximum reduction. |
| `QUANTIZATION_REQUANTIZE` | `QUANTIZATION` | `0x00u` | `quant_vd_vs_pred_imm12` | `VECTOR_CONFIG_FAULT` | `v2_quantization_requantize` | For active lanes selected by predicate rs2, multiply signed or unsigned source rs1 by the command multiplier, right shift with round-to-nearest ties-to-even, add zero point, clamp inclusively, canonicalize to configured SEW/signedness, and preserve inactive destination lanes. The aligned local command block at imm contains multiplier, shift, zero point, clamp minimum, clamp maximum, and a required-zero word. | Per-tensor fixed-point requantization, clipping, saturation, and narrowing helper. |
| `MATRIX_GEMM` | `MATRIX` | `0x00u` | `matrix_accumulator_imm12` | `MATRIX_ISSUE_FAULT` | `v2_matrix_gemm_program` | Issue one active INT8 MxK by KxN tile product described by the aligned local command block at imm to accumulator rd; rs1 and rs2 must be zero. | Frontend-issued B-weight-stationary INT8 matrix tile micro-op. |
| `CSR_DEBUG_READ` | `CSR_DEBUG` | `0x00u` | `scalar_rd_csr_imm12` | `ILLEGAL_INSTRUCTION` | `v2_csr_debug_read` | Read the read-only frontend CSR selected by imm12 into scalar register rd; rs1 and rs2 must be zero, writes to s0 are discarded, and an unknown CSR selector faults precisely without retiring. | Architectural frontend state and retired-instruction snapshot read. |
| `SYSTEM_EXIT` | `SYSTEM` | `0x00u` | `system_imm` | `NONE` | `v2_frontend_system_exit` | Terminate the active Holon program with DONE. | Program terminal success instruction. |
| `SYSTEM_FAULT` | `SYSTEM` | `0x01u` | `system_imm` | `EXPLICIT_PROGRAM_FAULT` | `v2_frontend_system_fault` | Terminate the active Holon program with a software-provided fault code. | Program terminal explicit fault instruction. |
| `DMA_LOAD` | `DMA` | `0x00u` | `dma_regs_words12` | `DMA_REQUEST_FAULT` | `v2_frontend_dma_load_program` | Copy imm12+1 aligned 32-bit words from the 64-bit system address in {scalar[rs1], scalar[rd]} into local scratchpad at the 32-bit byte address in scalar[rs2]. | Register-addressed frontend-issued DMA system-to-scratchpad load instruction. |
| `DMA_STORE` | `DMA` | `0x01u` | `dma_regs_words12` | `DMA_REQUEST_FAULT` | `v2_frontend_dma_store_program` | Copy imm12+1 aligned 32-bit words from local scratchpad at the 32-bit byte address in scalar[rs2] into the 64-bit system address in {scalar[rs1], scalar[rd]}. | Register-addressed frontend-issued DMA scratchpad-to-system store instruction. |
| `SYNC_WAIT_DMA` | `SYNC` | `0x00u` | `sync_order_point` | `ILLEGAL_INSTRUCTION` | `v2_frontend_sync_wait_dma` | Wait until all prior frontend-issued DMA commands have completed and their effects are architecturally visible. | DMA completion ordering point. In the first in-order DMA implementation, DMA instructions already retire after completion, so this instruction retires as an explicit no-op order point. |
| `SYNC_FENCE_LOCAL` | `SYNC` | `0x01u` | `sync_order_point` | `ILLEGAL_INSTRUCTION` | `v2_frontend_sync_fence_local` | Order prior local memory writes before later local memory reads or engine issue. | Local scratchpad ordering point for future multi-client local-memory arbitration. |
| `SYNC_FENCE_DMA` | `SYNC` | `0x02u` | `sync_order_point` | `ILLEGAL_INSTRUCTION` | `v2_frontend_sync_fence_dma` | Order prior local memory writes before later DMA stores and order prior DMA loads before later consumers. | DMA/local-memory ordering point for frontend-visible transfer ordering. |

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
