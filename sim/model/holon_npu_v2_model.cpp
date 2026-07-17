#include "holon_npu_v2_model.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <limits>

namespace holon_npu::v2::model {
namespace {

constexpr std::uint32_t k_opcode_shift = HOLON_NPU_ISA_OPCODE_SHIFT;
constexpr std::uint32_t k_rd_shift = HOLON_NPU_ISA_RD_SHIFT;
constexpr std::uint32_t k_rs1_shift = HOLON_NPU_ISA_RS1_SHIFT;
constexpr std::uint32_t k_rs2_shift = HOLON_NPU_ISA_RS2_SHIFT;
constexpr std::uint32_t k_field_mask = HOLON_NPU_ISA_FIELD_MASK;
constexpr std::uint32_t k_imm_mask = HOLON_NPU_ISA_IMM_MASK;
constexpr std::uint32_t k_pc_increment = HOLON_NPU_ISA_INSTRUCTION_BYTES;

std::int32_t wrap_add(std::int32_t lhs, std::int32_t rhs) {
    const auto lhs_bits = static_cast<std::uint32_t>(lhs);
    const auto rhs_bits = static_cast<std::uint32_t>(rhs);
    return std::bit_cast<std::int32_t>(lhs_bits + rhs_bits);
}

std::int32_t wrap_sub(std::int32_t lhs, std::int32_t rhs) {
    const auto lhs_bits = static_cast<std::uint32_t>(lhs);
    const auto rhs_bits = static_cast<std::uint32_t>(rhs);
    return std::bit_cast<std::int32_t>(lhs_bits - rhs_bits);
}

std::int32_t saturating_add_sub(
    std::int32_t lhs,
    std::int32_t rhs,
    std::uint32_t bits,
    bool is_signed,
    bool subtract
) {
    if (is_signed) {
        const auto minimum = -(std::int64_t{1} << (bits - 1U));
        const auto maximum = (std::int64_t{1} << (bits - 1U)) - 1;
        const auto result = static_cast<std::int64_t>(lhs) +
            (subtract ? -static_cast<std::int64_t>(rhs) : static_cast<std::int64_t>(rhs));
        return static_cast<std::int32_t>(std::clamp(result, minimum, maximum));
    }

    const auto maximum = bits == 32U
        ? std::uint64_t{0xFFFF'FFFFU}
        : (std::uint64_t{1} << bits) - 1U;
    const auto lhs_value = static_cast<std::uint32_t>(lhs) & maximum;
    const auto rhs_value = static_cast<std::uint32_t>(rhs) & maximum;
    const auto result = subtract
        ? (lhs_value < rhs_value ? std::uint64_t{0} : lhs_value - rhs_value)
        : std::min(lhs_value + rhs_value, maximum);
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(result));
}

std::int32_t arithmetic_shift_right(std::int32_t value, std::uint32_t count) {
    if (count == 0) {
        return value;
    }
    const auto bits = static_cast<std::uint32_t>(value);
    if (value >= 0) {
        return std::bit_cast<std::int32_t>(bits >> count);
    }
    const auto sign_fill = ~std::uint32_t{0} << (32U - count);
    return std::bit_cast<std::int32_t>((bits >> count) | sign_fill);
}

std::int64_t round_shift_nearest_even(std::int64_t value, std::uint32_t shift) {
    if (shift == 0) {
        return value;
    }
    const auto negative = value < 0;
    const auto magnitude = negative
        ? static_cast<std::uint64_t>(-(value + 1)) + 1U
        : static_cast<std::uint64_t>(value);
    auto quotient = magnitude >> shift;
    const auto remainder_mask = (std::uint64_t{1} << shift) - 1U;
    const auto remainder = magnitude & remainder_mask;
    const auto halfway = std::uint64_t{1} << (shift - 1U);
    if (remainder > halfway || (remainder == halfway && (quotient & 1U) != 0U)) {
        ++quotient;
    }
    return negative ? -static_cast<std::int64_t>(quotient)
                    : static_cast<std::int64_t>(quotient);
}

std::int32_t sign_extend_imm12(std::uint16_t immediate) {
    const auto bits = static_cast<std::uint32_t>(immediate) & HOLON_NPU_ISA_IMM_MASK;
    return (bits & 0x800U) != 0U
        ? std::bit_cast<std::int32_t>(bits | 0xFFFF'F000U)
        : static_cast<std::int32_t>(bits);
}

bool class_matches(std::uint32_t word, std::uint32_t value, std::uint32_t mask) {
    return (word & mask) == value;
}

bool aligned(std::uint64_t value, std::uint64_t alignment) {
    return alignment != 0 && value % alignment == 0;
}

bool range_fits_u64(std::uint64_t start, std::uint64_t byte_count) {
    return byte_count <= std::numeric_limits<std::uint64_t>::max() - start;
}

bool descriptor_reserved_zero(const holon_npu_program_desc_t& desc) {
    return desc.reserved_4c == 0 && desc.reserved_50 == 0 && desc.reserved_58 == 0 &&
           desc.reserved_60 == 0 && desc.reserved_68 == 0 && desc.reserved_70 == 0 &&
           desc.reserved_78 == 0;
}

}  // namespace

decoded_instruction decode(std::uint32_t word) {
    decoded_instruction inst{
        .word = word,
        .isa_class = HOLON_NPU_ISA_ENUM_RESERVED_F,
        .opcode = static_cast<std::uint8_t>((word >> k_opcode_shift) & k_field_mask),
        .rd = static_cast<std::uint8_t>((word >> k_rd_shift) & k_field_mask),
        .rs1 = static_cast<std::uint8_t>((word >> k_rs1_shift) & k_field_mask),
        .rs2 = static_cast<std::uint8_t>((word >> k_rs2_shift) & k_field_mask),
        .imm = static_cast<std::uint16_t>(word & k_imm_mask),
    };

    if (class_matches(word, HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL, HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_FRONTEND_CONTROL;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_PREDICATE, HOLON_NPU_ISA_CLASS_PREDICATE_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_PREDICATE;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_VECTOR_CONFIG, HOLON_NPU_ISA_CLASS_VECTOR_CONFIG_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_VECTOR_CONFIG;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_VECTOR_ALU, HOLON_NPU_ISA_CLASS_VECTOR_ALU_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_VECTOR_ALU;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_VECTOR_MEMORY, HOLON_NPU_ISA_CLASS_VECTOR_MEMORY_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_VECTOR_MEMORY;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE, HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_VECTOR_REDUCTION, HOLON_NPU_ISA_CLASS_VECTOR_REDUCTION_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_VECTOR_REDUCTION;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_QUANTIZATION, HOLON_NPU_ISA_CLASS_QUANTIZATION_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_QUANTIZATION;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_MATRIX, HOLON_NPU_ISA_CLASS_MATRIX_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_MATRIX;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_DMA, HOLON_NPU_ISA_CLASS_DMA_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_DMA;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_CSR_DEBUG, HOLON_NPU_ISA_CLASS_CSR_DEBUG_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_CSR_DEBUG;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_SYNC, HOLON_NPU_ISA_CLASS_SYNC_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_SYNC;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_SYSTEM, HOLON_NPU_ISA_CLASS_SYSTEM_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_SYSTEM;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_RESERVED_D, HOLON_NPU_ISA_CLASS_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_RESERVED_D;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_RESERVED_E, HOLON_NPU_ISA_CLASS_MASK)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_RESERVED_E;
    }
    return inst;
}

std::string class_name(holon_npu_isa_class_t isa_class) {
    switch (isa_class) {
        case HOLON_NPU_ISA_ENUM_FRONTEND_CONTROL:
            return "frontend_control";
        case HOLON_NPU_ISA_ENUM_PREDICATE:
            return "predicate";
        case HOLON_NPU_ISA_ENUM_VECTOR_CONFIG:
            return "vector_config";
        case HOLON_NPU_ISA_ENUM_VECTOR_ALU:
            return "vector_alu";
        case HOLON_NPU_ISA_ENUM_VECTOR_MEMORY:
            return "vector_memory";
        case HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE:
            return "vector_permute";
        case HOLON_NPU_ISA_ENUM_VECTOR_REDUCTION:
            return "vector_reduction";
        case HOLON_NPU_ISA_ENUM_QUANTIZATION:
            return "quantization";
        case HOLON_NPU_ISA_ENUM_MATRIX:
            return "matrix";
        case HOLON_NPU_ISA_ENUM_DMA:
            return "dma";
        case HOLON_NPU_ISA_ENUM_CSR_DEBUG:
            return "csr_debug";
        case HOLON_NPU_ISA_ENUM_SYNC:
            return "sync";
        case HOLON_NPU_ISA_ENUM_SYSTEM:
            return "system";
        case HOLON_NPU_ISA_ENUM_RESERVED_D:
            return "reserved_d";
        case HOLON_NPU_ISA_ENUM_RESERVED_E:
            return "reserved_e";
        case HOLON_NPU_ISA_ENUM_RESERVED_F:
            return "reserved_f";
    }
    return "unknown";
}

std::string disassemble(const decoded_instruction& inst) {
    const auto cls = class_name(inst.isa_class);
    const auto signed_imm = sign_extend_imm12(inst.imm);
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_FRONTEND_CONTROL) {
        const auto opcode = static_cast<v2_opcode>(inst.opcode);
        if (opcode == v2_opcode::frontend_control_movi) {
            return std::format("{}.movi s{}, {}", cls, inst.rd, signed_imm);
        }
        if (opcode == v2_opcode::frontend_control_add) {
            return std::format("{}.add s{}, s{}, s{}", cls, inst.rd, inst.rs1, inst.rs2);
        }
        if (opcode == v2_opcode::frontend_control_addi) {
            return std::format("{}.addi s{}, s{}, {}", cls, inst.rd, inst.rs1, signed_imm);
        }
        if (opcode == v2_opcode::frontend_control_load) {
            return std::format("{}.load s{}, [s{}{:+}]", cls, inst.rd, inst.rs1, signed_imm);
        }
        if (opcode == v2_opcode::frontend_control_store) {
            return std::format("{}.store s{}, [s{}{:+}]", cls, inst.rs2, inst.rs1, signed_imm);
        }
        if (opcode == v2_opcode::frontend_control_beq) {
            return std::format("{}.beq s{}, s{}, {:+}", cls, inst.rs1, inst.rs2, signed_imm);
        }
        if (opcode == v2_opcode::frontend_control_bne) {
            return std::format("{}.bne s{}, s{}, {:+}", cls, inst.rs1, inst.rs2, signed_imm);
        }
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_PREDICATE &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::predicate_ptrue)) {
        return std::format("{}.ptrue p{}", cls, inst.rd);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_PREDICATE &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::predicate_load)) {
        return std::format("{}.load p{}, [0x{:03X}]", cls, inst.rd, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_CONFIG &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_config_set)) {
        const auto vl = (inst.imm & HOLON_NPU_ISA_VTYPE_VL_MINUS_ONE_MASK) + 1U;
        const auto sew = 8U << ((inst.imm & HOLON_NPU_ISA_VTYPE_SEW_MASK) >>
                               HOLON_NPU_ISA_VTYPE_SEW_SHIFT);
        return std::format(
            "{}.set vl={}, sew={}, {}",
            cls,
            vl,
            sew,
            (inst.imm & HOLON_NPU_ISA_VTYPE_SIGNED) != 0 ? "signed" : "unsigned"
        );
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_MEMORY &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_memory_load)) {
        return std::format("{}.load v{}, p{}, [0x{:03X}]", cls, inst.rd, inst.rs1, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_MEMORY &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_memory_store)) {
        return std::format("{}.store v{}, p{}, [0x{:03X}]", cls, inst.rd, inst.rs1, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_add)) {
        return std::format("{}.add v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_sub)) {
        return std::format("{}.sub v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_min)) {
        return std::format("{}.min v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_max)) {
        return std::format("{}.max v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_eq)) {
        return std::format("{}.eq v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_lt)) {
        return std::format("{}.lt v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_shl)) {
        return std::format("{}.shl v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_srl)) {
        return std::format("{}.srl v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_sra)) {
        return std::format("{}.sra v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_select)) {
        return std::format("{}.select v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_permute_gather)) {
        return std::format("{}.gather v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE) {
        const auto operation = inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_permute_zip_lo)
            ? "zip.lo" : inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_permute_zip_hi)
            ? "zip.hi" : inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_permute_unzip_even)
            ? "unzip.even" : inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_permute_unzip_odd)
            ? "unzip.odd" : inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_permute_transpose4)
            ? "transpose4" : "unknown";
        return std::format("{}.{} v{}, v{}, v{}, p{}", cls, operation,
                           inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_REDUCTION) {
        const auto operation = inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_reduction_sum)
            ? "sum" : inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_reduction_min)
            ? "min" : inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_reduction_max)
            ? "max" : "unknown";
        return std::format("{}.{} v{}, v{}, p{}", cls, operation, inst.rd, inst.rs1, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_QUANTIZATION &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::quantization_requantize)) {
        return std::format("{}.requantize v{}, v{}, p{}, [0x{:03X}]",
                           cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_MATRIX &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::matrix_gemm)) {
        return std::format("matrix.gemm a{}, [0x{:03X}]", inst.rd, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_CSR_DEBUG &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::csr_debug_read)) {
        return std::format("csr_debug.read s{}, 0x{:03X}", inst.rd, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYSTEM &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::system_exit)) {
        return "system.exit";
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYSTEM &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::system_fault)) {
        return std::format("system.fault {}", inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_DMA &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::dma_load)) {
        return std::format(
            "dma.load sys={{s{},s{}}}, spm=s{}, words={}",
            inst.rs1,
            inst.rd,
            inst.rs2,
            inst.imm + 1U
        );
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_DMA &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::dma_store)) {
        return std::format(
            "dma.store sys={{s{},s{}}}, spm=s{}, words={}",
            inst.rs1,
            inst.rd,
            inst.rs2,
            inst.imm + 1U
        );
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYNC &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::sync_wait_dma)) {
        return "sync.wait_dma";
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYNC &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::sync_fence_local)) {
        return "sync.fence.local";
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYNC &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::sync_fence_dma)) {
        return "sync.fence.dma";
    }
    return std::format("{}.unknown opcode=0x{:X} word=0x{:08X}", cls, inst.opcode, inst.word);
}

