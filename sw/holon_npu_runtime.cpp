#include "holon_npu_runtime.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace holon_npu::v2::runtime {
namespace {

constexpr std::uint32_t k_opcode_shift = HOLON_NPU_ISA_OPCODE_SHIFT;
constexpr std::uint32_t k_rd_shift = HOLON_NPU_ISA_RD_SHIFT;
constexpr std::uint32_t k_rs1_shift = HOLON_NPU_ISA_RS1_SHIFT;
constexpr std::uint32_t k_rs2_shift = HOLON_NPU_ISA_RS2_SHIFT;
constexpr std::uint32_t k_field_mask = HOLON_NPU_ISA_FIELD_MASK;
constexpr std::uint32_t k_imm_mask = HOLON_NPU_ISA_IMM_MASK;

std::uint32_t encode(
    std::uint32_t isa_class,
    opcode opcode,
    std::uint8_t rd,
    std::uint8_t rs1,
    std::uint8_t rs2,
    std::uint16_t imm
) {
    return isa_class | (static_cast<std::uint32_t>(opcode) << k_opcode_shift) |
           ((static_cast<std::uint32_t>(rd) & k_field_mask) << k_rd_shift) |
           ((static_cast<std::uint32_t>(rs1) & k_field_mask) << k_rs1_shift) |
           ((static_cast<std::uint32_t>(rs2) & k_field_mask) << k_rs2_shift) |
           (static_cast<std::uint32_t>(imm) & k_imm_mask);
}

program_image make_image(
    program_builder builder,
    std::uint64_t required_caps,
    std::uint64_t required_op_classes
) {
    return program_image{
        .words = builder.words(),
        .required_caps = required_caps,
        .required_op_classes = required_op_classes,
    };
}

struct byte_range {
    std::uint64_t begin;
    std::uint64_t end;
};

bool make_matrix_range(
    std::uint32_t base,
    std::uint32_t rows,
    std::uint32_t stride,
    std::uint64_t row_bytes,
    byte_range& range
) {
    if (rows == 0 || row_bytes == 0) {
        return false;
    }
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto row_count_minus_one = static_cast<std::uint64_t>(rows - 1U);
    if (row_bytes > maximum - base ||
        (stride != 0 && row_count_minus_one > (maximum - base - row_bytes) / stride)) {
        return false;
    }
    range = byte_range{
        .begin = base,
        .end = static_cast<std::uint64_t>(base) + row_count_minus_one * stride + row_bytes,
    };
    return true;
}

bool ranges_overlap(byte_range lhs, byte_range rhs) {
    return lhs.begin < rhs.end && rhs.begin < lhs.end;
}

}  // namespace

std::uint32_t encode_scalar_movi(std::uint8_t rd, std::int16_t immediate) {
    return encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL, opcode::frontend_control_movi,
                  rd, 0, 0, static_cast<std::uint16_t>(immediate));
}

std::uint32_t encode_scalar_add(std::uint8_t rd, std::uint8_t rs1, std::uint8_t rs2) {
    return encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL, opcode::frontend_control_add,
                  rd, rs1, rs2, 0);
}

std::uint32_t encode_scalar_addi(std::uint8_t rd, std::uint8_t rs1, std::int16_t immediate) {
    return encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL, opcode::frontend_control_addi,
                  rd, rs1, 0, static_cast<std::uint16_t>(immediate));
}

std::uint32_t encode_scalar_load(std::uint8_t rd, std::uint8_t base, std::int16_t byte_offset) {
    return encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL, opcode::frontend_control_load,
                  rd, base, 0, static_cast<std::uint16_t>(byte_offset));
}

std::uint32_t encode_scalar_store(std::uint8_t source, std::uint8_t base, std::int16_t byte_offset) {
    return encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL, opcode::frontend_control_store,
                  0, base, source, static_cast<std::uint16_t>(byte_offset));
}

std::uint32_t encode_scalar_beq(
    std::uint8_t rs1, std::uint8_t rs2, std::int16_t instruction_offset
) {
    return encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL, opcode::frontend_control_beq,
                  0, rs1, rs2, static_cast<std::uint16_t>(instruction_offset));
}

