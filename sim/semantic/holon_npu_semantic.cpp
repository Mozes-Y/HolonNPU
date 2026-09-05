#include "holon_npu_semantic.hpp"
#include "holon_npu_execution.hpp"
#include "holon_npu_scalar.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <limits>
#include <ranges>

namespace holon_npu::semantic {
namespace {

constexpr std::uint32_t k_opcode_shift = HOLON_NPU_ISA_OPCODE_SHIFT;
constexpr std::uint32_t k_rd_shift = HOLON_NPU_ISA_RD_SHIFT;
constexpr std::uint32_t k_rs1_shift = HOLON_NPU_ISA_RS1_SHIFT;
constexpr std::uint32_t k_rs2_shift = HOLON_NPU_ISA_RS2_SHIFT;
constexpr std::uint32_t k_field_mask = HOLON_NPU_ISA_FIELD_MASK;
constexpr std::uint32_t k_imm_mask = HOLON_NPU_ISA_IMM_MASK;
constexpr std::uint32_t k_pc_increment = HOLON_NPU_ISA_INSTRUCTION_BYTES;
constexpr auto k_scalar_add = std::ranges::find(
    instruction::scalar_patterns, instruction::scalar_opcode::ADD, &instruction::scalar_pattern::opcode)->value;
constexpr auto k_scalar_addi = std::ranges::find(
    instruction::scalar_patterns, instruction::scalar_opcode::ADDI, &instruction::scalar_pattern::opcode)->value;

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
        const auto opcode = static_cast<instruction_opcode>(inst.opcode);
        if (opcode ==  instruction_opcode::frontend_control_movi) {
            return std::format("{}.movi s{}, {}", cls, inst.rd, signed_imm);
        }
        if (opcode ==  instruction_opcode::frontend_control_add) {
            return std::format("{}.add s{}, s{}, s{}", cls, inst.rd, inst.rs1, inst.rs2);
        }
        if (opcode ==  instruction_opcode::frontend_control_addi) {
            return std::format("{}.addi s{}, s{}, {}", cls, inst.rd, inst.rs1, signed_imm);
        }
        if (opcode ==  instruction_opcode::frontend_control_load) {
            return std::format("{}.load s{}, [s{}{:+}]", cls, inst.rd, inst.rs1, signed_imm);
        }
        if (opcode ==  instruction_opcode::frontend_control_store) {
            return std::format("{}.store s{}, [s{}{:+}]", cls, inst.rs2, inst.rs1, signed_imm);
        }
        if (opcode ==  instruction_opcode::frontend_control_beq) {
            return std::format("{}.beq s{}, s{}, {:+}", cls, inst.rs1, inst.rs2, signed_imm);
        }
        if (opcode ==  instruction_opcode::frontend_control_bne) {
            return std::format("{}.bne s{}, s{}, {:+}", cls, inst.rs1, inst.rs2, signed_imm);
        }
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_PREDICATE &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::predicate_ptrue)) {
        return std::format("{}.ptrue p{}", cls, inst.rd);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_PREDICATE &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::predicate_load)) {
        return std::format("{}.load p{}, [0x{:03X}]", cls, inst.rd, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_CONFIG &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_config_set)) {
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
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_memory_load)) {
        return std::format("{}.load v{}, p{}, [0x{:03X}]", cls, inst.rd, inst.rs1, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_MEMORY &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_memory_store)) {
        return std::format("{}.store v{}, p{}, [0x{:03X}]", cls, inst.rd, inst.rs1, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_add)) {
        return std::format("{}.add v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_sub)) {
        return std::format("{}.sub v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_min)) {
        return std::format("{}.min v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_max)) {
        return std::format("{}.max v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_eq)) {
        return std::format("{}.eq v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_lt)) {
        return std::format("{}.lt v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_shl)) {
        return std::format("{}.shl v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_srl)) {
        return std::format("{}.srl v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_sra)) {
        return std::format("{}.sra v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_alu_select)) {
        return std::format("{}.select v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_permute_gather)) {
        return std::format("{}.gather v{}, v{}, v{}, p{}", cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE) {
        const auto operation = inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_permute_zip_lo)
            ? "zip.lo" : inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_permute_zip_hi)
            ? "zip.hi" : inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_permute_unzip_even)
            ? "unzip.even" : inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_permute_unzip_odd)
            ? "unzip.odd" : inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_permute_transpose4)
            ? "transpose4" : "unknown";
        return std::format("{}.{} v{}, v{}, v{}, p{}", cls, operation,
                           inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_REDUCTION) {
        const auto operation = inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_reduction_sum)
            ? "sum" : inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_reduction_min)
            ? "min" : inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_reduction_max)
            ? "max" : "unknown";
        return std::format("{}.{} v{}, v{}, p{}", cls, operation, inst.rd, inst.rs1, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_QUANTIZATION &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::quantization_requantize)) {
        return std::format("{}.requantize v{}, v{}, p{}, [0x{:03X}]",
                           cls, inst.rd, inst.rs1, inst.rs2, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_MATRIX &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::matrix_gemm)) {
        return std::format("matrix.gemm a{}, [0x{:03X}]", inst.rd, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_CSR_DEBUG &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::csr_debug_read)) {
        return std::format("csr_debug.read s{}, 0x{:03X}", inst.rd, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYSTEM &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::system_exit)) {
        return "system.exit";
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYSTEM &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::system_fault)) {
        return inst.imm == 0 ? "system.fault" : "<reserved system.fault encoding>";
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_DMA &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::dma_load)) {
        return std::format(
            "dma.load sys={{s{},s{}}}, spm=s{}, words={}",
            inst.rs1,
            inst.rd,
            inst.rs2,
            inst.imm + 1U
        );
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_DMA &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::dma_store)) {
        return std::format(
            "dma.store sys={{s{},s{}}}, spm=s{}, words={}",
            inst.rs1,
            inst.rd,
            inst.rs2,
            inst.imm + 1U
        );
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYNC &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::sync_wait_dma)) {
        return "sync.wait_dma";
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYNC &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::sync_fence_local)) {
        return "sync.fence.local";
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYNC &&
        inst.opcode == static_cast<std::uint8_t>( instruction_opcode::sync_fence_dma)) {
        return "sync.fence.dma";
    }
    return std::format("{}.unknown opcode=0x{:X} word=0x{:08X}", cls, inst.opcode, inst.word);
}

program_machine::program_machine(std::size_t scratchpad_bytes, std::size_t max_vl)
    : scratchpad_(scratchpad_bytes),
      active_local_mem_bytes_(scratchpad_bytes),
      predicate_active_(max_vl, 1),
      max_vl_(max_vl) {
    for (auto& reg : vector_registers_) {
        reg.assign(max_vl_, 0);
    }
}

void program_machine::reset() {
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
    fault_ = architectural_fault::none;
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
    pending_.reset();
}

std::expected<void, boot_error> program_machine::boot(const boot_image& image) {
    if (pending_) {
        return std::unexpected(boot_error::operation_pending);
    }
    if (image.instructions.empty() ||
        image.instructions.size() > HOLON_NPU_PROGRAM_MEM_MAX_BYTES / k_pc_increment) {
        return std::unexpected(boot_error::invalid_program_size);
    }
    if (image.entry.value() % k_pc_increment != 0 ||
        image.entry.value() / k_pc_increment >= image.instructions.size()) {
        return std::unexpected(boot_error::invalid_entry);
    }
    if (image.local_memory_bytes == 0 ||
        image.local_memory_bytes > scratchpad_.size() ||
        image.local_memory_bytes > HOLON_NPU_LOCAL_MEM_MAX_BYTES ||
        image.local_memory_bytes % sizeof(std::uint32_t) != 0) {
        return std::unexpected(boot_error::invalid_local_memory_size);
    }
    const auto offset = image.data_address.value();
    if (offset > image.local_memory_bytes ||
        image.initial_data.size() > image.local_memory_bytes - offset) {
        return std::unexpected(boot_error::invalid_data_range);
    }

    // Allocate before reset so an invalid image or allocation failure cannot
    // partially replace the running architectural state.
    std::vector<std::uint32_t> instructions(image.instructions.begin(), image.instructions.end());
    std::vector<std::byte> data(image.initial_data.begin(), image.initial_data.end());
    reset();
    program_ = std::move(instructions);
    active_local_mem_bytes_ = image.local_memory_bytes;
    std::ranges::copy(data, scratchpad_.begin() + offset);
    pc_ = image.entry.value();
    state_ = lifecycle_state::running;
    return {};
}