machine::machine(std::size_t scratchpad_bytes, std::size_t max_vl)
    : scratchpad_(scratchpad_bytes),
      active_local_mem_bytes_(scratchpad_bytes),
      predicate_active_(max_vl, 1),
      max_vl_(max_vl) {
    for (auto& reg : vector_registers_) {
        reg.assign(max_vl_, 0);
    }
}

void machine::reset() {
    std::ranges::fill(scratchpad_, std::byte{0});
    active_local_mem_bytes_ = scratchpad_.size();
    for (auto& reg : vector_registers_) {
        std::ranges::fill(reg, 0);
    }
    std::ranges::fill(scalar_registers_, 0);
    std::ranges::fill(predicate_active_, 1);
    matrix_accumulator_ = {};
    matrix_accumulator_valid_ = false;
    matrix_accumulator_m_ = 0;
    matrix_accumulator_n_ = 0;
    state_ = lifecycle_state::idle;
    fault_ = model_error::none;
    pc_ = 0;
    vl_ = 0;
    element_width_ = vector_element_width::bits_32;
    rounding_ = vector_rounding::nearest_even;
    elements_signed_ = true;
    saturate_ = false;
    retired_ = 0;
    dma_events_.clear();
    matrix_events_.clear();
    matrix_accumulator_ = {};
    matrix_accumulator_valid_ = false;
    matrix_accumulator_m_ = 0;
    matrix_accumulator_n_ = 0;
    next_dma_sequence_ = 0;
    next_matrix_sequence_ = 0;
}

