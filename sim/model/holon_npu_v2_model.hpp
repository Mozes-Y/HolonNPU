#pragma once

#include "holon_npu_program.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace holon_npu::v2::model {

enum class lifecycle_state : std::uint8_t {
    idle,
    running,
    done,
    fault,
};

enum class model_error : std::uint32_t {
    none = HOLON_NPU_V2_FAULT_NONE,
    invalid_program_descriptor = HOLON_NPU_V2_FAULT_INVALID_PROGRAM_DESCRIPTOR,
    unsupported_abi_or_isa = HOLON_NPU_V2_FAULT_UNSUPPORTED_ABI_OR_ISA,
    unsupported_program_format = HOLON_NPU_V2_FAULT_UNSUPPORTED_PROGRAM_FORMAT,
    unsupported_capability = HOLON_NPU_V2_FAULT_UNSUPPORTED_CAPABILITY,
    unsupported_operation_class = HOLON_NPU_V2_FAULT_UNSUPPORTED_OPERATION_CLASS,
    alignment = HOLON_NPU_V2_FAULT_ALIGNMENT,
    local_memory_bounds = HOLON_NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS,
    illegal_instruction = HOLON_NPU_V2_FAULT_ILLEGAL_INSTRUCTION,
    vector_config = HOLON_NPU_V2_FAULT_VECTOR_CONFIG,
    matrix_issue = HOLON_NPU_V2_FAULT_MATRIX_ISSUE,
    dma_request = HOLON_NPU_V2_FAULT_DMA_REQUEST,
    explicit_program_fault = HOLON_NPU_V2_FAULT_EXPLICIT_PROGRAM_FAULT,
};

enum class v2_opcode : std::uint8_t {
    vector_config_set_vl = 0x0,
    vector_memory_load_i32 = 0x0,
    vector_memory_store_i32 = 0x1,
    vector_alu_add_i32 = 0x0,
    vector_alu_sub_i32 = 0x1,
    vector_alu_min_i32 = 0x2,
    vector_alu_max_i32 = 0x3,
    vector_alu_eq_i32 = 0x4,
    vector_alu_lt_i32 = 0x5,
    vector_alu_shl_i32 = 0x6,
    vector_alu_srl_i32 = 0x7,
    vector_alu_sra_i32 = 0x8,
    system_exit = 0x0,
    system_fault = 0x1,
};

struct decoded_instruction {
    std::uint32_t word = 0;
    holon_npu_isa_class_t isa_class = HOLON_NPU_ISA_ENUM_RESERVED_F;
    std::uint8_t opcode = 0;
    std::uint8_t rd = 0;
    std::uint8_t rs1 = 0;
    std::uint8_t rs2 = 0;
    std::uint16_t imm = 0;
};

struct run_result {
    lifecycle_state state = lifecycle_state::idle;
    model_error fault = model_error::none;
    std::uint32_t pc = 0;
    std::uint64_t retired = 0;
};

struct loader_config {
    std::uint64_t implemented_caps =
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
        HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
        HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
        HOLON_NPU_V2_CAP_IN_ORDER_DMA_QUEUE |
        HOLON_NPU_V2_CAP_MATRIX_MICRO_OP |
        HOLON_NPU_V2_CAP_INTEGER_QUANT_VECTOR;
    std::uint64_t implemented_op_classes =
        HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
        HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
        HOLON_NPU_PROGRAM_OP_CLASS_QUANTIZATION |
        HOLON_NPU_PROGRAM_OP_CLASS_MATRIX |
        HOLON_NPU_PROGRAM_OP_CLASS_DMA |
        HOLON_NPU_PROGRAM_OP_CLASS_CSR_DEBUG |
        HOLON_NPU_PROGRAM_OP_CLASS_SYNC |
        HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM;
    std::uint16_t isa_major = HOLON_NPU_ISA_MAJOR;
    std::uint16_t isa_minor = HOLON_NPU_ISA_MINOR;
};

enum class dma_direction : std::uint8_t {
    system_to_local,
    local_to_system,
};

struct dma_event {
    std::uint64_t sequence = 0;
    dma_direction direction = dma_direction::system_to_local;
    std::uint64_t system_byte_offset = 0;
    std::uint32_t local_byte_offset = 0;
    std::uint32_t byte_count = 0;
};

struct matrix_gemm_i8_i32_op {
    std::uint32_t a_offset = 0;
    std::uint32_t b_offset = 0;
    std::uint32_t c_offset = 0;
    std::uint32_t a_row_stride_bytes = 0;
    std::uint32_t b_row_stride_bytes = 0;
    std::uint32_t c_row_stride_bytes = 0;
    std::uint16_t m = 0;
    std::uint16_t n = 0;
    std::uint16_t k = 0;
    bool clear_accumulator = true;
};

struct matrix_event {
    std::uint64_t sequence = 0;
    std::uint16_t m = 0;
    std::uint16_t n = 0;
    std::uint16_t k = 0;
    std::uint32_t c_offset = 0;
};

std::uint32_t encode_vector_config_set_vl(std::uint16_t vl);
std::uint32_t encode_vector_load_i32(std::uint8_t vd, std::uint16_t local_byte_offset);
std::uint32_t encode_vector_store_i32(std::uint8_t vs, std::uint16_t local_byte_offset);
std::uint32_t encode_vector_add_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_vector_sub_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_vector_min_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_vector_max_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_vector_eq_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_vector_lt_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_vector_shl_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_vector_srl_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_vector_sra_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_system_exit();
std::uint32_t encode_system_fault(model_error fault);