std::uint32_t encode_scalar_bne(
    std::uint8_t rs1, std::uint8_t rs2, std::int16_t instruction_offset
) {
    return encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL, opcode::frontend_control_bne,
                  0, rs1, rs2, static_cast<std::uint16_t>(instruction_offset));
}

std::uint32_t encode_vector_config(
    std::uint16_t vl,
    vector_element_width element_width,
    bool is_signed,
    vector_rounding rounding,
    bool saturate
) {
    const auto vl_minus_one = vl == 0
        ? HOLON_NPU_ISA_VTYPE_VL_MINUS_ONE_MASK
        : (static_cast<std::uint32_t>(vl) - 1U) & HOLON_NPU_ISA_VTYPE_VL_MINUS_ONE_MASK;
    const auto immediate = vl_minus_one |
        (static_cast<std::uint32_t>(element_width) << HOLON_NPU_ISA_VTYPE_SEW_SHIFT) |
        (is_signed ? HOLON_NPU_ISA_VTYPE_SIGNED : 0U) |
        (static_cast<std::uint32_t>(rounding) << HOLON_NPU_ISA_VTYPE_ROUND_SHIFT) |
        (saturate ? HOLON_NPU_ISA_VTYPE_SATURATE : 0U);
    return encode(
        HOLON_NPU_ISA_CLASS_VECTOR_CONFIG,
        opcode::vector_config_set,
        0,
        0,
        0,
        static_cast<std::uint16_t>(immediate)
    );
}

std::uint32_t encode_predicate_ptrue(std::uint8_t pd) {
    return encode(HOLON_NPU_ISA_CLASS_PREDICATE, opcode::predicate_ptrue, pd, 0, 0, 0);
}

std::uint32_t encode_predicate_load(std::uint8_t pd, std::uint16_t local_byte_offset) {
    return encode(
        HOLON_NPU_ISA_CLASS_PREDICATE,
        opcode::predicate_load,
        pd,
        0,
        0,
        local_byte_offset
    );
}

std::uint32_t encode_vector_load(
    std::uint8_t vd,
    std::uint16_t local_byte_offset,
    std::uint8_t predicate
) {
    return encode(
        HOLON_NPU_ISA_CLASS_VECTOR_MEMORY,
        opcode::vector_memory_load,
        vd,
        predicate,
        0,
        local_byte_offset
    );
}

std::uint32_t encode_vector_store(
    std::uint8_t vs,
    std::uint16_t local_byte_offset,
    std::uint8_t predicate
) {
    return encode(
        HOLON_NPU_ISA_CLASS_VECTOR_MEMORY,
        opcode::vector_memory_store,
        vs,
        predicate,
        0,
        local_byte_offset
    );
}