void program_machine::initialize(
    std::span<const std::uint32_t> words,
    std::size_t active_local_mem_bytes,
    instruction_address entry
) {
    program_.assign(words.begin(), words.end());
    state_ = lifecycle_state::idle;
    fault_ = architectural_fault::none;
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
    pending_.reset();
    active_local_mem_bytes_ = std::min(active_local_mem_bytes, scratchpad_.size());
    pc_ = entry.value();
}

bool program_machine::load_arguments(std::span<const std::byte> bytes, local_address destination) {
    if (!local_range_ok(destination, bytes.size())) {
        return false;
    }
    std::ranges::copy(bytes, scratchpad_.begin() + destination.value());
    return true;
}

bool program_machine::write_i8(local_address destination, std::span<const std::int8_t> values) {
    if (!local_range_ok(destination, values.size())) {
        return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        store_i8(destination.value() + static_cast<std::uint32_t>(index), values[index]);
    }
    return true;
}

std::vector<std::int8_t> program_machine::read_i8(local_address source, std::size_t count) const {
    std::vector<std::int8_t> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = source.value() + static_cast<std::uint32_t>(index);
        values.push_back(local_range_ok(local_address{offset}, sizeof(std::int8_t)) ? load_i8(offset) : 0);
    }
    return values;
}

bool program_machine::write_i16(local_address destination, std::span<const std::int16_t> values) {
    const auto byte_count = values.size_bytes();
    if (!local_range_ok(destination, byte_count)) {
        return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        store_i16(destination.value() + static_cast<std::uint32_t>(index * sizeof(std::int16_t)), values[index]);
    }
    return true;
}

std::vector<std::int16_t> program_machine::read_i16(local_address source, std::size_t count) const {
    std::vector<std::int16_t> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = source.value() + static_cast<std::uint32_t>(index * sizeof(std::int16_t));
        values.push_back(local_range_ok(local_address{offset}, sizeof(std::int16_t)) ? load_i16(offset) : 0);
    }
    return values;
}

bool program_machine::write_i32(local_address destination, std::span<const std::int32_t> values) {
    const auto byte_count = values.size_bytes();
    if (!local_range_ok(destination, byte_count)) {
        return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        store_i32(destination.value() + static_cast<std::uint32_t>(index * sizeof(std::int32_t)), values[index]);
    }
    return true;
}

std::vector<std::int32_t> program_machine::read_i32(local_address source, std::size_t count) const {
    std::vector<std::int32_t> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto offset = source.value() + static_cast<std::uint32_t>(index * sizeof(std::int32_t));
        values.push_back(local_range_ok(local_address{offset}, sizeof(std::int32_t)) ? load_i32(offset) : 0);
    }
    return values;
}

void program_machine::clear_dma_events() {
    dma_events_.clear();
}

bool program_machine::issue_matrix_gemm_i8_i32(const matrix_gemm_i8_i32_op& op) {
    const auto flags_valid = op.clear_accumulator != op.accumulate;
    if (op.accumulator_id != 0 || !flags_valid ||
        op.m == 0 || op.n == 0 || op.k == 0 ||
        op.m > matrix_max_dimension || op.n > matrix_max_dimension ||
        op.k > matrix_max_dimension ||
        op.a_row_stride_bytes < op.k ||
        op.b_row_stride_bytes < op.n ||
        (op.store_result &&
         (op.c_row_stride_bytes < static_cast<std::uint32_t>(op.n) * sizeof(std::int32_t) ||
          op.c_offset.value() % sizeof(std::int32_t) != 0 ||
          op.c_row_stride_bytes % sizeof(std::int32_t) != 0)) ||
        (op.accumulate &&
         (!matrix_accumulator_valid_ || matrix_accumulator_m_ != op.m ||
          matrix_accumulator_n_ != op.n))) {
        raise_fault(architectural_fault::matrix_issue);
        return false;
    }

    const auto a_last = op.a_offset.value() +
                        static_cast<std::uint64_t>(op.m - 1U) * op.a_row_stride_bytes +
                        static_cast<std::uint64_t>(op.k);
    const auto b_last = op.b_offset.value() +
                        static_cast<std::uint64_t>(op.k - 1U) * op.b_row_stride_bytes +
                        static_cast<std::uint64_t>(op.n);
    const auto c_last = op.store_result
        ? op.c_offset.value() + static_cast<std::uint64_t>(op.m - 1U) * op.c_row_stride_bytes +
              static_cast<std::uint64_t>(op.n) * sizeof(std::int32_t)
        : std::uint64_t{0};
    if (a_last > active_local_mem_bytes_ || b_last > active_local_mem_bytes_ ||
        (op.store_result && c_last > active_local_mem_bytes_)) {
        raise_fault(architectural_fault::matrix_issue);
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
                const auto a_offset = op.a_offset.value() +
                                      static_cast<std::uint32_t>(row) * op.a_row_stride_bytes +
                                      kk;
                const auto b_offset = op.b_offset.value() +
                                      static_cast<std::uint32_t>(kk) * op.b_row_stride_bytes +
                                      col;
                const auto product = static_cast<std::int32_t>(load_i8(a_offset)) *
                                     static_cast<std::int32_t>(load_i8(b_offset));
                acc = wrap_add(acc, product);
            }
            matrix_accumulator_.at(row).at(col) = acc;
            if (op.store_result) {
                const auto c_offset = op.c_offset.value() +
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

void program_machine::clear_matrix_events() {
    matrix_events_.clear();
}

std::optional<operation> program_machine::operation_for(const decoded_instruction& inst) {
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_FRONTEND_CONTROL) {
        const auto opcode = static_cast<instruction_opcode>(inst.opcode);
        if (opcode == instruction_opcode::frontend_control_load ||
            opcode == instruction_opcode::frontend_control_store) {
            const auto base = static_cast<std::uint64_t>(
                std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rs1))
            );
            const auto address = static_cast<std::int64_t>(base) + sign_extend_imm12(inst.imm);
            return scalar_local_operation{
                .address = local_address{
                    address >= 0 && address <= std::numeric_limits<std::uint32_t>::max()
                        ? static_cast<std::uint32_t>(address)
                        : 0U
                },
                .write = opcode == instruction_opcode::frontend_control_store,
            };
        }
        return std::nullopt;
    }

    if (inst.isa_class == HOLON_NPU_ISA_ENUM_PREDICATE ||
        inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_CONFIG ||
        inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU ||
        inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_MEMORY ||
        inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE ||
        inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_REDUCTION ||
        inst.isa_class == HOLON_NPU_ISA_ENUM_QUANTIZATION) {
        auto active_lanes = vl_;
        std::optional<std::uint8_t> predicate_index;
        if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_MEMORY) {
            predicate_index = inst.rs1;
        } else if (inst.isa_class == HOLON_NPU_ISA_ENUM_QUANTIZATION) {
            predicate_index = inst.rs2;
        } else if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU ||
                   inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE ||
                   inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_REDUCTION) {
            predicate_index = static_cast<std::uint8_t>(
                inst.imm & HOLON_NPU_ISA_FIELD_MASK
            );
        }
        if (predicate_index && predicate_index_ok(*predicate_index)) {
            active_lanes = static_cast<std::uint32_t>(std::ranges::count(
                predicate_active_ | std::views::take(vl_),
                std::uint8_t{1}
            ));
        }
        return vector_operation{
            .instruction = inst,
            .vl = vl_,
            .active_lanes = active_lanes,
            .element_bytes = static_cast<std::uint8_t>(element_bytes()),
        };
    }

    if (inst.isa_class == HOLON_NPU_ISA_ENUM_MATRIX) {
        matrix_gemm_i8_i32_op command{};
        if (local_range_ok(inst.imm, HOLON_NPU_ISA_MATRIX_COMMAND_BYTES)) {
            const auto command_word = [&](std::uint32_t offset) {
                return std::bit_cast<std::uint32_t>(load_i32(inst.imm + offset));
            };
            const auto shape = command_word(HOLON_NPU_ISA_MATRIX_COMMAND_SHAPE_OFFSET);
            const auto flags = static_cast<std::uint8_t>(
                shape >> HOLON_NPU_ISA_MATRIX_SHAPE_FLAGS_SHIFT
            );
            command = matrix_gemm_i8_i32_op{
                .a_offset = local_address{command_word(HOLON_NPU_ISA_MATRIX_COMMAND_A_OFFSET)},
                .b_offset = local_address{command_word(HOLON_NPU_ISA_MATRIX_COMMAND_B_OFFSET)},
                .c_offset = local_address{command_word(HOLON_NPU_ISA_MATRIX_COMMAND_C_OFFSET)},
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
        }
        return matrix_operation{.command = command};
    }

    if (inst.isa_class == HOLON_NPU_ISA_ENUM_DMA) {
        const auto opcode = static_cast<instruction_opcode>(inst.opcode);
        if (opcode != instruction_opcode::dma_load && opcode != instruction_opcode::dma_store) {
            raise_fault(architectural_fault::dma_request);
            return std::nullopt;
        }
        const auto system = system_address{
            static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rd))) |
            (static_cast<std::uint64_t>(
                 std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rs1))) << 32U)
        };
        const auto local = local_address{
            std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rs2))
        };
        const auto byte_count = static_cast<std::uint32_t>(
            static_cast<std::size_t>(inst.imm + 1U) * HOLON_NPU_ISA_DMA_WORD_BYTES
        );
        if (!local_range_ok(local, byte_count)) {
            raise_fault(architectural_fault::local_memory_bounds);
            return std::nullopt;
        }
        return program_dma_operation{
            .direction = opcode == instruction_opcode::dma_load
                ? dma_direction::system_to_local
                : dma_direction::local_to_system,
            .system = system,
            .local = local,
            .byte_count = byte_count,
            .store_payload = opcode == instruction_opcode::dma_store
                ? read_local_bytes(local, byte_count)
                : std::vector<std::byte>{},
        };
    }

    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYNC) {
        return sync_operation{.opcode = static_cast<instruction_opcode>(inst.opcode)};
    }
    return std::nullopt;
}

