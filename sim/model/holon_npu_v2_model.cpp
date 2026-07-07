#include "holon_npu_v2_model.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>

namespace holon_npu::v2::model {
namespace {

constexpr std::uint32_t k_class_mask = 0xF000'0000U;
constexpr std::uint32_t k_opcode_shift = 24;
constexpr std::uint32_t k_rd_shift = 20;
constexpr std::uint32_t k_rs1_shift = 16;
constexpr std::uint32_t k_rs2_shift = 12;
constexpr std::uint32_t k_field_mask = 0x0FU;
constexpr std::uint32_t k_imm_mask = 0x0FFFU;
constexpr std::uint32_t k_pc_increment = HOLON_NPU_ISA_INSTRUCTION_BYTES;

std::uint32_t encode(
    std::uint32_t isa_class,
    v2_opcode opcode,
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

bool class_matches(std::uint32_t word, std::uint32_t value, std::uint32_t mask) {
    return (word & mask) == value;
}

bool aligned(std::uint64_t value, std::uint64_t alignment) {
    return alignment != 0 && value % alignment == 0;
}

bool descriptor_reserved_zero(const holon_npu_program_desc_t& desc) {
    return desc.reserved_4c == 0 && desc.reserved_50 == 0 && desc.reserved_58 == 0 &&
           desc.reserved_60 == 0 && desc.reserved_68 == 0 && desc.reserved_70 == 0 &&
           desc.reserved_78 == 0;
}

}  // namespace

std::uint32_t encode_vector_config_set_vl(std::uint16_t vl) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_CONFIG, v2_opcode::vector_config_set_vl, 0, 0, 0, vl);
}

std::uint32_t encode_vector_load_i32(std::uint8_t vd, std::uint16_t local_byte_offset) {
    return encode(
        HOLON_NPU_ISA_CLASS_VECTOR_MEMORY,
        v2_opcode::vector_memory_load_i32,
        vd,
        0,
        0,
        local_byte_offset
    );
}

std::uint32_t encode_vector_store_i32(std::uint8_t vs, std::uint16_t local_byte_offset) {
    return encode(
        HOLON_NPU_ISA_CLASS_VECTOR_MEMORY,
        v2_opcode::vector_memory_store_i32,
        vs,
        0,
        0,
        local_byte_offset
    );
}

std::uint32_t encode_vector_add_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, v2_opcode::vector_alu_add_i32, vd, vs1, vs2, 0);
}

std::uint32_t encode_vector_sub_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, v2_opcode::vector_alu_sub_i32, vd, vs1, vs2, 0);
}

std::uint32_t encode_vector_min_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, v2_opcode::vector_alu_min_i32, vd, vs1, vs2, 0);
}

std::uint32_t encode_vector_max_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, v2_opcode::vector_alu_max_i32, vd, vs1, vs2, 0);
}

std::uint32_t encode_system_exit() {
    return encode(HOLON_NPU_ISA_CLASS_SYSTEM, v2_opcode::system_exit, 0, 0, 0, 0);
}

