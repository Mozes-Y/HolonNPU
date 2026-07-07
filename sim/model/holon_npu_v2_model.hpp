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
    local_memory_bounds = HOLON_NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS,
    illegal_instruction = HOLON_NPU_V2_FAULT_ILLEGAL_INSTRUCTION,
    vector_config = HOLON_NPU_V2_FAULT_VECTOR_CONFIG,
    explicit_program_fault = HOLON_NPU_V2_FAULT_EXPLICIT_PROGRAM_FAULT,
};

enum class v2_opcode : std::uint8_t {
    vector_config_set_vl = 0x0,
    vector_memory_load_i32 = 0x0,
    vector_memory_store_i32 = 0x1,
    vector_alu_add_i32 = 0x0,
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

std::uint32_t encode_vector_config_set_vl(std::uint16_t vl);
std::uint32_t encode_vector_load_i32(std::uint8_t vd, std::uint16_t local_byte_offset);
std::uint32_t encode_vector_store_i32(std::uint8_t vs, std::uint16_t local_byte_offset);
std::uint32_t encode_vector_add_i32(std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2);
std::uint32_t encode_system_exit();
std::uint32_t encode_system_fault(model_error fault);

decoded_instruction decode(std::uint32_t word);
std::string class_name(holon_npu_isa_class_t isa_class);
std::string disassemble(const decoded_instruction& inst);

class machine {
public:
    static constexpr std::size_t vector_register_count = 16;
    static constexpr std::size_t default_max_vl = 256;

    explicit machine(
        std::size_t scratchpad_bytes = HOLON_NPU_LOCAL_MEM_MAX_BYTES,
        std::size_t max_vl = default_max_vl
    );

    void reset();
    void load_program(std::span<const std::uint32_t> words);
    bool load_arguments(std::span<const std::byte> bytes, std::uint32_t local_byte_offset);
    bool write_i32(std::uint32_t local_byte_offset, std::span<const std::int32_t> values);
    std::vector<std::int32_t> read_i32(std::uint32_t local_byte_offset, std::size_t count) const;

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
    std::int32_t load_i32(std::uint32_t local_byte_offset) const;
    void store_i32(std::uint32_t local_byte_offset, std::int32_t value);
    void raise_fault(model_error fault);
    bool register_index_ok(std::uint8_t index) const;

    std::vector<std::uint32_t> program_;
    std::vector<std::byte> scratchpad_;
    std::array<vector_register, vector_register_count> vector_registers_;
    std::vector<std::uint8_t> predicate_active_;
    lifecycle_state state_ = lifecycle_state::idle;
    model_error fault_ = model_error::none;
    std::uint32_t pc_ = 0;
    std::uint32_t vl_ = 0;
    std::uint64_t retired_ = 0;
    std::size_t max_vl_ = default_max_vl;
};

}  // namespace holon_npu::v2::model