decoded_instruction decode(std::uint32_t word);
std::string class_name(holon_npu_isa_class_t isa_class);
std::string disassemble(const decoded_instruction& inst);

class program_builder {
public:
    program_builder& raw(std::uint32_t word);
    program_builder& set_vl(std::uint16_t vl);
    program_builder& load_i32(std::uint8_t vd, std::uint16_t local_byte_offset);
    program_builder& store_i32(std::uint8_t vs, std::uint16_t local_byte_offset);
    program_builder& add_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
    program_builder& sub_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
    program_builder& min_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
    program_builder& max_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
    program_builder& eq_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
    program_builder& lt_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
    program_builder& shl_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
    program_builder& srl_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
    program_builder& sra_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
    program_builder& exit();
    program_builder& fault(model_error fault);

    [[nodiscard]] const std::vector<std::uint32_t>& words() const { return words_; }
    [[nodiscard]] std::span<const std::uint32_t> span() const { return words_; }
    [[nodiscard]] std::size_t size() const { return words_.size(); }

private:
    std::vector<std::uint32_t> words_;
};

class machine {
public:
    static constexpr std::size_t vector_register_count = 16;
    static constexpr std::size_t default_max_vl = 256;

    explicit machine(
        std::size_t scratchpad_bytes = HOLON_NPU_LOCAL_MEM_MAX_BYTES,
        std::size_t max_vl = default_max_vl
    );

    void reset();
    void resize_system_memory(std::size_t byte_count);
    void load_program(std::span<const std::uint32_t> words);
    run_result load_program_descriptor(
        const holon_npu_program_desc_t& desc,
        std::span<const std::uint32_t> program_words,
        std::span<const std::byte> argument_bytes,
        const loader_config& config = {}
    );
    bool load_arguments(std::span<const std::byte> bytes, std::uint32_t local_byte_offset);
    bool set_predicate(std::span<const std::uint8_t> active_lanes);
    bool write_i8(std::uint32_t local_byte_offset, std::span<const std::int8_t> values);
    std::vector<std::int8_t> read_i8(std::uint32_t local_byte_offset, std::size_t count) const;
    bool write_i32(std::uint32_t local_byte_offset, std::span<const std::int32_t> values);
    std::vector<std::int32_t> read_i32(std::uint32_t local_byte_offset, std::size_t count) const;
    bool write_system_i32(std::uint64_t system_byte_offset, std::span<const std::int32_t> values);
    std::vector<std::int32_t> read_system_i32(std::uint64_t system_byte_offset, std::size_t count) const;
    bool issue_dma_load(std::uint64_t system_byte_offset, std::uint32_t local_byte_offset, std::uint32_t byte_count);
    bool issue_dma_store(std::uint32_t local_byte_offset, std::uint64_t system_byte_offset, std::uint32_t byte_count);
    bool issue_matrix_gemm_i8_i32(const matrix_gemm_i8_i32_op& op);
    [[nodiscard]] const std::vector<dma_event>& dma_events() const { return dma_events_; }
    [[nodiscard]] const std::vector<matrix_event>& matrix_events() const { return matrix_events_; }
    void clear_dma_events();
    void clear_matrix_events();

    run_result step();
    run_result run(std::uint64_t max_instructions);

    [[nodiscard]] lifecycle_state state() const { return state_; }
    [[nodiscard]] model_error fault() const { return fault_; }
    [[nodiscard]] std::uint32_t pc() const { return pc_; }
    [[nodiscard]] std::uint32_t vl() const { return vl_; }
    [[nodiscard]] std::uint64_t retired() const { return retired_; }

private:
    using vector_register = std::vector<std::int32_t>;

    bool local_range_ok(std::uint32_t local_byte_offset, std::size_t byte_count) const;
    bool system_range_ok(std::uint64_t system_byte_offset, std::size_t byte_count) const;
    std::int8_t load_i8(std::uint32_t local_byte_offset) const;
    void store_i8(std::uint32_t local_byte_offset, std::int8_t value);
    std::int32_t load_i32(std::uint32_t local_byte_offset) const;
    void store_i32(std::uint32_t local_byte_offset, std::int32_t value);
    std::int32_t load_system_i32(std::uint64_t system_byte_offset) const;
    void store_system_i32(std::uint64_t system_byte_offset, std::int32_t value);
    void raise_fault(model_error fault);
    bool register_index_ok(std::uint8_t index) const;

    std::vector<std::uint32_t> program_;
    std::vector<std::byte> scratchpad_;
    std::vector<std::byte> system_memory_;
    std::vector<dma_event> dma_events_;
    std::vector<matrix_event> matrix_events_;
    std::array<vector_register, vector_register_count> vector_registers_;
    std::vector<std::uint8_t> predicate_active_;
    lifecycle_state state_ = lifecycle_state::idle;
    model_error fault_ = model_error::none;
    std::uint32_t pc_ = 0;
    std::uint32_t vl_ = 0;
    std::uint64_t retired_ = 0;
    std::uint64_t next_dma_sequence_ = 0;
    std::uint64_t next_matrix_sequence_ = 0;
    std::size_t max_vl_ = default_max_vl;
};

}  // namespace holon_npu::v2::model