void machine::resize_system_memory(std::size_t byte_count) {
    system_memory_.assign(byte_count, std::byte{0});
}

void machine::load_program(std::span<const std::uint32_t> words) {
    program_.assign(words.begin(), words.end());
    state_ = lifecycle_state::idle;
    fault_ = model_error::none;
    pc_ = 0;
    retired_ = 0;
    std::ranges::fill(scalar_registers_, 0);
    dma_events_.clear();
    matrix_events_.clear();
    matrix_accumulator_ = {};
    matrix_accumulator_valid_ = false;
    matrix_accumulator_m_ = 0;
    matrix_accumulator_n_ = 0;
    next_dma_sequence_ = 0;
    next_matrix_sequence_ = 0;
    active_local_mem_bytes_ = scratchpad_.size();
}

run_result machine::load_program_descriptor(
    const holon_npu_program_desc_t& desc,
    std::span<const std::uint32_t> program_words,
    std::span<const std::byte> argument_bytes,
    const loader_config& config
) {
    state_ = lifecycle_state::idle;
    fault_ = model_error::none;
    pc_ = 0;
    retired_ = 0;
    program_.clear();
    dma_events_.clear();
    matrix_events_.clear();
    next_dma_sequence_ = 0;
    next_matrix_sequence_ = 0;

    if (desc.size_bytes != HOLON_NPU_PROGRAM_DESC_SIZE || !descriptor_reserved_zero(desc)) {
        raise_fault(model_error::invalid_program_descriptor);
        return run_result{state_, fault_, pc_, retired_};
    }
    if (desc.version != HOLON_NPU_V2_ABI_MAJOR || desc.holon_isa_major != config.isa_major ||
        desc.holon_isa_minor > config.isa_minor) {
        raise_fault(model_error::unsupported_abi_or_isa);
        return run_result{state_, fault_, pc_, retired_};
    }
    if (desc.program_format != HOLON_NPU_PROGRAM_FORMAT_HOLON_V2) {
        raise_fault(model_error::unsupported_program_format);
        return run_result{state_, fault_, pc_, retired_};
    }
    if ((desc.flags & ~HOLON_NPU_PROGRAM_FLAG_VALID_MASK) != 0) {
        raise_fault(model_error::invalid_program_descriptor);
        return run_result{state_, fault_, pc_, retired_};
    }
    if ((desc.required_caps & ~config.implemented_caps) != 0) {
        raise_fault(model_error::unsupported_capability);
        return run_result{state_, fault_, pc_, retired_};
    }
    if ((desc.required_op_classes & ~config.implemented_op_classes) != 0) {
        raise_fault(model_error::unsupported_operation_class);
        return run_result{state_, fault_, pc_, retired_};
    }
    if (!range_fits_u64(desc.code_addr, desc.code_size_bytes) ||
        !range_fits_u64(desc.arg_addr, desc.arg_size_bytes) ||
        (desc.completion_addr != 0 &&
         !range_fits_u64(desc.completion_addr, HOLON_NPU_COMPLETION_RECORD_SIZE))) {
        raise_fault(model_error::invalid_program_descriptor);
        return run_result{state_, fault_, pc_, retired_};
    }
    if (!aligned(desc.code_addr, HOLON_NPU_PROGRAM_IMAGE_ALIGN) ||
        !aligned(desc.code_size_bytes, HOLON_NPU_PROGRAM_IMAGE_ALIGN) ||
        !aligned(desc.entry_pc, HOLON_NPU_ISA_INSTRUCTION_BYTES) ||
        !aligned(desc.arg_addr, HOLON_NPU_PROGRAM_ARGUMENT_ALIGN) ||
        !aligned(desc.arg_size_bytes, HOLON_NPU_PROGRAM_ARGUMENT_ALIGN) ||
        (desc.completion_addr != 0 &&
         !aligned(desc.completion_addr, HOLON_NPU_PROGRAM_COMPLETION_ALIGN))) {
        raise_fault(model_error::alignment);
        return run_result{state_, fault_, pc_, retired_};
    }
    if (desc.code_size_bytes == 0 ||
        desc.code_size_bytes != program_words.size_bytes() ||
        desc.arg_size_bytes != argument_bytes.size() ||
        desc.program_mem_bytes < desc.code_size_bytes ||
        desc.local_mem_bytes < desc.arg_size_bytes ||
        desc.program_mem_bytes > HOLON_NPU_PROGRAM_MEM_MAX_BYTES ||
        desc.local_mem_bytes > scratchpad_.size() ||
        desc.stack_bytes > HOLON_NPU_PROGRAM_STACK_MAX_BYTES ||
        static_cast<std::uint64_t>(desc.arg_size_bytes) + desc.stack_bytes >
            desc.local_mem_bytes ||
        desc.entry_pc >= desc.code_size_bytes) {
        raise_fault(model_error::local_memory_bounds);
        return run_result{state_, fault_, pc_, retired_};
    }

    load_program(program_words);
    active_local_mem_bytes_ = desc.local_mem_bytes;
    pc_ = desc.entry_pc;
    if (!load_arguments(argument_bytes, 0)) {
        raise_fault(model_error::local_memory_bounds);
        return run_result{state_, fault_, pc_, retired_};
    }
    return run_result{state_, fault_, pc_, retired_};
}

bool machine::load_arguments(std::span<const std::byte> bytes, std::uint32_t local_byte_offset) {
    if (!local_range_ok(local_byte_offset, bytes.size())) {
        return false;
    }
    std::ranges::copy(bytes, scratchpad_.begin() + local_byte_offset);
    return true;
}

bool machine::write_i8(std::uint32_t local_byte_offset, std::span<const std::int8_t> values) {
    if (!local_range_ok(local_byte_offset, values.size())) {
        return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        store_i8(local_byte_offset + static_cast<std::uint32_t>(index), values[index]);
    }
    return true;
}

std::vector<std::int8_t> machine::read_i8(std::uint32_t local_byte_offset, std::size_t count) const {
    std::vector<std::int8_t> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = local_byte_offset + static_cast<std::uint32_t>(index);
        values.push_back(local_range_ok(offset, sizeof(std::int8_t)) ? load_i8(offset) : 0);
    }
    return values;
}

bool machine::write_i16(std::uint32_t local_byte_offset, std::span<const std::int16_t> values) {
    const auto byte_count = values.size_bytes();
    if (!local_range_ok(local_byte_offset, byte_count)) {
        return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        store_i16(local_byte_offset + static_cast<std::uint32_t>(index * sizeof(std::int16_t)), values[index]);
    }
    return true;
}

std::vector<std::int16_t> machine::read_i16(std::uint32_t local_byte_offset, std::size_t count) const {
    std::vector<std::int16_t> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = local_byte_offset + static_cast<std::uint32_t>(index * sizeof(std::int16_t));
        values.push_back(local_range_ok(offset, sizeof(std::int16_t)) ? load_i16(offset) : 0);
    }
    return values;
}

bool machine::write_i32(std::uint32_t local_byte_offset, std::span<const std::int32_t> values) {
    const auto byte_count = values.size_bytes();
    if (!local_range_ok(local_byte_offset, byte_count)) {
        return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        store_i32(local_byte_offset + static_cast<std::uint32_t>(index * sizeof(std::int32_t)), values[index]);
    }
    return true;
}

std::vector<std::int32_t> machine::read_i32(std::uint32_t local_byte_offset, std::size_t count) const {
    std::vector<std::int32_t> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = local_byte_offset + static_cast<std::uint32_t>(index * sizeof(std::int32_t));
        values.push_back(local_range_ok(offset, sizeof(std::int32_t)) ? load_i32(offset) : 0);
    }
    return values;
}