std::uint32_t encode_system_fault(model_error fault) {
    return encode(
        HOLON_NPU_ISA_CLASS_SYSTEM,
        v2_opcode::system_fault,
        0,
        0,
        0,
        static_cast<std::uint16_t>(fault)
    );
}

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
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_RESERVED_D, k_class_mask)) {
        inst.isa_class = HOLON_NPU_ISA_ENUM_RESERVED_D;
    } else if (class_matches(word, HOLON_NPU_ISA_CLASS_RESERVED_E, k_class_mask)) {
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
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_CONFIG &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_config_set_vl)) {
        return std::format("{}.set_vl vl={}", cls, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_MEMORY &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_memory_load_i32)) {
        return std::format("{}.load_i32 v{}, [0x{:03X}]", cls, inst.rd, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_MEMORY &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_memory_store_i32)) {
        return std::format("{}.store_i32 v{}, [0x{:03X}]", cls, inst.rd, inst.imm);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_add_i32)) {
        return std::format("{}.add_i32 v{}, v{}, v{}", cls, inst.rd, inst.rs1, inst.rs2);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_sub_i32)) {
        return std::format("{}.sub_i32 v{}, v{}, v{}", cls, inst.rd, inst.rs1, inst.rs2);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_min_i32)) {
        return std::format("{}.min_i32 v{}, v{}, v{}", cls, inst.rd, inst.rs1, inst.rs2);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_ALU &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_alu_max_i32)) {
        return std::format("{}.max_i32 v{}, v{}, v{}", cls, inst.rd, inst.rs1, inst.rs2);
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYSTEM &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::system_exit)) {
        return "system.exit";
    }
    if (inst.isa_class == HOLON_NPU_ISA_ENUM_SYSTEM &&
        inst.opcode == static_cast<std::uint8_t>(v2_opcode::system_fault)) {
        return std::format("system.fault {}", inst.imm);
    }
    return std::format("{}.unknown opcode=0x{:X} word=0x{:08X}", cls, inst.opcode, inst.word);
}

machine::machine(std::size_t scratchpad_bytes, std::size_t max_vl)
    : scratchpad_(scratchpad_bytes), predicate_active_(max_vl, 1), max_vl_(max_vl) {
    for (auto& reg : vector_registers_) {
        reg.assign(max_vl_, 0);
    }
}

void machine::reset() {
    std::ranges::fill(scratchpad_, std::byte{0});
    for (auto& reg : vector_registers_) {
        std::ranges::fill(reg, 0);
    }
    std::ranges::fill(predicate_active_, 1);
    state_ = lifecycle_state::idle;
    fault_ = model_error::none;
    pc_ = 0;
    vl_ = 0;
    retired_ = 0;
    dma_events_.clear();
    matrix_events_.clear();
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
    dma_events_.clear();
    matrix_events_.clear();
    next_dma_sequence_ = 0;
    next_matrix_sequence_ = 0;
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
        desc.program_mem_bytes > HOLON_NPU_PROGRAM_MEM_MAX_BYTES ||
        desc.local_mem_bytes > scratchpad_.size() ||
        desc.stack_bytes > HOLON_NPU_PROGRAM_STACK_MAX_BYTES ||
        desc.entry_pc >= desc.code_size_bytes) {
        raise_fault(model_error::local_memory_bounds);
        return run_result{state_, fault_, pc_, retired_};
    }

    load_program(program_words);
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

