#pragma once

#include "holon_npu_isa.h"
#include "holon_npu_program.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <expected>
#include <span>
#include <vector>

namespace holon_npu::runtime {

enum class fault_code : std::uint32_t {
    none = HOLON_NPU_FAULT_NONE,
    invalid_program_descriptor = HOLON_NPU_FAULT_INVALID_PROGRAM_DESCRIPTOR,
    unsupported_abi_or_isa = HOLON_NPU_FAULT_UNSUPPORTED_ABI_OR_ISA,
    unsupported_program_format = HOLON_NPU_FAULT_UNSUPPORTED_PROGRAM_FORMAT,
    unsupported_capability = HOLON_NPU_FAULT_UNSUPPORTED_CAPABILITY,
    unsupported_operation_class = HOLON_NPU_FAULT_UNSUPPORTED_OPERATION_CLASS,
    alignment = HOLON_NPU_FAULT_ALIGNMENT,
    local_memory_bounds = HOLON_NPU_FAULT_LOCAL_MEMORY_BOUNDS,
    illegal_instruction = HOLON_NPU_FAULT_ILLEGAL_INSTRUCTION,
    vector_config = HOLON_NPU_FAULT_VECTOR_CONFIG,
    matrix_issue = HOLON_NPU_FAULT_MATRIX_ISSUE,
    dma_request = HOLON_NPU_FAULT_DMA_REQUEST,
    axi_read = HOLON_NPU_FAULT_AXI_READ,
    axi_write = HOLON_NPU_FAULT_AXI_WRITE,
    explicit_program_fault = HOLON_NPU_FAULT_EXPLICIT_PROGRAM_FAULT,
};

enum class opcode : std::uint8_t {
    frontend_control_movi = HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_MOVI,
    frontend_control_add = HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_ADD,
    frontend_control_addi = HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_ADDI,
    frontend_control_load = HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_LOAD,
    frontend_control_store = HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_STORE,
    frontend_control_beq = HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_BEQ,
    frontend_control_bne = HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_BNE,
    predicate_ptrue = HOLON_NPU_ISA_OPCODE_PREDICATE_PTRUE,
    predicate_load = HOLON_NPU_ISA_OPCODE_PREDICATE_LOAD,
    vector_config_set = HOLON_NPU_ISA_OPCODE_VECTOR_CONFIG_SET,
    vector_memory_load = HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_LOAD,
    vector_memory_store = HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_STORE,
    vector_alu_add = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD,
    vector_alu_sub = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SUB,
    vector_alu_min = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_MIN,
    vector_alu_max = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_MAX,
    vector_alu_eq = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_EQ,
    vector_alu_lt = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_LT,
    vector_alu_shl = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SHL,
    vector_alu_srl = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SRL,
    vector_alu_sra = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SRA,
    vector_alu_select = HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SELECT,
    vector_permute_gather = HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_GATHER,
    vector_permute_zip_lo = HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_LO,
    vector_permute_zip_hi = HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_HI,
    vector_permute_unzip_even = HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_EVEN,
    vector_permute_unzip_odd = HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_ODD,
    vector_permute_transpose4 = HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_TRANSPOSE4,
    vector_reduction_sum = HOLON_NPU_ISA_OPCODE_VECTOR_REDUCTION_SUM,
    vector_reduction_min = HOLON_NPU_ISA_OPCODE_VECTOR_REDUCTION_MIN,
    vector_reduction_max = HOLON_NPU_ISA_OPCODE_VECTOR_REDUCTION_MAX,
    quantization_requantize = HOLON_NPU_ISA_OPCODE_QUANTIZATION_REQUANTIZE,
    matrix_gemm = HOLON_NPU_ISA_OPCODE_MATRIX_GEMM,
    csr_debug_read = HOLON_NPU_ISA_OPCODE_CSR_DEBUG_READ,
    system_exit = HOLON_NPU_ISA_OPCODE_SYSTEM_EXIT,
    system_fault = HOLON_NPU_ISA_OPCODE_SYSTEM_FAULT,
    dma_load = HOLON_NPU_ISA_OPCODE_DMA_LOAD,
    dma_store = HOLON_NPU_ISA_OPCODE_DMA_STORE,
    sync_wait_dma = HOLON_NPU_ISA_OPCODE_SYNC_WAIT_DMA,
    sync_fence_local = HOLON_NPU_ISA_OPCODE_SYNC_FENCE_LOCAL,
    sync_fence_dma = HOLON_NPU_ISA_OPCODE_SYNC_FENCE_DMA,
};

enum class vector_element_width : std::uint8_t {
    bits_8 = HOLON_NPU_ISA_VTYPE_SEW_8,
    bits_16 = HOLON_NPU_ISA_VTYPE_SEW_16,
    bits_32 = HOLON_NPU_ISA_VTYPE_SEW_32,
};

enum class vector_rounding : std::uint8_t {
    nearest_even = HOLON_NPU_ISA_VTYPE_ROUND_RNE,
};

enum class csr : std::uint16_t {
    pc = HOLON_NPU_ISA_CSR_PC,
    instret_lo = HOLON_NPU_ISA_CSR_INSTRET_LO,
    instret_hi = HOLON_NPU_ISA_CSR_INSTRET_HI,
    program_size_bytes = HOLON_NPU_ISA_CSR_PROGRAM_SIZE_BYTES,
    local_mem_bytes = HOLON_NPU_ISA_CSR_LOCAL_MEM_BYTES,
};
std::uint32_t encode_scalar_movi(std::uint8_t rd, std::int16_t immediate);
std::uint32_t encode_scalar_add(std::uint8_t rd, std::uint8_t rs1, std::uint8_t rs2);
std::uint32_t encode_scalar_addi(std::uint8_t rd, std::uint8_t rs1, std::int16_t immediate);
std::uint32_t encode_scalar_load(std::uint8_t rd, std::uint8_t base, std::int16_t byte_offset);
std::uint32_t encode_scalar_store(std::uint8_t source, std::uint8_t base, std::int16_t byte_offset);
std::uint32_t encode_scalar_beq(std::uint8_t rs1, std::uint8_t rs2, std::int16_t instruction_offset);
std::uint32_t encode_scalar_bne(std::uint8_t rs1, std::uint8_t rs2, std::int16_t instruction_offset);

std::uint32_t encode_vector_config(
    std::uint16_t vl,
    vector_element_width element_width,
    bool is_signed,
    vector_rounding rounding = vector_rounding::nearest_even,
    bool saturate = false
);
std::uint32_t encode_predicate_ptrue(std::uint8_t pd);
std::uint32_t encode_predicate_load(std::uint8_t pd, std::uint16_t local_byte_offset);
std::uint32_t encode_vector_load(
    std::uint8_t vd,
    std::uint16_t local_byte_offset,
    std::uint8_t predicate = 0
);
std::uint32_t encode_vector_store(
    std::uint8_t vs,
    std::uint16_t local_byte_offset,
    std::uint8_t predicate = 0
);
std::uint32_t encode_vector_add(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_sub(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_min(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_max(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_eq(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_lt(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_shl(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_srl(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_sra(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_select(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_gather(std::uint8_t vd, std::uint8_t vs, std::uint8_t indices, std::uint8_t predicate = 0);
std::uint32_t encode_vector_zip_lo(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_zip_hi(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_unzip_even(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_unzip_odd(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
std::uint32_t encode_vector_transpose4(std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate = 0);
std::uint32_t encode_vector_reduce_sum(std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate = 0);
std::uint32_t encode_vector_reduce_min(std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate = 0);
std::uint32_t encode_vector_reduce_max(std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate = 0);
std::uint32_t encode_quant_requantize(
    std::uint8_t vd,
    std::uint8_t vs,
    std::uint8_t predicate,
    std::uint16_t command_byte_offset
);
std::uint32_t encode_matrix_gemm(std::uint8_t accumulator_id, std::uint16_t command_byte_offset);
std::uint32_t encode_csr_read(std::uint8_t rd, csr selector);
std::uint32_t encode_dma_load(
    std::uint8_t system_addr_lo_reg,
    std::uint8_t system_addr_hi_reg,
    std::uint8_t local_addr_reg,
    std::uint16_t word_count
);
std::uint32_t encode_dma_store(
    std::uint8_t system_addr_lo_reg,
    std::uint8_t system_addr_hi_reg,
    std::uint8_t local_addr_reg,
    std::uint16_t word_count
);
std::uint32_t encode_sync_wait_dma();
std::uint32_t encode_sync_fence_local();
std::uint32_t encode_sync_fence_dma();
std::uint32_t encode_system_exit();
std::uint32_t encode_system_fault();
class program_builder {
public:
    program_builder& raw(std::uint32_t word);
    program_builder& movi(std::uint8_t rd, std::int16_t immediate);
    program_builder& scalar_add(std::uint8_t rd, std::uint8_t rs1, std::uint8_t rs2);
    program_builder& scalar_addi(std::uint8_t rd, std::uint8_t rs1, std::int16_t immediate);
    program_builder& scalar_load(std::uint8_t rd, std::uint8_t base, std::int16_t byte_offset);
    program_builder& scalar_store(std::uint8_t source, std::uint8_t base, std::int16_t byte_offset);
    program_builder& beq(std::uint8_t rs1, std::uint8_t rs2, std::int16_t instruction_offset);
    program_builder& bne(std::uint8_t rs1, std::uint8_t rs2, std::int16_t instruction_offset);
    program_builder& configure(
        std::uint16_t vl,
        vector_element_width element_width,
        bool is_signed,
        vector_rounding rounding = vector_rounding::nearest_even,
        bool saturate = false
    );
    program_builder& predicate_ptrue(std::uint8_t pd = 0);
    program_builder& predicate_load(std::uint8_t pd, std::uint16_t local_byte_offset);
    program_builder& load(std::uint8_t vd, std::uint16_t local_byte_offset, std::uint8_t predicate = 0);
    program_builder& store(std::uint8_t vs, std::uint16_t local_byte_offset, std::uint8_t predicate = 0);
    program_builder& add(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& sub(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& min(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& max(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& eq(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& lt(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& shl(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& srl(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& sra(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& select(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& gather(std::uint8_t vd, std::uint8_t vs, std::uint8_t indices, std::uint8_t predicate = 0);
    program_builder& zip_lo(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& zip_hi(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& unzip_even(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& unzip_odd(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate = 0);
    program_builder& transpose4(std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate = 0);
    program_builder& reduce_sum(std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate = 0);
    program_builder& reduce_min(std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate = 0);
    program_builder& reduce_max(std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate = 0);
    program_builder& requantize(
        std::uint8_t vd,
        std::uint8_t vs,
        std::uint8_t predicate,
        std::uint16_t command_byte_offset
    );
    program_builder& matrix_gemm(std::uint8_t accumulator_id, std::uint16_t command_byte_offset);
    program_builder& csr_read(std::uint8_t rd, csr selector);
    program_builder& dma_load(
        std::uint8_t system_addr_lo_reg,
        std::uint8_t system_addr_hi_reg,
        std::uint8_t local_addr_reg,
        std::uint16_t word_count
    );
    program_builder& dma_store(
        std::uint8_t system_addr_lo_reg,
        std::uint8_t system_addr_hi_reg,
        std::uint8_t local_addr_reg,
        std::uint16_t word_count
    );
    program_builder& wait_dma();
    program_builder& fence_local();
    program_builder& fence_dma();
    program_builder& exit();
    program_builder& fault();

    [[nodiscard]] const std::vector<std::uint32_t>& words() const { return words_; }
    [[nodiscard]] std::span<const std::uint32_t> span() const { return words_; }
    [[nodiscard]] std::size_t size() const { return words_.size(); }

private:
    std::vector<std::uint32_t> words_;
};

struct program_image {
    std::vector<std::uint32_t> words;
    std::uint64_t required_caps;
    std::uint64_t required_op_classes;

    [[nodiscard]] std::span<const std::uint32_t> span() const { return words; }
};

enum class matrix_program_error : std::uint8_t {
    invalid_dimension,
    invalid_stride,
    invalid_alignment,
    local_memory_bounds,
    command_encoding_space,
    overlapping_local_regions,
};

struct matrix_gemm_config {
    std::uint32_t m = 0;
    std::uint32_t n = 0;
    std::uint32_t k = 0;
    std::uint32_t a_offset = 0;
    std::uint32_t b_offset = 0;
    std::uint32_t c_offset = 0;
    std::uint32_t a_row_stride_bytes = 0;
    std::uint32_t b_row_stride_bytes = 0;
    std::uint32_t c_row_stride_bytes = 0;
    std::uint32_t local_mem_bytes = 0;
    std::uint16_t command_offset = 0;
};

using matrix_command_block = std::array<std::uint32_t, HOLON_NPU_ISA_MATRIX_COMMAND_BYTES / 4U>;

struct tiled_matrix_program {
    program_image image;
    std::uint16_t command_offset = 0;
    std::vector<matrix_command_block> commands;

    [[nodiscard]] bool write_commands(std::span<std::byte> local_memory) const;
};

namespace examples {

program_image vector_add(
    std::uint16_t vl,
    std::uint16_t lhs_offset,
    std::uint16_t rhs_offset,
    std::uint16_t result_offset
);
program_image requant(
    std::uint16_t vl,
    std::uint16_t source_offset,
    std::uint16_t result_offset,
    std::uint16_t command_offset
);
program_image relu(
    std::uint16_t vl,
    std::uint16_t source_offset,
    std::uint16_t zero_offset,
    std::uint16_t result_offset
);
program_image reduce_sum(
    std::uint16_t vl,
    std::uint16_t source_offset,
    std::uint16_t result_offset
);
program_image transpose4(std::uint16_t source_offset, std::uint16_t result_offset);
program_image int8_gemm(std::uint8_t accumulator_id, std::uint16_t command_offset);
std::expected<tiled_matrix_program, matrix_program_error> tiled_int8_gemm(
    const matrix_gemm_config& config
);

}  // namespace examples

}  // namespace holon_npu::runtime