std::uint32_t encode_vector_add(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_add, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_sub(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_sub, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_min(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_min, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_max(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_max, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_eq(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_eq, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_lt(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_lt, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_shl(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_shl, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_srl(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_srl, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_sra(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_sra, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_select(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode::vector_alu_select, vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_gather(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t indices, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE, opcode::vector_permute_gather,
                  vd, vs, indices, predicate);
}

std::uint32_t encode_vector_zip_lo(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE, opcode::vector_permute_zip_lo,
                  vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_zip_hi(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE, opcode::vector_permute_zip_hi,
                  vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_unzip_even(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE, opcode::vector_permute_unzip_even,
                  vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_unzip_odd(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE, opcode::vector_permute_unzip_odd,
                  vd, vs1, vs2, predicate);
}

std::uint32_t encode_vector_transpose4(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE, opcode::vector_permute_transpose4,
                  vd, vs, 0, predicate);
}

std::uint32_t encode_vector_reduce_sum(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_REDUCTION, opcode::vector_reduction_sum,
                  vd, vs, 0, predicate);
}

std::uint32_t encode_vector_reduce_min(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_REDUCTION, opcode::vector_reduction_min,
                  vd, vs, 0, predicate);
}

std::uint32_t encode_vector_reduce_max(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_REDUCTION, opcode::vector_reduction_max,
                  vd, vs, 0, predicate);
}

std::uint32_t encode_quant_requantize(
    std::uint8_t vd,
    std::uint8_t vs,
    std::uint8_t predicate,
    std::uint16_t command_byte_offset
) {
    return encode(HOLON_NPU_ISA_CLASS_QUANTIZATION, opcode::quantization_requantize,
                  vd, vs, predicate, command_byte_offset);
}

std::uint32_t encode_matrix_gemm(
    std::uint8_t accumulator_id, std::uint16_t command_byte_offset
) {
    return encode(
        HOLON_NPU_ISA_CLASS_MATRIX,
        opcode::matrix_gemm,
        accumulator_id,
        0,
        0,
        command_byte_offset
    );
}

std::uint32_t encode_csr_read(std::uint8_t rd, csr selector) {
    return encode(
        HOLON_NPU_ISA_CLASS_CSR_DEBUG,
        opcode::csr_debug_read,
        rd,
        0,
        0,
        static_cast<std::uint16_t>(selector)
    );
}

std::uint32_t encode_dma_load(
    std::uint8_t system_addr_lo_reg,
    std::uint8_t system_addr_hi_reg,
    std::uint8_t local_addr_reg,
    std::uint16_t word_count
) {
    if (word_count == 0 || word_count > HOLON_NPU_ISA_DMA_MAX_WORDS) {
        throw std::invalid_argument{"DMA word count must be in [1, 4096]"};
    }
    return encode(
        HOLON_NPU_ISA_CLASS_DMA,
        opcode::dma_load,
        system_addr_lo_reg,
        system_addr_hi_reg,
        local_addr_reg,
        static_cast<std::uint16_t>(word_count - 1U)
    );
}

std::uint32_t encode_dma_store(
    std::uint8_t system_addr_lo_reg,
    std::uint8_t system_addr_hi_reg,
    std::uint8_t local_addr_reg,
    std::uint16_t word_count
) {
    if (word_count == 0 || word_count > HOLON_NPU_ISA_DMA_MAX_WORDS) {
        throw std::invalid_argument{"DMA word count must be in [1, 4096]"};
    }
    return encode(
        HOLON_NPU_ISA_CLASS_DMA,
        opcode::dma_store,
        system_addr_lo_reg,
        system_addr_hi_reg,
        local_addr_reg,
        static_cast<std::uint16_t>(word_count - 1U)
    );
}

std::uint32_t encode_sync_wait_dma() {
    return encode(HOLON_NPU_ISA_CLASS_SYNC, opcode::sync_wait_dma, 0, 0, 0, 0);
}

std::uint32_t encode_sync_fence_local() {
    return encode(HOLON_NPU_ISA_CLASS_SYNC, opcode::sync_fence_local, 0, 0, 0, 0);
}

std::uint32_t encode_sync_fence_dma() {
    return encode(HOLON_NPU_ISA_CLASS_SYNC, opcode::sync_fence_dma, 0, 0, 0, 0);
}

std::uint32_t encode_system_exit() {
    return encode(HOLON_NPU_ISA_CLASS_SYSTEM, opcode::system_exit, 0, 0, 0, 0);
}

std::uint32_t encode_system_fault(fault_code fault) {
    return encode(
        HOLON_NPU_ISA_CLASS_SYSTEM,
        opcode::system_fault,
        0,
        0,
        0,
        static_cast<std::uint16_t>(fault)
    );
}
program_builder& program_builder::raw(std::uint32_t word) {
    words_.push_back(word);
    return *this;
}

program_builder& program_builder::movi(std::uint8_t rd, std::int16_t immediate) {
    return raw(encode_scalar_movi(rd, immediate));
}

program_builder& program_builder::scalar_add(std::uint8_t rd, std::uint8_t rs1, std::uint8_t rs2) {
    return raw(encode_scalar_add(rd, rs1, rs2));
}

program_builder& program_builder::scalar_addi(
    std::uint8_t rd, std::uint8_t rs1, std::int16_t immediate
) {
    return raw(encode_scalar_addi(rd, rs1, immediate));
}

program_builder& program_builder::scalar_load(
    std::uint8_t rd, std::uint8_t base, std::int16_t byte_offset
) {
    return raw(encode_scalar_load(rd, base, byte_offset));
}

program_builder& program_builder::scalar_store(
    std::uint8_t source, std::uint8_t base, std::int16_t byte_offset
) {
    return raw(encode_scalar_store(source, base, byte_offset));
}

program_builder& program_builder::beq(
    std::uint8_t rs1, std::uint8_t rs2, std::int16_t instruction_offset
) {
    return raw(encode_scalar_beq(rs1, rs2, instruction_offset));
}

program_builder& program_builder::bne(
    std::uint8_t rs1, std::uint8_t rs2, std::int16_t instruction_offset
) {
    return raw(encode_scalar_bne(rs1, rs2, instruction_offset));
}

program_builder& program_builder::configure(
    std::uint16_t vl,
    vector_element_width element_width,
    bool is_signed,
    vector_rounding rounding,
    bool saturate
) {
    return raw(encode_vector_config(vl, element_width, is_signed, rounding, saturate));
}

program_builder& program_builder::predicate_ptrue(std::uint8_t pd) {
    return raw(encode_predicate_ptrue(pd));
}

program_builder& program_builder::predicate_load(std::uint8_t pd, std::uint16_t local_byte_offset) {
    return raw(encode_predicate_load(pd, local_byte_offset));
}

program_builder& program_builder::load(
    std::uint8_t vd, std::uint16_t local_byte_offset, std::uint8_t predicate
) {
    return raw(encode_vector_load(vd, local_byte_offset, predicate));
}

program_builder& program_builder::store(
    std::uint8_t vs, std::uint16_t local_byte_offset, std::uint8_t predicate
) {
    return raw(encode_vector_store(vs, local_byte_offset, predicate));
}

program_builder& program_builder::add(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_add(vd, vs1, vs2, predicate));
}

program_builder& program_builder::sub(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_sub(vd, vs1, vs2, predicate));
}

program_builder& program_builder::min(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_min(vd, vs1, vs2, predicate));
}

program_builder& program_builder::max(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_max(vd, vs1, vs2, predicate));
}

program_builder& program_builder::eq(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_eq(vd, vs1, vs2, predicate));
}

program_builder& program_builder::lt(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_lt(vd, vs1, vs2, predicate));
}

program_builder& program_builder::shl(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_shl(vd, vs1, vs2, predicate));
}