bool machine::set_predicate(std::span<const std::uint8_t> active_lanes) {
    if (active_lanes.size() > predicate_active_.size()) {
        raise_fault(model_error::vector_config);
        return false;
    }
    std::ranges::fill(predicate_active_, 0);
    std::ranges::copy(active_lanes, predicate_active_.begin());
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
    if (op.m == 0 || op.n == 0 || op.k == 0 ||
        op.a_row_stride_bytes < op.k ||
        op.b_row_stride_bytes < op.n ||
        op.c_row_stride_bytes < static_cast<std::uint32_t>(op.n) * sizeof(std::int32_t) ||
        op.c_offset % sizeof(std::int32_t) != 0 ||
        op.c_row_stride_bytes % sizeof(std::int32_t) != 0) {
        raise_fault(model_error::matrix_issue);
        return false;
    }

    const auto a_last = op.a_offset +
                        static_cast<std::uint64_t>(op.m - 1U) * op.a_row_stride_bytes +
                        static_cast<std::uint64_t>(op.k);
    const auto b_last = op.b_offset +
                        static_cast<std::uint64_t>(op.k - 1U) * op.b_row_stride_bytes +
                        static_cast<std::uint64_t>(op.n);
    const auto c_last = op.c_offset +
                        static_cast<std::uint64_t>(op.m - 1U) * op.c_row_stride_bytes +
                        static_cast<std::uint64_t>(op.n) * sizeof(std::int32_t);
    if (a_last > scratchpad_.size() || b_last > scratchpad_.size() || c_last > scratchpad_.size()) {
        raise_fault(model_error::matrix_issue);
        return false;
    }

    for (std::uint16_t row = 0; row < op.m; ++row) {
        for (std::uint16_t col = 0; col < op.n; ++col) {
            const auto c_offset = op.c_offset +
                                  static_cast<std::uint32_t>(row) * op.c_row_stride_bytes +
                                  static_cast<std::uint32_t>(col) * sizeof(std::int32_t);
            auto acc = op.clear_accumulator ? std::int32_t{0} : load_i32(c_offset);
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
            store_i32(c_offset, acc);
        }
    }

    matrix_events_.push_back(matrix_event{
        .sequence = next_matrix_sequence_++,
        .m = op.m,
        .n = op.n,
        .k = op.k,
        .c_offset = op.c_offset,
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
        case HOLON_NPU_ISA_ENUM_VECTOR_CONFIG:
            if (inst.opcode != static_cast<std::uint8_t>(v2_opcode::vector_config_set_vl) ||
                inst.imm == 0 || inst.imm > max_vl_) {
                raise_fault(model_error::vector_config);
                return run_result{state_, fault_, pc_, retired_};
            }
            vl_ = inst.imm;
            pc_ = next_pc;
            ++retired_;
            break;

        case HOLON_NPU_ISA_ENUM_VECTOR_MEMORY:
            if (!register_index_ok(inst.rd) || vl_ == 0) {
                raise_fault(model_error::vector_config);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (!local_range_ok(inst.imm, static_cast<std::size_t>(vl_) * sizeof(std::int32_t))) {
                raise_fault(model_error::local_memory_bounds);
                return run_result{state_, fault_, pc_, retired_};
            }
            if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_memory_load_i32)) {
                for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                    vector_registers_.at(inst.rd).at(lane) =
                        load_i32(inst.imm + lane * sizeof(std::int32_t));
                }
            } else if (inst.opcode == static_cast<std::uint8_t>(v2_opcode::vector_memory_store_i32)) {
                for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                    store_i32(
                        inst.imm + lane * sizeof(std::int32_t),
                        vector_registers_.at(inst.rd).at(lane)
                    );
                }
            } else {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            pc_ = next_pc;
            ++retired_;
            break;

        case HOLON_NPU_ISA_ENUM_VECTOR_ALU:
            if (!register_index_ok(inst.rd) || !register_index_ok(inst.rs1) ||
                !register_index_ok(inst.rs2) || vl_ == 0) {
                raise_fault(model_error::illegal_instruction);
                return run_result{state_, fault_, pc_, retired_};
            }
            for (std::uint32_t lane = 0; lane < vl_; ++lane) {
                if (predicate_active_.at(lane) != 0) {
                    const auto lhs = vector_registers_.at(inst.rs1).at(lane);
                    const auto rhs = vector_registers_.at(inst.rs2).at(lane);
                    switch (static_cast<v2_opcode>(inst.opcode)) {
                        case v2_opcode::vector_alu_add_i32:
                            vector_registers_.at(inst.rd).at(lane) = wrap_add(lhs, rhs);
                            break;
                        case v2_opcode::vector_alu_sub_i32:
                            vector_registers_.at(inst.rd).at(lane) = wrap_sub(lhs, rhs);
                            break;
                        case v2_opcode::vector_alu_min_i32:
                            vector_registers_.at(inst.rd).at(lane) = std::min(lhs, rhs);
                            break;
                        case v2_opcode::vector_alu_max_i32:
                            vector_registers_.at(inst.rd).at(lane) = std::max(lhs, rhs);
                            break;
                        default:
                            raise_fault(model_error::illegal_instruction);
                            return run_result{state_, fault_, pc_, retired_};
                    }
                }
            }
            pc_ = next_pc;
            ++retired_;
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
    return offset <= scratchpad_.size() && byte_count <= scratchpad_.size() - offset;
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

}  // namespace holon_npu::v2::model