bool machine::write_system_i32(std::uint64_t system_byte_offset, std::span<const std::int32_t> values) {
    const auto byte_count = values.size_bytes();
    if (!system_range_ok(system_byte_offset, byte_count)) {
        return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        store_system_i32(system_byte_offset + static_cast<std::uint64_t>(index * sizeof(std::int32_t)), values[index]);
    }
    return true;
}

std::vector<std::int32_t> machine::read_system_i32(std::uint64_t system_byte_offset, std::size_t count) const {
    std::vector<std::int32_t> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = system_byte_offset + static_cast<std::uint64_t>(index * sizeof(std::int32_t));
        values.push_back(system_range_ok(offset, sizeof(std::int32_t)) ? load_system_i32(offset) : 0);
    }
    return values;
}

bool machine::issue_dma_load(
    std::uint64_t system_byte_offset,
    std::uint32_t local_byte_offset,
    std::uint32_t byte_count
) {
    if (!local_range_ok(local_byte_offset, byte_count)) {
        raise_fault(model_error::local_memory_bounds);
        return false;
    }
    if (!system_range_ok(system_byte_offset, byte_count)) {
        raise_fault(model_error::dma_request);
        return false;
    }
    std::ranges::copy(
        system_memory_.begin() + static_cast<std::ptrdiff_t>(system_byte_offset),
        system_memory_.begin() + static_cast<std::ptrdiff_t>(system_byte_offset + byte_count),
        scratchpad_.begin() + local_byte_offset
    );
    dma_events_.push_back(dma_event{
        .sequence = next_dma_sequence_++,
        .direction = dma_direction::system_to_local,
        .system_byte_offset = system_byte_offset,
        .local_byte_offset = local_byte_offset,
        .byte_count = byte_count,
    });
    return true;
}

bool machine::issue_dma_store(
    std::uint32_t local_byte_offset,
    std::uint64_t system_byte_offset,
    std::uint32_t byte_count
) {
    if (!local_range_ok(local_byte_offset, byte_count)) {
        raise_fault(model_error::local_memory_bounds);
        return false;
    }
    if (!system_range_ok(system_byte_offset, byte_count)) {
        raise_fault(model_error::dma_request);
        return false;
    }
    std::ranges::copy(
        scratchpad_.begin() + local_byte_offset,
        scratchpad_.begin() + local_byte_offset + byte_count,
        system_memory_.begin() + static_cast<std::ptrdiff_t>(system_byte_offset)
    );
    dma_events_.push_back(dma_event{
        .sequence = next_dma_sequence_++,
        .direction = dma_direction::local_to_system,
        .system_byte_offset = system_byte_offset,
        .local_byte_offset = local_byte_offset,
        .byte_count = byte_count,
    });
    return true;
}

void machine::clear_dma_events() {
    dma_events_.clear();
}

bool machine::issue_matrix_gemm_i8_i32(const matrix_gemm_i8_i32_op& op) {
    const auto flags_valid = op.clear_accumulator != op.accumulate;
    if (op.accumulator_id != 0 || !flags_valid ||
        op.m == 0 || op.n == 0 || op.k == 0 ||
        op.m > matrix_max_dimension || op.n > matrix_max_dimension ||
        op.k > matrix_max_dimension ||
        op.a_row_stride_bytes < op.k ||
        op.b_row_stride_bytes < op.n ||
        (op.store_result &&
         (op.c_row_stride_bytes < static_cast<std::uint32_t>(op.n) * sizeof(std::int32_t) ||
          op.c_offset % sizeof(std::int32_t) != 0 ||
          op.c_row_stride_bytes % sizeof(std::int32_t) != 0)) ||
        (op.accumulate &&
         (!matrix_accumulator_valid_ || matrix_accumulator_m_ != op.m ||
          matrix_accumulator_n_ != op.n))) {
        raise_fault(model_error::matrix_issue);
        return false;
    }

    const auto a_last = op.a_offset +
                        static_cast<std::uint64_t>(op.m - 1U) * op.a_row_stride_bytes +
                        static_cast<std::uint64_t>(op.k);
    const auto b_last = op.b_offset +
                        static_cast<std::uint64_t>(op.k - 1U) * op.b_row_stride_bytes +
                        static_cast<std::uint64_t>(op.n);
    const auto c_last = op.store_result
        ? op.c_offset + static_cast<std::uint64_t>(op.m - 1U) * op.c_row_stride_bytes +
              static_cast<std::uint64_t>(op.n) * sizeof(std::int32_t)
        : std::uint64_t{0};
    if (a_last > active_local_mem_bytes_ || b_last > active_local_mem_bytes_ ||
        (op.store_result && c_last > active_local_mem_bytes_)) {
        raise_fault(model_error::matrix_issue);
        return false;
    }

    if (op.clear_accumulator) {
        matrix_accumulator_ = {};
        matrix_accumulator_valid_ = true;
        matrix_accumulator_m_ = op.m;
        matrix_accumulator_n_ = op.n;
    }

    for (std::uint16_t row = 0; row < op.m; ++row) {
        for (std::uint16_t col = 0; col < op.n; ++col) {
            auto acc = matrix_accumulator_.at(row).at(col);
            for (std::uint16_t kk = 0; kk < op.k; ++kk) {
                const auto a_offset = op.a_offset +
                                      static_cast<std::uint32_t>(row) * op.a_row_stride_bytes +
                                      kk;
                const auto b_offset = op.b_offset +
                                      static_cast<std::uint32_t>(kk) * op.b_row_stride_bytes +
                                      col;
                const auto product = static_cast<std::int32_t>(load_i8(a_offset)) *
                                     static_cast<std::int32_t>(load_i8(b_offset));
                acc = wrap_add(acc, product);
            }
            matrix_accumulator_.at(row).at(col) = acc;
            if (op.store_result) {
                const auto c_offset = op.c_offset +
                                      static_cast<std::uint32_t>(row) * op.c_row_stride_bytes +
                                      static_cast<std::uint32_t>(col) * sizeof(std::int32_t);
                store_i32(c_offset, acc);
            }
        }
    }

    matrix_events_.push_back(matrix_event{
        .sequence = next_matrix_sequence_++,
        .m = op.m,
        .n = op.n,
        .k = op.k,
        .c_offset = op.c_offset,
        .accumulator_id = op.accumulator_id,
        .stored = op.store_result,
    });
    return true;
}

void machine::clear_matrix_events() {
    matrix_events_.clear();
}