std::expected<execution_event, api_error> program_machine::advance() {
    if (pending_) {
        return std::unexpected(api_error::operation_pending);
    }
    if (state_ == lifecycle_state::done || state_ == lifecycle_state::fault) {
        return terminal_event{state_, fault_, instruction_address{pc_}, retired_};
    }
    if (pc_ % k_pc_increment != 0 || pc_ / k_pc_increment >= program_.size()) {
        raise_fault(architectural_fault::illegal_instruction);
        return terminal_event{state_, fault_, instruction_address{pc_}, retired_};
    }

    state_ = lifecycle_state::running;
    const auto inst = decode(program_.at(pc_ / k_pc_increment));
    if (auto request = operation_for(inst)) {
        pending_operation public_request{
            .token = operation_token{next_token_++},
            .pc = instruction_address{pc_},
            .value = std::move(*request),
        };
        pending_ = pending_context{public_request, inst};
        return public_request;
    }
    if (state_ == lifecycle_state::fault) {
        return terminal_event{state_, fault_, instruction_address{pc_}, retired_};
    }

    const auto retired_pc = pc_;
    const auto result = execute_current_instruction();
    if (result.state == lifecycle_state::done || result.state == lifecycle_state::fault) {
        return terminal_event{result.state, result.fault, instruction_address{result.pc}, result.retired};
    }
    return retired_event{instruction_address{retired_pc}, inst.word, result.retired};
}

bool program_machine::complete_dma(
    const program_dma_operation& request,
    const operation_result& result
) {
    if (request.direction == dma_direction::system_to_local) {
        const auto* payload = std::get_if<read_payload>(&result);
        if (payload == nullptr || payload->bytes.size() != request.byte_count ||
            !write_local_bytes(request.local, payload->bytes)) {
            return false;
        }
    } else if (!std::holds_alternative<operation_success>(result)) {
        return false;
    }

    dma_events_.push_back(dma_event{
        .sequence = next_dma_sequence_++,
        .direction = request.direction,
        .system_byte_offset = request.system,
        .local_byte_offset = request.local,
        .byte_count = request.byte_count,
    });
    pc_ += k_pc_increment;
    ++retired_;
    return true;
}

std::expected<execution_event, api_error> program_machine::complete(
    operation_token token,
    operation_result result
) {
    if (!pending_) {
        return std::unexpected(api_error::no_pending_operation);
    }
    if (pending_->public_operation.token != token) {
        return std::unexpected(api_error::token_mismatch);
    }

    if (!std::holds_alternative<operation_failure>(result)) {
        const auto* dma = std::get_if<program_dma_operation>(
            &pending_->public_operation.value
        );
        const auto valid_result = dma != nullptr &&
                dma->direction == dma_direction::system_to_local
            ? std::holds_alternative<read_payload>(result) &&
                std::get<read_payload>(result).bytes.size() == dma->byte_count
            : std::holds_alternative<operation_success>(result);
        if (!valid_result) {
            return std::unexpected(api_error::invalid_completion);
        }
    }

    auto context = std::move(*pending_);
    pending_.reset();
    if (const auto* failure = std::get_if<operation_failure>(&result)) {
        raise_fault(
            failure->fault == architectural_fault::none
                ? architectural_fault::dma_request
                : failure->fault
        );
        return terminal_event{state_, fault_, instruction_address{pc_}, retired_};
    }

    const auto retired_pc = pc_;
    if (const auto* dma = std::get_if<program_dma_operation>(&context.public_operation.value)) {
        if (!complete_dma(*dma, result)) {
            return std::unexpected(api_error::invalid_completion);
        }
    } else {
        if (!std::holds_alternative<operation_success>(result)) {
            return std::unexpected(api_error::invalid_completion);
        }
        const auto execution = execute_current_instruction();
        if (execution.state == lifecycle_state::done || execution.state == lifecycle_state::fault) {
            return terminal_event{
                execution.state,
                execution.fault,
                instruction_address{execution.pc},
                execution.retired,
            };
        }
    }

    return retired_event{
        instruction_address{retired_pc},
        context.instruction.word,
        retired_,
    };
}

