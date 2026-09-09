#pragma once

#include "holon_npu_instruction.hpp"
#include <vector>

namespace holon_npu::runtime {

// Program construction only: byte positions, never implicit instruction indices.
class program_builder {
public:
    program_builder& emit(semantic::instruction::scalar_word word);
    program_builder& emit(semantic::instruction::holon_word word);
    program_builder& npu(semantic::instruction::npu_opcode opcode,
        std::initializer_list<semantic::instruction::named_operand> operands);
    program_builder& li(semantic::instruction::scalar_register rd, std::uint32_t value);
    program_builder& addi(semantic::instruction::scalar_register rd, semantic::instruction::scalar_register rs, std::int32_t value);
    [[nodiscard]] std::span<const std::byte> bytes() const { return bytes_; }
    [[nodiscard]] std::size_t offset() const { return bytes_.size(); }
private:
    std::vector<std::byte> bytes_;
};

} // namespace holon_npu::runtime