run_result machine::step() {
    if (state_ == lifecycle_state::done || state_ == lifecycle_state::fault) {
        return run_result{state_, fault_, pc_, retired_};
    }
    if (pc_ % k_pc_increment != 0 || pc_ / k_pc_increment >= program_.size()) {
        raise_fault(model_error::illegal_instruction);
        return run_result{state_, fault_, pc_, retired_};
    }

    state_ = lifecycle_state::running;
    const auto inst = decode(program_.at(pc_ / k_pc_increment));
    const auto next_pc = pc_ + k_pc_increment;

    switch (inst.isa_class) {
        case HOLON_NPU_ISA_ENUM_FRONTEND_CONTROL: {
            const auto opcode = static_cast<v2_opcode>(inst.opcode);
            const auto signed_imm = sign_extend_imm12(inst.imm);
            const auto write_scalar = [&](std::uint8_t index, std::int32_t value) {
                if (index != 0) {
                    scalar_registers_.at(index) = value;
                }
            };
            if (opcode == v2_opcode::frontend_control_movi) {
                if (inst.rs1 != 0 || inst.rs2 != 0) {
                    raise_fault(model_error::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
                }
                write_scalar(inst.rd, signed_imm);
                pc_ = next_pc;
            } else if (opcode == v2_opcode::frontend_control_add) {
                if (inst.imm != 0) {
                    raise_fault(model_error::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
                }
                write_scalar(
                    inst.rd,
                    wrap_add(scalar_registers_.at(inst.rs1), scalar_registers_.at(inst.rs2))
                );
                pc_ = next_pc;
            } else if (opcode == v2_opcode::frontend_control_addi) {
                if (inst.rs2 != 0) {
                    raise_fault(model_error::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
                }
                write_scalar(inst.rd, wrap_add(scalar_registers_.at(inst.rs1), signed_imm));
                pc_ = next_pc;
            } else if (opcode == v2_opcode::frontend_control_load ||
                       opcode == v2_opcode::frontend_control_store) {
                if ((opcode == v2_opcode::frontend_control_load && inst.rs2 != 0) ||
                    (opcode == v2_opcode::frontend_control_store && inst.rd != 0)) {
                    raise_fault(model_error::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
                }
                const auto base = static_cast<std::uint64_t>(
                    std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rs1))
                );
                const auto address = static_cast<std::int64_t>(base) + signed_imm;
                if (address < 0 || address > std::numeric_limits<std::uint32_t>::max() ||
                    !aligned(static_cast<std::uint64_t>(address), sizeof(std::uint32_t)) ||
                    !local_range_ok(static_cast<std::uint32_t>(address), sizeof(std::uint32_t))) {
                    raise_fault(model_error::local_memory_bounds);
                    return run_result{state_, fault_, pc_, retired_};
                }
                if (opcode == v2_opcode::frontend_control_load) {
                    write_scalar(inst.rd, load_i32(static_cast<std::uint32_t>(address)));
                } else {
                    store_i32(
                        static_cast<std::uint32_t>(address),
                        scalar_registers_.at(inst.rs2)
                    );
                }
                pc_ = next_pc;
            } else if (opcode == v2_opcode::frontend_control_beq ||
                       opcode == v2_opcode::frontend_control_bne) {
                if (inst.rd != 0) {
                    raise_fault(model_error::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
                }
                const auto equal = scalar_registers_.at(inst.rs1) == scalar_registers_.at(inst.rs2);
                const auto taken = opcode == v2_opcode::frontend_control_beq ? equal : !equal;
                if (taken) {
                    const auto target = static_cast<std::int64_t>(pc_) +
                        static_cast<std::int64_t>(signed_imm) * HOLON_NPU_ISA_SCALAR_BRANCH_SCALE;
                    const auto program_bytes = static_cast<std::int64_t>(
                        program_.size() * sizeof(std::uint32_t)
                    );
                    if (target < 0 || target + HOLON_NPU_ISA_INSTRUCTION_BYTES > program_bytes ||
                        target % HOLON_NPU_ISA_INSTRUCTION_BYTES != 0) {
                        raise_fault(model_error::illegal_instruction);
                        return run_result{state_, fault_, pc_, retired_};
                    }
                    pc_ = static_cast<std::uint32_t>(target);
                } else {
                    pc_ = next_pc;
                }
            } else {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            scalar_registers_.at(0) = 0;
            ++retired_;
            break;
        }

        case HOLON_NPU_ISA_ENUM_PREDICATE:
            if (!predicate_index_ok(inst.rd) || inst.rs1 != 0 || inst.rs2 != 0) {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::predicate_ptrue)) {
                if (inst.imm != 0 || vl_ == 0) {
                    raise_fault(model_error::vector_config);
                    return run_result{state_, fault_, pc_, retired_};
                }
                for (std::size_t lane = 0; lane < predicate_active_.size(); ++lane) {
                    predicate_active_.at(lane) = lane < vl_ ? 1 : 0;
                }
            } else if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::predicate_load)) {
                if (!aligned(inst.imm, HOLON_NPU_ISA_PREDICATE_WORD_BYTES) ||
                    !local_range_ok(inst.imm, HOLON_NPU_ISA_PREDICATE_WORD_BYTES)) {
                    raise_fault(model_error::local_memory_bounds);
                    return run_result{state_, fault_, pc_, retired_};
                }
                const auto bits = static_cast<std::uint32_t>(load_i32(inst.imm));
                for (std::size_t lane = 0; lane < predicate_active_.size(); ++lane) {
                    predicate_active_.at(lane) = (bits >> lane) & 1U;
                }
            } else {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            pc_ = next_pc;
            ++retired_;
            break;

        case HOLON_NPU_ISA_ENUM_VECTOR_CONFIG:
            {
                const auto configured_vl =
                    (inst.imm & HOLON_NPU_ISA_VTYPE_VL_MINUS_ONE_MASK) + 1U;
                const auto sew = static_cast<std::uint8_t>(
                    (inst.imm & HOLON_NPU_ISA_VTYPE_SEW_MASK) >>
                    HOLON_NPU_ISA_VTYPE_SEW_SHIFT
                );
                const auto rounding = static_cast<std::uint8_t>(
                    (inst.imm & HOLON_NPU_ISA_VTYPE_ROUND_MASK) >>
                    HOLON_NPU_ISA_VTYPE_ROUND_SHIFT
                );
                if (inst.opcode != static_cast<std::uint8_t>(v2_opcode::vector_config_set) ||
                    inst.rd != 0 || inst.rs1 != 0 || inst.rs2 != 0 ||
                    configured_vl > max_vl_ || sew > HOLON_NPU_ISA_VTYPE_SEW_32 ||
                    rounding != HOLON_NPU_ISA_VTYPE_ROUND_RNE ||
                    (inst.imm & ~(HOLON_NPU_ISA_VTYPE_VL_MINUS_ONE_MASK |
                                  HOLON_NPU_ISA_VTYPE_SEW_MASK |
                                  HOLON_NPU_ISA_VTYPE_SIGNED |
                                  HOLON_NPU_ISA_VTYPE_ROUND_MASK |
                                  HOLON_NPU_ISA_VTYPE_SATURATE)) != 0) {
                    raise_fault(model_error::vector_config);
                    return run_result{state_, fault_, pc_, retired_};
                }
                vl_ = configured_vl;
                element_width_ = static_cast<vector_element_width>(sew);
                rounding_ = static_cast<vector_rounding>(rounding);
                elements_signed_ = (inst.imm & HOLON_NPU_ISA_VTYPE_SIGNED) != 0;
                saturate_ = (inst.imm & HOLON_NPU_ISA_VTYPE_SATURATE) != 0;
            }
            pc_ = next_pc;
            ++retired_;
            break;

        case HOLON_NPU_ISA_ENUM_VECTOR_MEMORY:
            if (!register_index_ok(inst.rd) || !predicate_index_ok(inst.rs1) ||
                inst.rs2 != 0 || vl_ == 0) {
                raise_fault(model_error::vector_config);
                return run_result{state_, fault_, pc_, retired_};
            }
            if ((inst.imm % element_bytes()) != 0 ||
                !local_range_ok(inst.imm, static_cast<std::size_t>(vl_) * element_bytes())) {
                raise_fault(model_error::local_memory_bounds);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_memory_load)) {
                for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                    if (predicate_active_.at(lane) != 0) {
                        vector_registers_.at(inst.rd).at(lane) =
                            load_element(inst.imm + lane * element_bytes());
                    }
                }
            } else if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_memory_store)) {
                for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                    if (predicate_active_.at(lane) != 0) {
                        store_element(
                            inst.imm + lane * element_bytes(),
                            vector_registers_.at(inst.rd).at(lane)
                        );
                    }
                }
            } else {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            pc_ = next_pc;
            ++retired_;
            break;

        case HOLON_NPU_ISA_ENUM_VECTOR_ALU: {
            const auto predicate = static_cast<std::uint8_t>(
                inst.imm & HOLON_NPU_ISA_VECTOR_PREDICATE_MASK
            );
            if (!register_index_ok(inst.rd) || !register_index_ok(inst.rs1) ||
                !register_index_ok(inst.rs2) || !predicate_index_ok(predicate) || vl_ == 0 ||
                (inst.imm & HOLON_NPU_ISA_VECTOR_PREDICATE_RESERVED_MASK) != 0) {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto opcode = static_cast<v2_opcode>(inst.opcode);
            const auto opcode_valid = opcode == v2_opcode::vector_alu_add ||
                opcode == v2_opcode::vector_alu_sub || opcode == v2_opcode::vector_alu_min ||
                opcode == v2_opcode::vector_alu_max || opcode == v2_opcode::vector_alu_eq ||
                opcode == v2_opcode::vector_alu_lt || opcode == v2_opcode::vector_alu_shl ||
                opcode == v2_opcode::vector_alu_srl || opcode == v2_opcode::vector_alu_sra ||
                opcode == v2_opcode::vector_alu_select;
            if (!opcode_valid) {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                if (opcode == v2_opcode::vector_alu_select) {
                    vector_registers_.at(inst.rd).at(lane) = predicate_active_.at(lane) != 0
                        ? vector_registers_.at(inst.rs1).at(lane)
                        : vector_registers_.at(inst.rs2).at(lane);
                } else if (predicate_active_.at(lane) != 0) {
                    const auto lhs = vector_registers_.at(inst.rs1).at(lane);
                    const auto rhs = vector_registers_.at(inst.rs2).at(lane);
                    switch (opcode) {
                        case v2_opcode::vector_alu_add:
                            vector_registers_.at(inst.rd).at(lane) = saturate_
                                ? saturating_add_sub(lhs, rhs, element_bytes() * 8U,
                                                     elements_signed_, false)
                                : normalize_element(static_cast<std::uint32_t>(wrap_add(lhs, rhs)));
                            break;
                        case v2_opcode::vector_alu_sub:
                            vector_registers_.at(inst.rd).at(lane) = saturate_
                                ? saturating_add_sub(lhs, rhs, element_bytes() * 8U,
                                                     elements_signed_, true)
                                : normalize_element(static_cast<std::uint32_t>(wrap_sub(lhs, rhs)));
                            break;
                        case v2_opcode::vector_alu_min:
                            vector_registers_.at(inst.rd).at(lane) = elements_signed_
                                ? std::min(lhs, rhs)
                                : (static_cast<std::uint32_t>(lhs) < static_cast<std::uint32_t>(rhs)
                                    ? lhs : rhs);
                            break;
                        case v2_opcode::vector_alu_max:
                            vector_registers_.at(inst.rd).at(lane) = elements_signed_
                                ? std::max(lhs, rhs)
                                : (static_cast<std::uint32_t>(lhs) > static_cast<std::uint32_t>(rhs)
                                    ? lhs : rhs);
                            break;
                        case v2_opcode::vector_alu_eq:
                            vector_registers_.at(inst.rd).at(lane) = lhs == rhs ? 1 : 0;
                            break;
                        case v2_opcode::vector_alu_lt:
                            vector_registers_.at(inst.rd).at(lane) = elements_signed_
                                ? (lhs < rhs ? 1 : 0)
                                : (static_cast<std::uint32_t>(lhs) < static_cast<std::uint32_t>(rhs)
                                    ? 1 : 0);
                            break;
                        case v2_opcode::vector_alu_shl:
                            vector_registers_.at(inst.rd).at(lane) =
                                normalize_element(static_cast<std::uint32_t>(lhs) <<
                                    (static_cast<std::uint32_t>(rhs) &
                                     static_cast<std::uint32_t>(element_bytes() * 8U - 1U)));
                            break;
                        case v2_opcode::vector_alu_srl:
                            {
                                const auto element_bits = static_cast<std::uint32_t>(element_bytes() * 8U);
                                const auto element_mask = element_bits == 32U
                                    ? 0xFFFF'FFFFU
                                    : (std::uint32_t{1} << element_bits) - 1U;
                                vector_registers_.at(inst.rd).at(lane) = normalize_element(
                                    (static_cast<std::uint32_t>(lhs) & element_mask) >>
                                    (static_cast<std::uint32_t>(rhs) & (element_bits - 1U))
                                );
                            }
                            break;
                        case v2_opcode::vector_alu_sra:
                            vector_registers_.at(inst.rd).at(lane) =
                                normalize_element(static_cast<std::uint32_t>(arithmetic_shift_right(
                                    lhs,
                                    static_cast<std::uint32_t>(rhs) &
                                        static_cast<std::uint32_t>(element_bytes() * 8U - 1U)
                                )));
                            break;
                        case v2_opcode::vector_alu_select:
                            break;
                        default:
                            break;
                    }
                }
            }
            pc_ = next_pc;
            ++retired_;
            break;
        }

        case HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE: {
            const auto predicate = static_cast<std::uint8_t>(
                inst.imm & HOLON_NPU_ISA_VECTOR_PREDICATE_MASK
            );
            const auto opcode = static_cast<v2_opcode>(inst.opcode);
            const auto opcode_valid = opcode == v2_opcode::vector_permute_gather ||
                opcode == v2_opcode::vector_permute_zip_lo ||
                opcode == v2_opcode::vector_permute_zip_hi ||
                opcode == v2_opcode::vector_permute_unzip_even ||
                opcode == v2_opcode::vector_permute_unzip_odd ||
                opcode == v2_opcode::vector_permute_transpose4;
            if (!opcode_valid || !register_index_ok(inst.rd) || !register_index_ok(inst.rs1) ||
                !register_index_ok(inst.rs2) || !predicate_index_ok(predicate) || vl_ == 0 ||
                (inst.imm & HOLON_NPU_ISA_VECTOR_PREDICATE_RESERVED_MASK) != 0) {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (((opcode == v2_opcode::vector_permute_zip_lo ||
                  opcode == v2_opcode::vector_permute_zip_hi ||
                  opcode == v2_opcode::vector_permute_unzip_even ||
                  opcode == v2_opcode::vector_permute_unzip_odd) && (vl_ % 2U) != 0U) ||
                (opcode == v2_opcode::vector_permute_transpose4 &&
                 (vl_ != 16U || inst.rs2 != 0))) {
                raise_fault(model_error::vector_config);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto source = vector_registers_.at(inst.rs1);
            const auto source2 = vector_registers_.at(inst.rs2);
            if (opcode == v2_opcode::vector_permute_gather) {
                for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                    if (predicate_active_.at(lane) != 0 &&
                        static_cast<std::uint32_t>(source2.at(lane)) >= vl_) {
                        raise_fault(model_error::vector_config);
                        return run_result{state_, fault_, pc_, retired_};
                    }
                }
            }
            for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                if (predicate_active_.at(lane) != 0) {
                    const auto half = vl_ / 2U;
                    switch (opcode) {
                        case v2_opcode::vector_permute_gather:
                            vector_registers_.at(inst.rd).at(lane) =
                                source.at(static_cast<std::uint32_t>(source2.at(lane)));
                            break;
                        case v2_opcode::vector_permute_zip_lo:
                        case v2_opcode::vector_permute_zip_hi: {
                            const auto source_lane = lane / 2U +
                                (opcode == v2_opcode::vector_permute_zip_hi ? half : 0U);
                            vector_registers_.at(inst.rd).at(lane) = (lane % 2U) == 0U
                                ? source.at(source_lane) : source2.at(source_lane);
                            break;
                        }
                        case v2_opcode::vector_permute_unzip_even:
                        case v2_opcode::vector_permute_unzip_odd: {
                            const auto odd = opcode == v2_opcode::vector_permute_unzip_odd ? 1U : 0U;
                            const auto source_lane = 2U * (lane % half) + odd;
                            vector_registers_.at(inst.rd).at(lane) = lane < half
                                ? source.at(source_lane) : source2.at(source_lane);
                            break;
                        }
                        case v2_opcode::vector_permute_transpose4:
                            vector_registers_.at(inst.rd).at(lane) =
                                source.at((lane % 4U) * 4U + lane / 4U);
                            break;
                        default:
                            break;
                    }
                }
            }
            pc_ = next_pc;
            ++retired_;
            break;
        }

        case HOLON_NPU_ISA_ENUM_VECTOR_REDUCTION: {
            const auto predicate = static_cast<std::uint8_t>(
                inst.imm & HOLON_NPU_ISA_VECTOR_PREDICATE_MASK
            );
            const auto opcode = static_cast<v2_opcode>(inst.opcode);
            const auto opcode_valid = opcode == v2_opcode::vector_reduction_sum ||
                opcode == v2_opcode::vector_reduction_min ||
                opcode == v2_opcode::vector_reduction_max;
            if (!opcode_valid || !register_index_ok(inst.rd) || !register_index_ok(inst.rs1) ||
                inst.rs2 != 0 || !predicate_index_ok(predicate) || vl_ == 0 ||
                (inst.imm & HOLON_NPU_ISA_VECTOR_PREDICATE_RESERVED_MASK) != 0) {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto element_bits = static_cast<std::uint32_t>(element_bytes() * 8U);
            const auto unsigned_max = element_bits == 32U
                ? std::uint32_t{0xFFFF'FFFFU}
                : (std::uint32_t{1} << element_bits) - 1U;
            const auto signed_max = static_cast<std::int32_t>(unsigned_max >> 1U);
            const auto signed_min = std::bit_cast<std::int32_t>(~static_cast<std::uint32_t>(signed_max));
            std::int32_t accumulator = opcode == v2_opcode::vector_reduction_sum
                ? 0 : opcode == v2_opcode::vector_reduction_min
                ? (elements_signed_ ? signed_max : std::bit_cast<std::int32_t>(unsigned_max))
                : (elements_signed_ ? signed_min : 0);
            for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                if (predicate_active_.at(lane) == 0) {
                    continue;
                }
                const auto value = vector_registers_.at(inst.rs1).at(lane);
                if (opcode == v2_opcode::vector_reduction_sum) {
                    accumulator = normalize_element(
                        static_cast<std::uint32_t>(wrap_add(accumulator, value))
                    );
                } else if (opcode == v2_opcode::vector_reduction_min) {
                    accumulator = elements_signed_
                        ? std::min(accumulator, value)
                        : (static_cast<std::uint32_t>(accumulator) < static_cast<std::uint32_t>(value)
                           ? accumulator : value);
                } else {
                    accumulator = elements_signed_
                        ? std::max(accumulator, value)
                        : (static_cast<std::uint32_t>(accumulator) > static_cast<std::uint32_t>(value)
                           ? accumulator : value);
                }
            }
            vector_registers_.at(inst.rd).at(0) = accumulator;
            pc_ = next_pc;
            ++retired_;
            break;
        }

        case HOLON_NPU_ISA_ENUM_QUANTIZATION: {
            if (inst.opcode != static_cast<std::uint8_t>(v2_opcode::quantization_requantize) ||
                !register_index_ok(inst.rd) || !register_index_ok(inst.rs1) ||
                !predicate_index_ok(inst.rs2) || vl_ == 0 ||
                !aligned(inst.imm, HOLON_NPU_ISA_QUANT_COMMAND_ALIGN) ||
                !local_range_ok(inst.imm, HOLON_NPU_ISA_QUANT_COMMAND_BYTES)) {
                raise_fault(model_error::vector_config);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto multiplier = load_i32(inst.imm + HOLON_NPU_ISA_QUANT_COMMAND_MULTIPLIER_OFFSET);
            const auto shift_word = std::bit_cast<std::uint32_t>(
                load_i32(inst.imm + HOLON_NPU_ISA_QUANT_COMMAND_SHIFT_OFFSET)
            );
            const auto zero_point = load_i32(inst.imm + HOLON_NPU_ISA_QUANT_COMMAND_ZERO_POINT_OFFSET);
            const auto clamp_min = load_i32(inst.imm + HOLON_NPU_ISA_QUANT_COMMAND_CLAMP_MIN_OFFSET);
            const auto clamp_max = load_i32(inst.imm + HOLON_NPU_ISA_QUANT_COMMAND_CLAMP_MAX_OFFSET);
            const auto reserved = load_i32(inst.imm + HOLON_NPU_ISA_QUANT_COMMAND_RESERVED_OFFSET);
            if (shift_word > 31U || clamp_min > clamp_max || reserved != 0) {
                raise_fault(model_error::vector_config);
                return run_result{state_, fault_, pc_, retired_};
            }
            for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                if (predicate_active_.at(lane) == 0) {
                    continue;
                }
                const auto source = vector_registers_.at(inst.rs1).at(lane);
                const auto source_wide = elements_signed_
                    ? static_cast<std::int64_t>(source)
                    : static_cast<std::int64_t>(static_cast<std::uint32_t>(source));
                const auto scaled = round_shift_nearest_even(
                    source_wide * static_cast<std::int64_t>(multiplier),
                    shift_word
                ) + static_cast<std::int64_t>(zero_point);
                const auto clamped = std::clamp(
                    scaled,
                    static_cast<std::int64_t>(clamp_min),
                    static_cast<std::int64_t>(clamp_max)
                );
                vector_registers_.at(inst.rd).at(lane) =
                    normalize_element(static_cast<std::uint32_t>(clamped));
            }
            pc_ = next_pc;
            ++retired_;
            break;
        }

        case HOLON_NPU_ISA_ENUM_MATRIX: {
            if (inst.opcode != static_cast<std::uint8_t>(v2_opcode::matrix_gemm) ||
                inst.rs1 != 0 || inst.rs2 != 0 ||
                !aligned(inst.imm, HOLON_NPU_ISA_MATRIX_COMMAND_BYTES) ||
                !local_range_ok(inst.imm, HOLON_NPU_ISA_MATRIX_COMMAND_BYTES)) {
                raise_fault(model_error::matrix_issue);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto command_word = [&](std::uint32_t offset) {
                return std::bit_cast<std::uint32_t>(load_i32(inst.imm + offset));
            };
            const auto shape = command_word(HOLON_NPU_ISA_MATRIX_COMMAND_SHAPE_OFFSET);
            const auto flags = static_cast<std::uint8_t>(
                shape >> HOLON_NPU_ISA_MATRIX_SHAPE_FLAGS_SHIFT
            );
            if (command_word(HOLON_NPU_ISA_MATRIX_COMMAND_RESERVED_OFFSET) != 0 ||
                (flags & ~HOLON_NPU_ISA_MATRIX_FLAGS_VALID_MASK) != 0) {
                raise_fault(model_error::matrix_issue);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto op = matrix_gemm_i8_i32_op{
                .a_offset = command_word(HOLON_NPU_ISA_MATRIX_COMMAND_A_OFFSET),
                .b_offset = command_word(HOLON_NPU_ISA_MATRIX_COMMAND_B_OFFSET),
                .c_offset = command_word(HOLON_NPU_ISA_MATRIX_COMMAND_C_OFFSET),
                .a_row_stride_bytes = command_word(HOLON_NPU_ISA_MATRIX_COMMAND_A_STRIDE_OFFSET),
                .b_row_stride_bytes = command_word(HOLON_NPU_ISA_MATRIX_COMMAND_B_STRIDE_OFFSET),
                .c_row_stride_bytes = command_word(HOLON_NPU_ISA_MATRIX_COMMAND_C_STRIDE_OFFSET),
                .m = static_cast<std::uint16_t>(
                    (shape >> HOLON_NPU_ISA_MATRIX_SHAPE_M_SHIFT) &
                    HOLON_NPU_ISA_MATRIX_DIMENSION_MASK
                ),
                .n = static_cast<std::uint16_t>(
                    (shape >> HOLON_NPU_ISA_MATRIX_SHAPE_N_SHIFT) &
                    HOLON_NPU_ISA_MATRIX_DIMENSION_MASK
                ),
                .k = static_cast<std::uint16_t>(
                    (shape >> HOLON_NPU_ISA_MATRIX_SHAPE_K_SHIFT) &
                    HOLON_NPU_ISA_MATRIX_DIMENSION_MASK
                ),
                .accumulator_id = inst.rd,
                .clear_accumulator = (flags & HOLON_NPU_ISA_MATRIX_FLAG_CLEAR) != 0,
                .accumulate = (flags & HOLON_NPU_ISA_MATRIX_FLAG_ACCUMULATE) != 0,
                .store_result = (flags & HOLON_NPU_ISA_MATRIX_FLAG_STORE) != 0,
            };
            if (!issue_matrix_gemm_i8_i32(op)) {
                return run_result{state_, fault_, pc_, retired_};
            }
            pc_ = next_pc;
            ++retired_;
            break;
        }

        case HOLON_NPU_ISA_ENUM_CSR_DEBUG: {
            if (inst.opcode != static_cast<std::uint8_t>(v2_opcode::csr_debug_read) ||
                inst.rs1 != 0 || inst.rs2 != 0) {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }

            std::uint32_t value = 0;
            switch (inst.imm) {
                case HOLON_NPU_ISA_CSR_PC:
                    value = pc_;
                    break;
                case HOLON_NPU_ISA_CSR_INSTRET_LO:
                    value = static_cast<std::uint32_t>(retired_);
                    break;
                case HOLON_NPU_ISA_CSR_INSTRET_HI:
                    value = static_cast<std::uint32_t>(retired_ >> 32U);
                    break;
                case HOLON_NPU_ISA_CSR_PROGRAM_SIZE_BYTES:
                    value = static_cast<std::uint32_t>(program_.size() * k_pc_increment);
                    break;
                case HOLON_NPU_ISA_CSR_LOCAL_MEM_BYTES:
                    value = static_cast<std::uint32_t>(active_local_mem_bytes_);
                    break;
                default:
                    raise_fault(model_error::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.rd != 0) {
                scalar_registers_.at(inst.rd) = std::bit_cast<std::int32_t>(value);
            }
            pc_ = next_pc;
            ++retired_;
            break;
        }

        case HOLON_NPU_ISA_ENUM_DMA: {
            const auto system_address =
                static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rd))) |
                (static_cast<std::uint64_t>(
                     std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rs1))) << 32U);
            const auto local_address =
                std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rs2));
            const auto byte_count =
                static_cast<std::size_t>(inst.imm + 1U) * HOLON_NPU_ISA_DMA_WORD_BYTES;
            if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::dma_load) &&
                !issue_dma_load(system_address, local_address, byte_count)) {
                return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::dma_store) &&
                !issue_dma_store(local_address, system_address, byte_count)) {
                return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.opcode != static_cast<std::uint8_t>(v2_opcode::dma_load) &&
                inst.opcode != static_cast<std::uint8_t>(v2_opcode::dma_store)) {
                raise_fault(model_error::dma_request);
                return run_result{state_, fault_, pc_, retired_};
            }
            pc_ = next_pc;
            ++retired_;
            break;
        }

        case HOLON_NPU_ISA_ENUM_SYNC:
            if (inst.rd != 0 || inst.rs1 != 0 || inst.rs2 != 0 || inst.imm != 0) {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::sync_wait_dma) ||
                inst.opcode == static_cast<std::uint8_t>(v2_opcode::sync_fence_local) ||
                inst.opcode == static_cast<std::uint8_t>(v2_opcode::sync_fence_dma)) {
                pc_ = next_pc;
                ++retired_;
            } else {
                raise_fault(model_error::illegal_instruction);
            }
            break;

        case HOLON_NPU_ISA_ENUM_SYSTEM:
            if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::system_exit)) {
                pc_ = next_pc;
                state_ = lifecycle_state::done;
                ++retired_;
            } else if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::system_fault)) {
                raise_fault(static_cast<model_error>(inst.imm));
            } else {
                raise_fault(model_error::illegal_instruction);
            }
            break;

        default:
            raise_fault(model_error::illegal_instruction);
            break;
    }

    return run_result{state_, fault_, pc_, retired_};
}