run_result program_machine::execute_current_instruction() {
    if (state_ == lifecycle_state::done || state_ == lifecycle_state::fault) {
        return run_result{state_, fault_, pc_, retired_};
    }
    if (pc_ % k_pc_increment != 0 || pc_ / k_pc_increment >= program_.size()) {
        raise_fault(architectural_fault::illegal_instruction);
        return run_result{state_, fault_, pc_, retired_};
    }

    state_ = lifecycle_state::running;
    const auto inst = decode(program_.at(pc_ / k_pc_increment));
    const auto next_pc = pc_ + k_pc_increment;

    switch (inst.isa_class) {
        case HOLON_NPU_ISA_ENUM_FRONTEND_CONTROL: {
            const auto opcode = static_cast<instruction_opcode>(inst.opcode);
            const auto signed_imm = sign_extend_imm12(inst.imm);
            const auto write_scalar = [&](std::uint8_t index, std::int32_t value) {
                if (index != 0) {
                    scalar_registers_.at(index) = value;
                }
            };
            if (opcode == instruction_opcode::frontend_control_movi ||
                opcode == instruction_opcode::frontend_control_add ||
                opcode == instruction_opcode::frontend_control_addi) {
                const auto add = opcode == instruction_opcode::frontend_control_add;
                if ((add && inst.imm != 0) || (!add && inst.rs2 != 0) ||
                    (opcode == instruction_opcode::frontend_control_movi && inst.rs1 != 0)) {
                    raise_fault(architectural_fault::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
                }
                // Lower the migration encoding into the one scalar arithmetic owner.
                const instruction::scalar_word word{(add ? k_scalar_add : k_scalar_addi)
                    | (std::uint32_t{inst.rd} << instruction::rd_shift)
                    | (std::uint32_t{inst.rs1} << instruction::rs1_shift)
                    | (add ? std::uint32_t{inst.rs2} << instruction::rs2_shift
                           : std::uint32_t{inst.imm} << 20)};
                const auto evaluated = scalar::evaluate(word, instruction_address{pc_}, {
                    std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rs1)),
                    std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rs2))});
                if (!evaluated) {
                    raise_fault(architectural_fault::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
                }
                if (const auto& write = std::get<scalar::register_result>(evaluated->value).write)
                    write_scalar(write->destination.value(), std::bit_cast<std::int32_t>(write->value));
                pc_ = evaluated->next_pc.value();
            } else if (opcode ==  instruction_opcode::frontend_control_load ||
                       opcode ==  instruction_opcode::frontend_control_store) {
                if ((opcode ==  instruction_opcode::frontend_control_load && inst.rs2 != 0) ||
                    (opcode ==  instruction_opcode::frontend_control_store && inst.rd != 0)) {
                    raise_fault(architectural_fault::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
                }
                const auto base = static_cast<std::uint64_t>(
                    std::bit_cast<std::uint32_t>(scalar_registers_.at(inst.rs1))
                );
                const auto address = static_cast<std::int64_t>(base) + signed_imm;
                if (address < 0 || address > std::numeric_limits<std::uint32_t>::max() ||
                    !aligned(static_cast<std::uint64_t>(address), sizeof(std::uint32_t)) ||
                    !local_range_ok(static_cast<std::uint32_t>(address), sizeof(std::uint32_t))) {
                    raise_fault(architectural_fault::local_memory_bounds);
                    return run_result{state_, fault_, pc_, retired_};
                }
                if (opcode ==  instruction_opcode::frontend_control_load) {
                    write_scalar(inst.rd, load_i32(static_cast<std::uint32_t>(address)));
                } else {
                    store_i32(
                        static_cast<std::uint32_t>(address),
                        scalar_registers_.at(inst.rs2)
                    );
                }
                pc_ = next_pc;
            } else if (opcode ==  instruction_opcode::frontend_control_beq ||
                       opcode ==  instruction_opcode::frontend_control_bne) {
                if (inst.rd != 0) {
                    raise_fault(architectural_fault::illegal_instruction);
                    return run_result{state_, fault_, pc_, retired_};
                }
                const auto equal = scalar_registers_.at(inst.rs1) == scalar_registers_.at(inst.rs2);
                const auto taken = opcode ==  instruction_opcode::frontend_control_beq ? equal : !equal;
                if (taken) {
                    const auto target = static_cast<std::int64_t>(pc_) +
                        static_cast<std::int64_t>(signed_imm) * HOLON_NPU_ISA_SCALAR_BRANCH_SCALE;
                    const auto program_bytes = static_cast<std::int64_t>(
                        program_.size() * sizeof(std::uint32_t)
                    );
                    if (target < 0 || target + HOLON_NPU_ISA_INSTRUCTION_BYTES > program_bytes ||
                        target % HOLON_NPU_ISA_INSTRUCTION_BYTES != 0) {
                        raise_fault(architectural_fault::illegal_instruction);
                        return run_result{state_, fault_, pc_, retired_};
                    }
                    pc_ = static_cast<std::uint32_t>(target);
                } else {
                    pc_ = next_pc;
                }
            } else {
                raise_fault(architectural_fault::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            scalar_registers_.at(0) = 0;
            ++retired_;
            break;
        }

        case HOLON_NPU_ISA_ENUM_PREDICATE:
            if (!predicate_index_ok(inst.rd) || inst.rs1 != 0 || inst.rs2 != 0) {
                raise_fault(architectural_fault::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.opcode == static_cast<std::uint8_t>( instruction_opcode::predicate_ptrue)) {
                if (inst.imm != 0 || vl_ == 0) {
                    raise_fault(architectural_fault::vector_config);
                    return run_result{state_, fault_, pc_, retired_};
                }
                for (std::size_t lane = 0; lane < predicate_active_.size(); ++lane) {
                    predicate_active_.at(lane) = lane < vl_ ? 1 : 0;
                }
            } else if (inst.opcode == static_cast<std::uint8_t>( instruction_opcode::predicate_load)) {
                if (!aligned(inst.imm, HOLON_NPU_ISA_PREDICATE_WORD_BYTES) ||
                    !local_range_ok(inst.imm, HOLON_NPU_ISA_PREDICATE_WORD_BYTES)) {
                    raise_fault(architectural_fault::local_memory_bounds);
                    return run_result{state_, fault_, pc_, retired_};
                }
                const auto bits = static_cast<std::uint32_t>(load_i32(inst.imm));
                for (std::size_t lane = 0; lane < predicate_active_.size(); ++lane) {
                    predicate_active_.at(lane) = (bits >> lane) & 1U;
                }
            } else {
                raise_fault(architectural_fault::illegal_instruction);
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
                if (inst.opcode != static_cast<std::uint8_t>( instruction_opcode::vector_config_set) ||
                    inst.rd != 0 || inst.rs1 != 0 || inst.rs2 != 0 ||
                    configured_vl > max_vl_ || sew > HOLON_NPU_ISA_VTYPE_SEW_32 ||
                    rounding != HOLON_NPU_ISA_VTYPE_ROUND_RNE ||
                    (inst.imm & ~(HOLON_NPU_ISA_VTYPE_VL_MINUS_ONE_MASK |
                                  HOLON_NPU_ISA_VTYPE_SEW_MASK |
                                  HOLON_NPU_ISA_VTYPE_SIGNED |
                                  HOLON_NPU_ISA_VTYPE_ROUND_MASK |
                                  HOLON_NPU_ISA_VTYPE_SATURATE)) != 0) {
                    raise_fault(architectural_fault::vector_config);
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
                raise_fault(architectural_fault::vector_config);
                return run_result{state_, fault_, pc_, retired_};
            }
            if ((inst.imm % element_bytes()) != 0 ||
                !local_range_ok(inst.imm, static_cast<std::size_t>(vl_) * element_bytes())) {
                raise_fault(architectural_fault::local_memory_bounds);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_memory_load)) {
                for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                    if (predicate_active_.at(lane) != 0) {
                        vector_registers_.at(inst.rd).at(lane) =
                            load_element(inst.imm + lane * element_bytes());
                    }
                }
            } else if (inst.opcode == static_cast<std::uint8_t>( instruction_opcode::vector_memory_store)) {
                for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                    if (predicate_active_.at(lane) != 0) {
                        store_element(
                            inst.imm + lane * element_bytes(),
                            vector_registers_.at(inst.rd).at(lane)
                        );
                    }
                }
            } else {
                raise_fault(architectural_fault::illegal_instruction);
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
                raise_fault(architectural_fault::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto opcode = static_cast<instruction_opcode>(inst.opcode);
            const auto opcode_valid = opcode ==  instruction_opcode::vector_alu_add ||
                opcode ==  instruction_opcode::vector_alu_sub || opcode ==  instruction_opcode::vector_alu_min ||
                opcode ==  instruction_opcode::vector_alu_max || opcode ==  instruction_opcode::vector_alu_eq ||
                opcode ==  instruction_opcode::vector_alu_lt || opcode ==  instruction_opcode::vector_alu_shl ||
                opcode ==  instruction_opcode::vector_alu_srl || opcode ==  instruction_opcode::vector_alu_sra ||
                opcode ==  instruction_opcode::vector_alu_select;
            if (!opcode_valid) {
                raise_fault(architectural_fault::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                if (opcode ==  instruction_opcode::vector_alu_select) {
                    vector_registers_.at(inst.rd).at(lane) = predicate_active_.at(lane) != 0
                        ? vector_registers_.at(inst.rs1).at(lane)
                        : vector_registers_.at(inst.rs2).at(lane);
                } else if (predicate_active_.at(lane) != 0) {
                    const auto lhs = vector_registers_.at(inst.rs1).at(lane);
                    const auto rhs = vector_registers_.at(inst.rs2).at(lane);
                    switch (opcode) {
                        case  instruction_opcode::vector_alu_add:
                            vector_registers_.at(inst.rd).at(lane) = saturate_
                                ? saturating_add_sub(lhs, rhs, element_bytes() * 8U,
                                                     elements_signed_, false)
                                : normalize_element(static_cast<std::uint32_t>(wrap_add(lhs, rhs)));
                            break;
                        case  instruction_opcode::vector_alu_sub:
                            vector_registers_.at(inst.rd).at(lane) = saturate_
                                ? saturating_add_sub(lhs, rhs, element_bytes() * 8U,
                                                     elements_signed_, true)
                                : normalize_element(static_cast<std::uint32_t>(wrap_sub(lhs, rhs)));
                            break;
                        case  instruction_opcode::vector_alu_min:
                            vector_registers_.at(inst.rd).at(lane) = elements_signed_
                                ? std::min(lhs, rhs)
                                : (static_cast<std::uint32_t>(lhs) < static_cast<std::uint32_t>(rhs)
                                    ? lhs : rhs);
                            break;
                        case  instruction_opcode::vector_alu_max:
                            vector_registers_.at(inst.rd).at(lane) = elements_signed_
                                ? std::max(lhs, rhs)
                                : (static_cast<std::uint32_t>(lhs) > static_cast<std::uint32_t>(rhs)
                                    ? lhs : rhs);
                            break;
                        case  instruction_opcode::vector_alu_eq:
                            vector_registers_.at(inst.rd).at(lane) = lhs == rhs ? 1 : 0;
                            break;
                        case  instruction_opcode::vector_alu_lt:
                            vector_registers_.at(inst.rd).at(lane) = elements_signed_
                                ? (lhs < rhs ? 1 : 0)
                                : (static_cast<std::uint32_t>(lhs) < static_cast<std::uint32_t>(rhs)
                                    ? 1 : 0);
                            break;
                        case  instruction_opcode::vector_alu_shl:
                            vector_registers_.at(inst.rd).at(lane) =
                                normalize_element(static_cast<std::uint32_t>(lhs) <<
                                    (static_cast<std::uint32_t>(rhs) &
                                     static_cast<std::uint32_t>(element_bytes() * 8U - 1U)));
                            break;
                        case  instruction_opcode::vector_alu_srl:
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
                        case  instruction_opcode::vector_alu_sra:
                            vector_registers_.at(inst.rd).at(lane) =
                                normalize_element(static_cast<std::uint32_t>(arithmetic_shift_right(
                                    lhs,
                                    static_cast<std::uint32_t>(rhs) &
                                        static_cast<std::uint32_t>(element_bytes() * 8U - 1U)
                                )));
                            break;
                        case  instruction_opcode::vector_alu_select:
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
            const auto opcode = static_cast<instruction_opcode>(inst.opcode);
            const auto opcode_valid = opcode ==  instruction_opcode::vector_permute_gather ||
                opcode ==  instruction_opcode::vector_permute_zip_lo ||
                opcode ==  instruction_opcode::vector_permute_zip_hi ||
                opcode ==  instruction_opcode::vector_permute_unzip_even ||
                opcode ==  instruction_opcode::vector_permute_unzip_odd ||
                opcode ==  instruction_opcode::vector_permute_transpose4;
            if (!opcode_valid || !register_index_ok(inst.rd) || !register_index_ok(inst.rs1) ||
                !register_index_ok(inst.rs2) || !predicate_index_ok(predicate) || vl_ == 0 ||
                (inst.imm & HOLON_NPU_ISA_VECTOR_PREDICATE_RESERVED_MASK) != 0) {
                raise_fault(architectural_fault::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (((opcode ==  instruction_opcode::vector_permute_zip_lo ||
                  opcode ==  instruction_opcode::vector_permute_zip_hi ||
                  opcode ==  instruction_opcode::vector_permute_unzip_even ||
                  opcode ==  instruction_opcode::vector_permute_unzip_odd) && (vl_ % 2U) != 0U) ||
                (opcode ==  instruction_opcode::vector_permute_transpose4 &&
                 (vl_ != 16U || inst.rs2 != 0))) {
                raise_fault(architectural_fault::vector_config);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto source = vector_registers_.at(inst.rs1);
            const auto source2 = vector_registers_.at(inst.rs2);
            if (opcode ==  instruction_opcode::vector_permute_gather) {
                for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                    if (predicate_active_.at(lane) != 0 &&
                        static_cast<std::uint32_t>(source2.at(lane)) >= vl_) {
                        raise_fault(architectural_fault::vector_config);
                        return run_result{state_, fault_, pc_, retired_};
                    }
                }
            }
            for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                if (predicate_active_.at(lane) != 0) {
                    const auto half = vl_ / 2U;
                    switch (opcode) {
                        case  instruction_opcode::vector_permute_gather:
                            vector_registers_.at(inst.rd).at(lane) =
                                source.at(static_cast<std::uint32_t>(source2.at(lane)));
                            break;
                        case  instruction_opcode::vector_permute_zip_lo:
                        case  instruction_opcode::vector_permute_zip_hi: {
                            const auto source_lane = lane / 2U +
                                (opcode ==  instruction_opcode::vector_permute_zip_hi ? half : 0U);
                            vector_registers_.at(inst.rd).at(lane) = (lane % 2U) == 0U
                                ? source.at(source_lane) : source2.at(source_lane);
                            break;
                        }
                        case  instruction_opcode::vector_permute_unzip_even:
                        case  instruction_opcode::vector_permute_unzip_odd: {
                            const auto odd = opcode ==  instruction_opcode::vector_permute_unzip_odd ? 1U : 0U;
                            const auto source_lane = 2U * (lane % half) + odd;
                            vector_registers_.at(inst.rd).at(lane) = lane < half
                                ? source.at(source_lane) : source2.at(source_lane);
                            break;
                        }
                        case  instruction_opcode::vector_permute_transpose4:
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
            const auto opcode = static_cast<instruction_opcode>(inst.opcode);
            const auto opcode_valid = opcode ==  instruction_opcode::vector_reduction_sum ||
                opcode ==  instruction_opcode::vector_reduction_min ||
                opcode ==  instruction_opcode::vector_reduction_max;
            if (!opcode_valid || !register_index_ok(inst.rd) || !register_index_ok(inst.rs1) ||
                inst.rs2 != 0 || !predicate_index_ok(predicate) || vl_ == 0 ||
                (inst.imm & HOLON_NPU_ISA_VECTOR_PREDICATE_RESERVED_MASK) != 0) {
                raise_fault(architectural_fault::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto element_bits = static_cast<std::uint32_t>(element_bytes() * 8U);
            const auto unsigned_max = element_bits == 32U
                ? std::uint32_t{0xFFFF'FFFFU}
                : (std::uint32_t{1} << element_bits) - 1U;
            const auto signed_max = static_cast<std::int32_t>(unsigned_max >> 1U);
            const auto signed_min = std::bit_cast<std::int32_t>(~static_cast<std::uint32_t>(signed_max));
            std::int32_t accumulator = opcode ==  instruction_opcode::vector_reduction_sum
                ? 0 : opcode ==  instruction_opcode::vector_reduction_min
                ? (elements_signed_ ? signed_max : std::bit_cast<std::int32_t>(unsigned_max))
                : (elements_signed_ ? signed_min : 0);
            for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                if (predicate_active_.at(lane) == 0) {
                    continue;
                }
                const auto value = vector_registers_.at(inst.rs1).at(lane);
                if (opcode ==  instruction_opcode::vector_reduction_sum) {
                    accumulator = normalize_element(
                        static_cast<std::uint32_t>(wrap_add(accumulator, value))
                    );
                } else if (opcode ==  instruction_opcode::vector_reduction_min) {
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
            if (inst.opcode != static_cast<std::uint8_t>( instruction_opcode::quantization_requantize) ||
                !register_index_ok(inst.rd) || !register_index_ok(inst.rs1) ||
                !predicate_index_ok(inst.rs2) || vl_ == 0 ||
                !aligned(inst.imm, HOLON_NPU_ISA_QUANT_COMMAND_ALIGN) ||
                !local_range_ok(inst.imm, HOLON_NPU_ISA_QUANT_COMMAND_BYTES)) {
                raise_fault(architectural_fault::vector_config);
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
                raise_fault(architectural_fault::vector_config);
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
            if (inst.opcode != static_cast<std::uint8_t>( instruction_opcode::matrix_gemm) ||
                inst.rs1 != 0 || inst.rs2 != 0 ||
                !aligned(inst.imm, HOLON_NPU_ISA_MATRIX_COMMAND_BYTES) ||
                !local_range_ok(inst.imm, HOLON_NPU_ISA_MATRIX_COMMAND_BYTES)) {
                raise_fault(architectural_fault::matrix_issue);
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
                raise_fault(architectural_fault::matrix_issue);
                return run_result{state_, fault_, pc_, retired_};
            }
            const auto op = matrix_gemm_i8_i32_op{
                .a_offset = local_address{command_word(HOLON_NPU_ISA_MATRIX_COMMAND_A_OFFSET)},
                .b_offset = local_address{command_word(HOLON_NPU_ISA_MATRIX_COMMAND_B_OFFSET)},
                .c_offset = local_address{command_word(HOLON_NPU_ISA_MATRIX_COMMAND_C_OFFSET)},
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
            if (inst.opcode != static_cast<std::uint8_t>( instruction_opcode::csr_debug_read) ||
                inst.rs1 != 0 || inst.rs2 != 0) {
                raise_fault(architectural_fault::illegal_instruction);
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
                    raise_fault(architectural_fault::illegal_instruction);
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
            raise_fault(architectural_fault::dma_request);
            break;
        }

        case HOLON_NPU_ISA_ENUM_SYNC:
            if (inst.rd != 0 || inst.rs1 != 0 || inst.rs2 != 0 || inst.imm != 0) {
                raise_fault(architectural_fault::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.opcode == static_cast<std::uint8_t>( instruction_opcode::sync_wait_dma) ||
                inst.opcode == static_cast<std::uint8_t>( instruction_opcode::sync_fence_local) ||
                inst.opcode == static_cast<std::uint8_t>( instruction_opcode::sync_fence_dma)) {
                pc_ = next_pc;
                ++retired_;
            } else {
                raise_fault(architectural_fault::illegal_instruction);
            }
            break;

        case HOLON_NPU_ISA_ENUM_SYSTEM:
            if (inst.opcode == static_cast<std::uint8_t>( instruction_opcode::system_exit)) {
                pc_ = next_pc;
                state_ = lifecycle_state::done;
                ++retired_;
            } else if (inst.opcode == static_cast<std::uint8_t>( instruction_opcode::system_fault)) {
                raise_fault(
                    inst.imm == 0
                        ? architectural_fault::explicit_program_fault
                        : architectural_fault::illegal_instruction
                );
            } else {
                raise_fault(architectural_fault::illegal_instruction);
            }
            break;

        default:
            raise_fault(architectural_fault::illegal_instruction);
            break;
    }

    return run_result{state_, fault_, pc_, retired_};
}

bool program_machine::local_range_ok(local_address address, std::size_t byte_count) const {
    const auto offset = static_cast<std::size_t>(address.value());
    return offset <= active_local_mem_bytes_ &&
           byte_count <= active_local_mem_bytes_ - offset;
}

std::vector<std::byte> program_machine::read_local_bytes(
    local_address address,
    std::size_t byte_count
) const {
    if (!local_range_ok(address, byte_count)) {
        return {};
    }
    const auto first = scratchpad_.begin() + address.value();
    return {first, first + static_cast<std::ptrdiff_t>(byte_count)};
}

bool program_machine::write_local_bytes(
    local_address address,
    std::span<const std::byte> bytes
) {
    if (!local_range_ok(address, bytes.size())) {
        return false;
    }
    std::ranges::copy(bytes, scratchpad_.begin() + address.value());
    return true;
}

std::int8_t program_machine::load_i8(std::uint32_t local_byte_offset) const {
    std::int8_t value = 0;
    std::memcpy(&value, scratchpad_.data() + local_byte_offset, sizeof(value));
    return value;
}

void program_machine::store_i8(std::uint32_t local_byte_offset, std::int8_t value) {
    std::memcpy(scratchpad_.data() + local_byte_offset, &value, sizeof(value));
}

std::int16_t program_machine::load_i16(std::uint32_t local_byte_offset) const {
    std::int16_t value = 0;
    std::memcpy(&value, scratchpad_.data() + local_byte_offset, sizeof(value));
    return value;
}

void program_machine::store_i16(std::uint32_t local_byte_offset, std::int16_t value) {
    std::memcpy(scratchpad_.data() + local_byte_offset, &value, sizeof(value));
}

std::int32_t program_machine::load_i32(std::uint32_t local_byte_offset) const {
    std::int32_t value = 0;
    std::memcpy(&value, scratchpad_.data() + local_byte_offset, sizeof(value));
    return value;
}

void program_machine::store_i32(std::uint32_t local_byte_offset, std::int32_t value) {
    std::memcpy(scratchpad_.data() + local_byte_offset, &value, sizeof(value));
}

void program_machine::raise_fault(architectural_fault fault) {
    state_ = lifecycle_state::fault;
    fault_ = fault;
}

bool program_machine::register_index_ok(std::uint8_t index) const {
    return index < vector_registers_.size();
}

bool program_machine::predicate_index_ok(std::uint8_t index) const {
    return index == 0;
}

std::size_t program_machine::element_bytes() const {
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

std::int32_t program_machine::normalize_element(std::uint32_t value) const {
    const auto bits = static_cast<std::uint32_t>(element_bytes() * 8U);
    const auto mask = bits == 32U ? 0xFFFF'FFFFU : ((std::uint32_t{1} << bits) - 1U);
    auto normalized = value & mask;
    if (elements_signed_ && bits < 32U && (normalized & (std::uint32_t{1} << (bits - 1U))) != 0) {
        normalized |= ~mask;
    }
    return std::bit_cast<std::int32_t>(normalized);
}

std::int32_t program_machine::load_element(std::uint32_t local_byte_offset) const {
    std::uint32_t value = 0;
    std::memcpy(&value, scratchpad_.data() + local_byte_offset, element_bytes());
    return normalize_element(value);
}

void program_machine::store_element(std::uint32_t local_byte_offset, std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    std::memcpy(scratchpad_.data() + local_byte_offset, &bits, element_bytes());
}

device::device(std::size_t scratchpad_bytes, std::size_t max_vl, loader_config config)
    : program_(scratchpad_bytes, max_vl), config_(config) {}

void device::reset() {
    program_.reset();
    phase_ = phase::idle;
    state_ = lifecycle_state::idle;
    fault_ = architectural_fault::none;
    descriptor_address_ = system_address{};
    descriptor_ = {};
    fetched_program_.clear();
    fetched_arguments_.clear();
    pending_.reset();
    deferred_terminal_.reset();
    next_token_ = 1;
    irq_pending_ = false;
    reset_requested_ = false;
    halted_ = false;
    debug_step_active_ = false;
}

std::expected<void, api_error> device::submit(system_address descriptor_address) {
    if (phase_ != phase::idle || pending_) {
        return std::unexpected(api_error::invalid_state);
    }
    descriptor_address_ = descriptor_address;
    descriptor_ = {};
    fetched_program_.clear();
    fetched_arguments_.clear();
    deferred_terminal_.reset();
    fault_ = architectural_fault::none;
    irq_pending_ = false;
    state_ = lifecycle_state::loading;
    phase_ = phase::descriptor;
    return {};
}

std::expected<void, api_error> device::soft_reset() {
    if (phase_ == phase::idle) {
        reset();
        return {};
    }
    if (phase_ == phase::resetting || reset_requested_) {
        return std::unexpected(api_error::invalid_state);
    }
    reset_requested_ = true;
    state_ = lifecycle_state::resetting;
    phase_ = phase::resetting;
    if (!pending_) {
        finish_reset();
    }
    return {};
}

std::expected<void, api_error> device::halt() {
    if (phase_ != phase::execute || pending_ || state_ != lifecycle_state::running) {
        return std::unexpected(api_error::invalid_state);
    }
    halted_ = true;
    state_ = lifecycle_state::halted;
    return {};
}

std::expected<void, api_error> device::resume() {
    if (phase_ != phase::execute || !halted_ || pending_) {
        return std::unexpected(api_error::invalid_state);
    }
    halted_ = false;
    debug_step_active_ = false;
    state_ = lifecycle_state::running;
    return {};
}

std::expected<void, api_error> device::debug_step() {
    if (phase_ != phase::execute || !halted_ || pending_) {
        return std::unexpected(api_error::invalid_state);
    }
    halted_ = false;
    debug_step_active_ = true;
    state_ = lifecycle_state::running;
    return {};
}

std::expected<void, api_error> device::clear_terminal() {
    if (phase_ != phase::terminal ||
        (state_ != lifecycle_state::done && state_ != lifecycle_state::fault)) {
        return std::unexpected(api_error::invalid_state);
    }
    reset();
    return {};
}

void device::finish_reset() {
    reset();
}

pending_operation device::make_pending(operation value, instruction_address pc) {
    return pending_operation{
        .token = operation_token{next_token_++},
        .pc = pc,
        .value = std::move(value),
    };
}

void device::enter_fault(architectural_fault fault) {
    fault_ = fault;
    state_ = lifecycle_state::fault;
    deferred_terminal_ = terminal_event{
        lifecycle_state::fault,
        fault,
        instruction_address{program_.pc()},
        program_.retired(),
    };
    phase_ = descriptor_.completion_addr != 0 ? phase::completion : phase::terminal;
}

bool device::validate_descriptor_shape(const holon_npu_program_desc_t& desc) {
    if (desc.size_bytes != HOLON_NPU_PROGRAM_DESC_SIZE || !descriptor_reserved_zero(desc) ||
        (desc.flags & ~HOLON_NPU_PROGRAM_FLAG_VALID_MASK) != 0) {
        enter_fault(architectural_fault::invalid_program_descriptor);
        return false;
    }
    if (desc.version != HOLON_NPU_ABI_MAJOR || desc.holon_isa_major != config_.isa_major ||
        desc.holon_isa_minor > config_.isa_minor) {
        enter_fault(architectural_fault::unsupported_abi_or_isa);
        return false;
    }
    if (desc.program_format != HOLON_NPU_PROGRAM_FORMAT_HOLON) {
        enter_fault(architectural_fault::unsupported_program_format);
        return false;
    }
    if ((desc.required_caps & ~config_.implemented_caps) != 0) {
        enter_fault(architectural_fault::unsupported_capability);
        return false;
    }
    if ((desc.required_op_classes & ~config_.implemented_op_classes) != 0) {
        enter_fault(architectural_fault::unsupported_operation_class);
        return false;
    }
    if (!range_fits_u64(desc.code_addr, desc.code_size_bytes) ||
        !range_fits_u64(desc.arg_addr, desc.arg_size_bytes) ||
        (desc.completion_addr != 0 &&
         !range_fits_u64(desc.completion_addr, HOLON_NPU_COMPLETION_RECORD_SIZE))) {
        enter_fault(architectural_fault::invalid_program_descriptor);
        return false;
    }
    if (!aligned(desc.code_addr, HOLON_NPU_PROGRAM_IMAGE_ALIGN) ||
        !aligned(desc.code_size_bytes, HOLON_NPU_PROGRAM_IMAGE_ALIGN) ||
        !aligned(desc.entry_pc, HOLON_NPU_ISA_INSTRUCTION_BYTES) ||
        !aligned(desc.arg_addr, HOLON_NPU_PROGRAM_ARGUMENT_ALIGN) ||
        !aligned(desc.arg_size_bytes, HOLON_NPU_PROGRAM_ARGUMENT_ALIGN) ||
        (desc.completion_addr != 0 &&
         !aligned(desc.completion_addr, HOLON_NPU_PROGRAM_COMPLETION_ALIGN))) {
        enter_fault(architectural_fault::alignment);
        return false;
    }
    if (desc.code_size_bytes == 0 || desc.program_mem_bytes < desc.code_size_bytes ||
        desc.local_mem_bytes < desc.arg_size_bytes ||
        desc.program_mem_bytes > HOLON_NPU_PROGRAM_MEM_MAX_BYTES ||
        desc.local_mem_bytes > program_.scratchpad_.size() ||
        desc.stack_bytes > HOLON_NPU_PROGRAM_STACK_MAX_BYTES ||
        static_cast<std::uint64_t>(desc.arg_size_bytes) + desc.stack_bytes >
            desc.local_mem_bytes ||
        desc.entry_pc >= desc.code_size_bytes) {
        enter_fault(architectural_fault::local_memory_bounds);
        return false;
    }
    return true;
}

run_result device::validate_and_start(
    const holon_npu_program_desc_t& descriptor,
    std::span<const std::uint32_t> program_words,
    std::span<const std::byte> arguments
) {
    descriptor_ = descriptor;
    fault_ = architectural_fault::none;
    deferred_terminal_.reset();
    if (!validate_descriptor_shape(descriptor)) {
        return {state_, fault_, program_.pc(), program_.retired()};
    }
    if (descriptor.code_size_bytes != program_words.size_bytes() ||
        descriptor.arg_size_bytes != arguments.size()) {
        enter_fault(architectural_fault::local_memory_bounds);
        return {state_, fault_, program_.pc(), program_.retired()};
    }

    program_.reset();
    program_.initialize(
        program_words,
        descriptor.local_mem_bytes,
        instruction_address{descriptor.entry_pc}
    );
    if (!program_.load_arguments(arguments)) {
        enter_fault(architectural_fault::local_memory_bounds);
        return {state_, fault_, program_.pc(), program_.retired()};
    }
    state_ = lifecycle_state::running;
    phase_ = phase::execute;
    return program_.snapshot();
}

run_result device::load_program_descriptor(
    const holon_npu_program_desc_t& descriptor,
    std::span<const std::uint32_t> program_words,
    std::span<const std::byte> arguments
) {
    return validate_and_start(descriptor, program_words, arguments);
}

execution_event device::terminal() {
    const auto event = deferred_terminal_.value_or(terminal_event{
        state_,
        fault_,
        instruction_address{program_.pc()},
        program_.retired(),
    });
    state_ = event.state;
    fault_ = event.fault;
    phase_ = phase::terminal;
    const auto irq_flag = event.state == lifecycle_state::done
        ? HOLON_NPU_PROGRAM_FLAG_IRQ_ON_DONE
        : HOLON_NPU_PROGRAM_FLAG_IRQ_ON_FAULT;
    irq_pending_ = (descriptor_.flags & irq_flag) != 0;
    return event;
}

std::expected<execution_event, api_error> device::advance() {
    if (pending_) {
        return std::unexpected(api_error::operation_pending);
    }
    if (phase_ == phase::idle) {
        return terminal_event{
            lifecycle_state::idle,
            architectural_fault::none,
            instruction_address{},
            0,
        };
    }
    if (phase_ == phase::terminal) {
        return terminal();
    }
    if (halted_) {
        return std::unexpected(api_error::invalid_state);
    }
    if (phase_ == phase::resetting) {
        finish_reset();
        return terminal_event{
            lifecycle_state::idle,
            architectural_fault::none,
            instruction_address{},
            0,
        };
    }

    if (phase_ == phase::descriptor) {
        auto request = make_pending(descriptor_fetch{.address = descriptor_address_});
        pending_ = external_pending{request, std::nullopt};
        return request;
    }
    if (phase_ == phase::code) {
        auto request = make_pending(code_fetch{
            .address = system_address{descriptor_.code_addr},
            .byte_count = descriptor_.code_size_bytes,
        });
        pending_ = external_pending{request, std::nullopt};
        return request;
    }
    if (phase_ == phase::arguments) {
        auto request = make_pending(argument_fetch{
            .address = system_address{descriptor_.arg_addr},
            .byte_count = descriptor_.arg_size_bytes,
        });
        pending_ = external_pending{request, std::nullopt};
        return request;
    }
    if (phase_ == phase::completion) {
        const auto terminal_event = deferred_terminal_.value();
        const holon_npu_completion_record_t record{
            .abi_version = HOLON_NPU_ABI_VERSION_RESET,
            .status = terminal_event.state == lifecycle_state::done
                ? HOLON_NPU_COMPLETION_STATUS_DONE
                : HOLON_NPU_COMPLETION_STATUS_FAULT,
            .fault_code = static_cast<std::uint32_t>(terminal_event.fault),
            .debug_pc = terminal_event.pc.value(),
            .cycle_count = 0,
            .instret = terminal_event.instret,
        };
        completion_record_write write{.address = system_address{descriptor_.completion_addr}};
        std::memcpy(write.payload.data(), &record, sizeof(record));
        auto request = make_pending(std::move(write), terminal_event.pc);
        pending_ = external_pending{request, std::nullopt};
        return request;
    }

    auto program_event = program_.advance();
    if (!program_event) {
        return std::unexpected(program_event.error());
    }
    if (auto* request = std::get_if<pending_operation>(&*program_event)) {
        auto external = make_pending(request->value, request->pc);
        pending_ = external_pending{external, request->token};
        return external;
    }
    if (auto* event = std::get_if<terminal_event>(&*program_event)) {
        deferred_terminal_ = *event;
        state_ = event->state;
        fault_ = event->fault;
        phase_ = descriptor_.completion_addr != 0 ? phase::completion : phase::terminal;
        return phase_ == phase::completion ? advance() : terminal();
    }
    if (debug_step_active_ && std::holds_alternative<retired_event>(*program_event)) {
        debug_step_active_ = false;
        halted_ = true;
        state_ = lifecycle_state::halted;
    }
    return *program_event;
}

std::expected<execution_event, api_error> device::complete(
    operation_token token,
    operation_result result
) {
    if (!pending_) {
        return std::unexpected(api_error::no_pending_operation);
    }
    if (pending_->request.token != token) {
        return std::unexpected(api_error::token_mismatch);
    }

    if (reset_requested_) {
        auto context = std::move(*pending_);
        pending_.reset();
        if (context.program_token) {
            const auto completed = program_.complete(*context.program_token, std::move(result));
            if (!completed && completed.error() != api_error::invalid_completion) {
                return std::unexpected(completed.error());
            }
        }
        finish_reset();
        return terminal_event{
            lifecycle_state::idle,
            architectural_fault::none,
            instruction_address{},
            0,
        };
    }

    if (pending_->program_token) {
        auto completed = program_.complete(*pending_->program_token, std::move(result));
        if (!completed) {
            return std::unexpected(completed.error());
        }
        pending_.reset();
        if (auto* event = std::get_if<terminal_event>(&*completed)) {
            deferred_terminal_ = *event;
            state_ = event->state;
            fault_ = event->fault;
            phase_ = descriptor_.completion_addr != 0 ? phase::completion : phase::terminal;
            return phase_ == phase::completion ? advance() : terminal();
        }
        if (debug_step_active_ && std::holds_alternative<retired_event>(*completed)) {
            debug_step_active_ = false;
            halted_ = true;
            state_ = lifecycle_state::halted;
        }
        return *completed;
    }

    if (const auto* failure = std::get_if<operation_failure>(&result)) {
        pending_.reset();
        enter_fault(
            failure->fault == architectural_fault::none
                ? architectural_fault::dma_request
                : failure->fault
        );
        return phase_ == phase::completion ? advance() : terminal();
    }
    const auto* payload = std::get_if<read_payload>(&result);
    if (phase_ == phase::descriptor) {
        if (payload == nullptr || payload->bytes.size() != HOLON_NPU_PROGRAM_DESC_SIZE) {
            return std::unexpected(api_error::invalid_completion);
        }
        pending_.reset();
        std::memcpy(&descriptor_, payload->bytes.data(), sizeof(descriptor_));
        if (!validate_descriptor_shape(descriptor_)) {
            return phase_ == phase::completion ? advance() : terminal();
        }
        phase_ = phase::code;
        return advance();
    }
    if (phase_ == phase::code) {
        if (payload == nullptr || payload->bytes.size() != descriptor_.code_size_bytes) {
            return std::unexpected(api_error::invalid_completion);
        }
        pending_.reset();
        fetched_program_.resize(payload->bytes.size() / sizeof(std::uint32_t));
        std::memcpy(fetched_program_.data(), payload->bytes.data(), payload->bytes.size());
        if (descriptor_.arg_size_bytes == 0) {
            const auto result = validate_and_start(descriptor_, fetched_program_, {});
            if (result.state == lifecycle_state::fault) {
                return phase_ == phase::completion ? advance() : terminal();
            }
            return advance();
        }
        phase_ = phase::arguments;
        return advance();
    }
    if (phase_ == phase::arguments) {
        if (payload == nullptr || payload->bytes.size() != descriptor_.arg_size_bytes) {
            return std::unexpected(api_error::invalid_completion);
        }
        pending_.reset();
        fetched_arguments_ = payload->bytes;
        const auto start = validate_and_start(descriptor_, fetched_program_, fetched_arguments_);
        if (start.state == lifecycle_state::fault) {
            return phase_ == phase::completion ? advance() : terminal();
        }
        return advance();
    }
    if (phase_ == phase::completion) {
        if (!std::holds_alternative<operation_success>(result)) {
            return std::unexpected(api_error::invalid_completion);
        }
        pending_.reset();
        return terminal();
    }
    return std::unexpected(api_error::invalid_state);
}

direct_runner::direct_runner(
    std::size_t scratchpad_bytes,
    std::size_t max_vl,
    std::size_t system_memory_bytes
) : device_(scratchpad_bytes, max_vl), system_memory_(system_memory_bytes) {}

void direct_runner::reset() {
    device_.reset();
    std::ranges::fill(system_memory_, std::byte{0});
}

void direct_runner::resize_system_memory(std::size_t byte_count) {
    system_memory_.assign(byte_count, std::byte{0});
}

std::expected<void, api_error> direct_runner::submit(system_address descriptor_address) {
    return device_.submit(descriptor_address);
}

void direct_runner::load_program(std::span<const std::uint32_t> words) {
    device_.program().initialize(words, device_.program().scratchpad_.size());
    device_.phase_ = device::phase::execute;
    device_.state_ = lifecycle_state::running;
    device_.fault_ = architectural_fault::none;
    device_.descriptor_ = {};
    device_.pending_.reset();
    device_.deferred_terminal_.reset();
    device_.reset_requested_ = false;
    device_.halted_ = false;
    device_.debug_step_active_ = false;
}

run_result direct_runner::load_program_descriptor(
    const holon_npu_program_desc_t& descriptor,
    std::span<const std::uint32_t> program_words,
    std::span<const std::byte> arguments,
    const loader_config& config
) {
    device_.config_ = config;
    return device_.load_program_descriptor(descriptor, program_words, arguments);
}

bool direct_runner::load_arguments(std::span<const std::byte> bytes, local_address destination) {
    return device_.program().load_arguments(bytes, destination);
}

bool direct_runner::write_i8(local_address destination, std::span<const std::int8_t> values) {
    return device_.program().write_i8(destination, values);
}

std::vector<std::int8_t> direct_runner::read_i8(local_address source, std::size_t count) const {
    return device_.program().read_i8(source, count);
}

bool direct_runner::write_i16(local_address destination, std::span<const std::int16_t> values) {
    return device_.program().write_i16(destination, values);
}

std::vector<std::int16_t> direct_runner::read_i16(local_address source, std::size_t count) const {
    return device_.program().read_i16(source, count);
}

bool direct_runner::write_i32(local_address destination, std::span<const std::int32_t> values) {
    return device_.program().write_i32(destination, values);
}

std::vector<std::int32_t> direct_runner::read_i32(local_address source, std::size_t count) const {
    return device_.program().read_i32(source, count);
}

bool direct_runner::system_range_ok(system_address address, std::size_t byte_count) const {
    const auto offset = address.value();
    return offset <= system_memory_.size() && byte_count <= system_memory_.size() - offset;
}

bool direct_runner::write_system_i32(
    system_address destination,
    std::span<const std::int32_t> values
) {
    if (!system_range_ok(destination, values.size_bytes())) {
        return false;
    }
    std::memcpy(system_memory_.data() + destination.value(), values.data(), values.size_bytes());
    return true;
}

std::vector<std::int32_t> direct_runner::read_system_i32(
    system_address source,
    std::size_t count
) const {
    std::vector<std::int32_t> values(count);
    if (!system_range_ok(source, values.size() * sizeof(std::int32_t))) {
        std::ranges::fill(values, 0);
        return values;
    }
    std::memcpy(values.data(), system_memory_.data() + source.value(), values.size() * sizeof(std::int32_t));
    return values;
}

bool direct_runner::write_system_bytes(
    system_address destination,
    std::span<const std::byte> bytes
) {
    if (!system_range_ok(destination, bytes.size())) {
        return false;
    }
    std::ranges::copy(bytes, system_memory_.begin() + destination.value());
    return true;
}

std::vector<std::byte> direct_runner::read_system_bytes(
    system_address source,
    std::size_t byte_count
) const {
    if (!system_range_ok(source, byte_count)) {
        return {};
    }
    const auto first = system_memory_.begin() + source.value();
    return {first, first + byte_count};
}

bool direct_runner::issue_dma_load(
    system_address source,
    local_address destination,
    std::uint32_t byte_count
) {
    if (!system_range_ok(source, byte_count) ||
        !device_.program().local_range_ok(destination, byte_count)) {
        device_.program().raise_fault(
            system_range_ok(source, byte_count)
                ? architectural_fault::local_memory_bounds
                : architectural_fault::dma_request
        );
        return false;
    }
    const auto first = system_memory_.begin() + source.value();
    const std::vector<std::byte> bytes(first, first + byte_count);
    device_.program().write_local_bytes(destination, bytes);
    device_.program().dma_events_.push_back(dma_event{
        .sequence = device_.program().next_dma_sequence_++,
        .direction = dma_direction::system_to_local,
        .system_byte_offset = source,
        .local_byte_offset = destination,
        .byte_count = byte_count,
    });
    return true;
}

bool direct_runner::issue_dma_store(
    local_address source,
    system_address destination,
    std::uint32_t byte_count
) {
    if (!device_.program().local_range_ok(source, byte_count) ||
        !system_range_ok(destination, byte_count)) {
        device_.program().raise_fault(
            device_.program().local_range_ok(source, byte_count)
                ? architectural_fault::dma_request
                : architectural_fault::local_memory_bounds
        );
        return false;
    }
    const auto bytes = device_.program().read_local_bytes(source, byte_count);
    std::ranges::copy(bytes, system_memory_.begin() + destination.value());
    device_.program().dma_events_.push_back(dma_event{
        .sequence = device_.program().next_dma_sequence_++,
        .direction = dma_direction::local_to_system,
        .system_byte_offset = destination,
        .local_byte_offset = source,
        .byte_count = byte_count,
    });
    return true;
}

bool direct_runner::issue_matrix_gemm_i8_i32(const matrix_gemm_i8_i32_op& op) {
    return device_.program().issue_matrix_gemm_i8_i32(op);
}

operation_result direct_runner::service(const pending_operation& request) {
    return service_operation(request.value, system_memory_view{{}, system_memory_});
}

run_result direct_runner::snapshot() const {
    return {
        device_.state(),
        device_.fault(),
        device_.program().pc(),
        device_.program().retired(),
    };
}

run_result direct_runner::step() {
    auto event = device_.advance();
    while (event) {
        const auto* request = std::get_if<pending_operation>(&*event);
        if (request == nullptr) {
            return snapshot();
        }
        event = device_.complete(request->token, service(*request));
    }
    device_.enter_fault(architectural_fault::explicit_program_fault);
    return snapshot();
}

run_result direct_runner::run(std::uint64_t max_instructions) {
    auto result = snapshot();
    for (std::uint64_t count = 0; count < max_instructions; ++count) {
        result = step();
        if (result.state == lifecycle_state::done || result.state == lifecycle_state::fault) {
            return result;
        }
    }
    device_.enter_fault(architectural_fault::explicit_program_fault);
    return snapshot();
}

}  // namespace holon_npu::semantic