program_builder& program_builder::srl(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_srl(vd, vs1, vs2, predicate));
}

program_builder& program_builder::sra(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_sra(vd, vs1, vs2, predicate));
}

program_builder& program_builder::select(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_select(vd, vs1, vs2, predicate));
}

program_builder& program_builder::gather(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t indices, std::uint8_t predicate
) {
    return raw(encode_vector_gather(vd, vs, indices, predicate));
}

program_builder& program_builder::zip_lo(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_zip_lo(vd, vs1, vs2, predicate));
}

program_builder& program_builder::zip_hi(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_zip_hi(vd, vs1, vs2, predicate));
}

program_builder& program_builder::unzip_even(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_unzip_even(vd, vs1, vs2, predicate));
}

program_builder& program_builder::unzip_odd(
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t predicate
) {
    return raw(encode_vector_unzip_odd(vd, vs1, vs2, predicate));
}

program_builder& program_builder::transpose4(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate
) {
    return raw(encode_vector_transpose4(vd, vs, predicate));
}

program_builder& program_builder::reduce_sum(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate
) {
    return raw(encode_vector_reduce_sum(vd, vs, predicate));
}

program_builder& program_builder::reduce_min(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate
) {
    return raw(encode_vector_reduce_min(vd, vs, predicate));
}

program_builder& program_builder::reduce_max(
    std::uint8_t vd, std::uint8_t vs, std::uint8_t predicate
) {
    return raw(encode_vector_reduce_max(vd, vs, predicate));
}

program_builder& program_builder::requantize(
    std::uint8_t vd,
    std::uint8_t vs,
    std::uint8_t predicate,
    std::uint16_t command_byte_offset
) {
    return raw(encode_quant_requantize(vd, vs, predicate, command_byte_offset));
}