run_result machine::run(std::uint64_t max_instructions) {
    run_result result{state_, fault_, pc_, retired_};
    for (std::uint64_t count = 0; count < max_instructions; ++count) {
        result = step();
        if (result.state == lifecycle_state::done || result.state == lifecycle_state::fault) {
            return result;
        }
    }
    raise_fault(model_error::explicit_program_fault);
    return run_result{state_, fault_, pc_, retired_};
}

bool machine::local_range_ok(std::uint32_t local_byte_offset, std::size_t byte_count) const {
    const auto offset = static_cast<std::size_t>(local_byte_offset);
    return offset <= active_local_mem_bytes_ &&
           byte_count <= active_local_mem_bytes_ - offset;
}

bool machine::system_range_ok(std::uint64_t system_byte_offset, std::size_t byte_count) const {
    const auto offset = static_cast<std::size_t>(system_byte_offset);
    return system_byte_offset <= static_cast<std::uint64_t>(system_memory_.size()) &&
           offset <= system_memory_.size() && byte_count <= system_memory_.size() - offset;
}

std::int8_t machine::load_i8(std::uint32_t local_byte_offset) const {
    std::int8_t value = 0;
    std::memcpy(&value, scratchpad_.data() + local_byte_offset, sizeof(value));
    return value;
}