program_builder& program_builder::matrix_gemm(
    std::uint8_t accumulator_id, std::uint16_t command_byte_offset
) {
    return raw(encode_matrix_gemm(accumulator_id, command_byte_offset));
}

program_builder& program_builder::csr_read(std::uint8_t rd, csr selector) {
    return raw(encode_csr_read(rd, selector));
}

program_builder& program_builder::dma_load(
    std::uint8_t system_addr_lo_reg,
    std::uint8_t system_addr_hi_reg,
    std::uint8_t local_addr_reg,
    std::uint16_t word_count
) {
    return raw(encode_dma_load(
        system_addr_lo_reg,
        system_addr_hi_reg,
        local_addr_reg,
        word_count
    ));
}

program_builder& program_builder::dma_store(
    std::uint8_t system_addr_lo_reg,
    std::uint8_t system_addr_hi_reg,
    std::uint8_t local_addr_reg,
    std::uint16_t word_count
) {
    return raw(encode_dma_store(
        system_addr_lo_reg,
        system_addr_hi_reg,
        local_addr_reg,
        word_count
    ));
}

program_builder& program_builder::wait_dma() {
    return raw(encode_sync_wait_dma());
}

program_builder& program_builder::fence_local() {
    return raw(encode_sync_fence_local());
}

program_builder& program_builder::fence_dma() {
    return raw(encode_sync_fence_dma());
}

program_builder& program_builder::exit() {
    return raw(encode_system_exit());
}

program_builder& program_builder::fault(fault_code fault) {
    return raw(encode_system_fault(fault));
}

bool tiled_matrix_program::write_commands(std::span<std::byte> local_memory) const {
    const auto command_bytes = commands.size() * HOLON_NPU_ISA_MATRIX_COMMAND_BYTES;
    if (command_offset > local_memory.size() ||
        command_bytes > local_memory.size() - command_offset) {
        return false;
    }

    auto destination = local_memory.subspan(command_offset, command_bytes);
    for (std::size_t command_index = 0; command_index < commands.size(); ++command_index) {
        for (std::size_t word_index = 0; word_index < commands[command_index].size(); ++word_index) {
            const auto word = commands[command_index][word_index];
            const auto byte_offset = command_index * HOLON_NPU_ISA_MATRIX_COMMAND_BYTES +
                                     word_index * sizeof(std::uint32_t);
            for (std::size_t byte_index = 0; byte_index < sizeof(std::uint32_t); ++byte_index) {
                destination[byte_offset + byte_index] = std::byte{
                    static_cast<std::uint8_t>(word >> (byte_index * 8U))
                };
            }
        }
    }
    return true;
}