void machine::store_i8(std::uint32_t local_byte_offset, std::int8_t value) {
    std::memcpy(scratchpad_.data() + local_byte_offset, &value, sizeof(value));
}

std::int16_t machine::load_i16(std::uint32_t local_byte_offset) const {
    std::int16_t value = 0;
    std::memcpy(&value, scratchpad_.data() + local_byte_offset, sizeof(value));
    return value;
}

void machine::store_i16(std::uint32_t local_byte_offset, std::int16_t value) {
    std::memcpy(scratchpad_.data() + local_byte_offset, &value, sizeof(value));
}

std::int32_t machine::load_i32(std::uint32_t local_byte_offset) const {
    std::int32_t value = 0;
    std::memcpy(&value, scratchpad_.data() + local_byte_offset, sizeof(value));
    return value;
}

void machine::store_i32(std::uint32_t local_byte_offset, std::int32_t value) {
    std::memcpy(scratchpad_.data() + local_byte_offset, &value, sizeof(value));
}

std::int32_t machine::load_system_i32(std::uint64_t system_byte_offset) const {
    std::int32_t value = 0;
    std::memcpy(&value, system_memory_.data() + system_byte_offset, sizeof(value));
    return value;
}

void machine::store_system_i32(std::uint64_t system_byte_offset, std::int32_t value) {
    std::memcpy(system_memory_.data() + system_byte_offset, &value, sizeof(value));
}