namespace examples {

program_image vector_add(
    std::uint16_t vl,
    std::uint16_t lhs_offset,
    std::uint16_t rhs_offset,
    std::uint16_t result_offset
) {
    program_builder builder;
    builder.configure(vl, vector_element_width::bits_32, true)
        .predicate_ptrue()
        .load(1, lhs_offset)
        .load(2, rhs_offset)
        .add(3, 1, 2)
        .store(3, result_offset)
        .exit();
    return make_image(
        std::move(builder),
        HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY | HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE,
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR | HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM
    );
}

program_image requant(
    std::uint16_t vl,
    std::uint16_t source_offset,
    std::uint16_t result_offset,
    std::uint16_t command_offset
) {
    program_builder builder;
    builder.configure(vl, vector_element_width::bits_32, true)
        .predicate_ptrue()
        .load(1, source_offset)
        .requantize(2, 1, 0, command_offset)
        .store(2, result_offset)
        .exit();
    return make_image(
        std::move(builder),
        HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY | HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE |
            HOLON_NPU_V2_CAP_QUANT_VECTOR,
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR | HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
            HOLON_NPU_PROGRAM_OP_CLASS_QUANTIZATION | HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM
    );
}

program_image relu(
    std::uint16_t vl,
    std::uint16_t source_offset,
    std::uint16_t zero_offset,
    std::uint16_t result_offset
) {
    program_builder builder;
    builder.configure(vl, vector_element_width::bits_32, true)
        .predicate_ptrue()
        .load(1, source_offset)
        .load(2, zero_offset)
        .max(3, 1, 2)
        .store(3, result_offset)
        .exit();
    return make_image(
        std::move(builder),
        HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY | HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE,
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR | HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM
    );
}

program_image reduce_sum(
    std::uint16_t vl,
    std::uint16_t source_offset,
    std::uint16_t result_offset
) {
    program_builder builder;
    builder.configure(vl, vector_element_width::bits_32, true)
        .predicate_ptrue()
        .load(1, source_offset)
        .reduce_sum(2, 1)
        .store(2, result_offset)
        .exit();
    return make_image(
        std::move(builder),
        HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY | HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE,
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR | HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM
    );
}

program_image transpose4(std::uint16_t source_offset, std::uint16_t result_offset) {
    program_builder builder;
    builder.configure(16, vector_element_width::bits_32, true)
        .predicate_ptrue()
        .load(1, source_offset)
        .transpose4(2, 1)
        .store(2, result_offset)
        .exit();
    return make_image(
        std::move(builder),
        HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY | HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE,
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR | HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM
    );
}

program_image int8_gemm(std::uint8_t accumulator_id, std::uint16_t command_offset) {
    program_builder builder;
    builder.matrix_gemm(accumulator_id, command_offset).exit();
    return make_image(
        std::move(builder),
        HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY | HOLON_NPU_V2_CAP_MATRIX_MICRO_OP,
        HOLON_NPU_PROGRAM_OP_CLASS_MATRIX | HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM
    );
}

std::expected<tiled_matrix_program, matrix_program_error> tiled_int8_gemm(
    const matrix_gemm_config& config
) {
    constexpr auto tile = static_cast<std::uint64_t>(HOLON_NPU_ISA_MATRIX_MAX_DIMENSION);
    constexpr auto command_bytes = static_cast<std::uint64_t>(HOLON_NPU_ISA_MATRIX_COMMAND_BYTES);
    constexpr auto immediate_space = static_cast<std::uint64_t>(HOLON_NPU_ISA_IMM_MASK) + 1U;

    if (config.m == 0 || config.n == 0 || config.k == 0) {
        return std::unexpected(matrix_program_error::invalid_dimension);
    }
    if (config.a_row_stride_bytes < config.k || config.b_row_stride_bytes < config.n ||
        static_cast<std::uint64_t>(config.c_row_stride_bytes) <
            static_cast<std::uint64_t>(config.n) * sizeof(std::int32_t)) {
        return std::unexpected(matrix_program_error::invalid_stride);
    }
    if ((config.c_offset % alignof(std::int32_t)) != 0 ||
        (config.c_row_stride_bytes % alignof(std::int32_t)) != 0 ||
        (config.command_offset % HOLON_NPU_ISA_MATRIX_COMMAND_BYTES) != 0) {
        return std::unexpected(matrix_program_error::invalid_alignment);
    }
    if (config.local_mem_bytes == 0 || config.local_mem_bytes > HOLON_NPU_LOCAL_MEM_MAX_BYTES) {
        return std::unexpected(matrix_program_error::local_memory_bounds);
    }

    const auto m_tiles = (static_cast<std::uint64_t>(config.m) + tile - 1U) / tile;
    const auto n_tiles = (static_cast<std::uint64_t>(config.n) + tile - 1U) / tile;
    const auto k_tiles = (static_cast<std::uint64_t>(config.k) + tile - 1U) / tile;
    if (config.command_offset >= immediate_space) {
        return std::unexpected(matrix_program_error::command_encoding_space);
    }
    const auto max_commands = (immediate_space - config.command_offset) / command_bytes;
    if (m_tiles > max_commands || n_tiles > max_commands / m_tiles ||
        k_tiles > max_commands / (m_tiles * n_tiles)) {
        return std::unexpected(matrix_program_error::command_encoding_space);
    }
    const auto command_count = m_tiles * n_tiles * k_tiles;
    const byte_range command_range{
        .begin = config.command_offset,
        .end = config.command_offset + command_count * command_bytes,
    };

    byte_range a_range{};
    byte_range b_range{};
    byte_range c_range{};
    if (!make_matrix_range(config.a_offset, config.m, config.a_row_stride_bytes, config.k, a_range) ||
        !make_matrix_range(config.b_offset, config.k, config.b_row_stride_bytes, config.n, b_range) ||
        !make_matrix_range(
            config.c_offset,
            config.m,
            config.c_row_stride_bytes,
            static_cast<std::uint64_t>(config.n) * sizeof(std::int32_t),
            c_range
        ) ||
        command_range.end > config.local_mem_bytes || a_range.end > config.local_mem_bytes ||
        b_range.end > config.local_mem_bytes || c_range.end > config.local_mem_bytes) {
        return std::unexpected(matrix_program_error::local_memory_bounds);
    }
    if (ranges_overlap(command_range, a_range) || ranges_overlap(command_range, b_range) ||
        ranges_overlap(command_range, c_range) || ranges_overlap(a_range, b_range) ||
        ranges_overlap(a_range, c_range) || ranges_overlap(b_range, c_range)) {
        return std::unexpected(matrix_program_error::overlapping_local_regions);
    }

    program_builder builder;
    std::vector<matrix_command_block> commands;
    commands.reserve(static_cast<std::size_t>(command_count));
    for (std::uint64_t m_base = 0; m_base < config.m; m_base += tile) {
        const auto active_m = std::min(tile, static_cast<std::uint64_t>(config.m) - m_base);
        for (std::uint64_t n_base = 0; n_base < config.n; n_base += tile) {
            const auto active_n = std::min(tile, static_cast<std::uint64_t>(config.n) - n_base);
            for (std::uint64_t k_base = 0; k_base < config.k; k_base += tile) {
                const auto active_k = std::min(tile, static_cast<std::uint64_t>(config.k) - k_base);
                const auto first_k_tile = k_base == 0;
                const auto last_k_tile = k_base + active_k == config.k;
                const auto flags = (first_k_tile ? HOLON_NPU_ISA_MATRIX_FLAG_CLEAR
                                                 : HOLON_NPU_ISA_MATRIX_FLAG_ACCUMULATE) |
                                   (last_k_tile ? HOLON_NPU_ISA_MATRIX_FLAG_STORE : 0U);
                const auto shape = static_cast<std::uint32_t>(active_m) |
                    (static_cast<std::uint32_t>(active_n) << HOLON_NPU_ISA_MATRIX_SHAPE_N_SHIFT) |
                    (static_cast<std::uint32_t>(active_k) << HOLON_NPU_ISA_MATRIX_SHAPE_K_SHIFT) |
                    (flags << HOLON_NPU_ISA_MATRIX_SHAPE_FLAGS_SHIFT);
                commands.push_back(matrix_command_block{
                    config.a_offset + static_cast<std::uint32_t>(m_base) * config.a_row_stride_bytes +
                        static_cast<std::uint32_t>(k_base),
                    config.b_offset + static_cast<std::uint32_t>(k_base) * config.b_row_stride_bytes +
                        static_cast<std::uint32_t>(n_base),
                    config.c_offset + static_cast<std::uint32_t>(m_base) * config.c_row_stride_bytes +
                        static_cast<std::uint32_t>(n_base) *
                            static_cast<std::uint32_t>(sizeof(std::int32_t)),
                    config.a_row_stride_bytes,
                    config.b_row_stride_bytes,
                    config.c_row_stride_bytes,
                    shape,
                    0,
                });
                const auto current_command_offset = config.command_offset +
                    (commands.size() - 1U) * HOLON_NPU_ISA_MATRIX_COMMAND_BYTES;
                builder.matrix_gemm(0, static_cast<std::uint16_t>(current_command_offset));
            }
        }
    }
    builder.fence_local().exit();

    return tiled_matrix_program{
        .image = make_image(
            std::move(builder),
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY | HOLON_NPU_V2_CAP_MATRIX_MICRO_OP,
            HOLON_NPU_PROGRAM_OP_CLASS_MATRIX | HOLON_NPU_PROGRAM_OP_CLASS_SYNC |
                HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM
        ),
        .command_offset = config.command_offset,
        .commands = std::move(commands),
    };
}

}  // namespace examples

}  // namespace holon_npu::v2::runtime