void machine::raise_fault(model_error fault) {
    state_ = lifecycle_state::fault;
    fault_ = fault;
}

bool machine::register_index_ok(std::uint8_t index) const {
    return index < vector_registers_.size();
}

bool machine::predicate_index_ok(std::uint8_t index) const {
    return index == 0;
}

std::size_t machine::element_bytes() const {
    switch (element_width_) {
        case vector_element_width::bits_8:
            return 1;
        case vector_element_width::bits_16:
            return 2;
        case vector_element_width::bits_32:
            return 4;
    }
    return 0;
}

std::int32_t machine::normalize_element(std::uint32_t value) const {
    const auto bits = static_cast<std::uint32_t>(element_bytes() * 8U);
    const auto mask = bits == 32U ? 0xFFFF'FFFFU : ((std::uint32_t{1} << bits) - 1U);
    auto normalized = value & mask;
    if (elements_signed_ && bits < 32U && (normalized & (std::uint32_t{1} << (bits - 1U))) != 0) {
        normalized |= ~mask;
    }
    return std::bit_cast<std::int32_t>(normalized);
}

std::int32_t machine::load_element(std::uint32_t local_byte_offset) const {
    std::uint32_t value = 0;
    std::memcpy(&value, scratchpad_.data() + local_byte_offset, element_bytes());
    return normalize_element(value);
}

void machine::store_element(std::uint32_t local_byte_offset, std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    std::memcpy(scratchpad_.data() + local_byte_offset, &bits, element_bytes());
}

}  // namespace holon_npu::v2::model
