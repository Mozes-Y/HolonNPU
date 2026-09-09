#include "holon_npu_runtime.hpp"

#include <stdexcept>

namespace holon_npu::runtime {
using namespace semantic::instruction;
namespace {
std::uint32_t pattern(scalar_opcode op) {
    return std::ranges::find(scalar_patterns, op, &scalar_pattern::opcode)->value;
}
void check_register(scalar_register r) {
    if (r.value() >= register_count) throw std::invalid_argument("scalar register out of range");
}
}

program_builder& program_builder::emit(scalar_word word) {
    for (unsigned i = 0; i < scalar_bytes; ++i) bytes_.push_back(static_cast<std::byte>(word.bits >> (8 * i)));
    return *this;
}
program_builder& program_builder::emit(holon_word word) {
    for (unsigned i = 0; i < holon_bytes; ++i) bytes_.push_back(static_cast<std::byte>(word.bits >> (8 * i)));
    return *this;
}
program_builder& program_builder::npu(npu_opcode opcode, std::initializer_list<named_operand> operands) {
    const auto word = encode_holon(opcode, operands);
    if (!word) throw std::invalid_argument("invalid Holon instruction operands");
    return emit(*word);
}
program_builder& program_builder::li(scalar_register rd, std::uint32_t value) {
    check_register(rd);
    emit(scalar_word{((value + 0x800u) & 0xfffff000u) | (std::uint32_t{rd.value()} << rd_shift) | pattern(scalar_opcode::LUI)});
    return addi(rd, rd, std::bit_cast<std::int32_t>((value & 4095u) ^ 2048u) - 2048);
}
program_builder& program_builder::addi(scalar_register rd, scalar_register rs, std::int32_t value) {
    check_register(rd); check_register(rs);
    if (value < -2048 || value > 2047) throw std::invalid_argument("ADDI immediate out of range");
    return emit(scalar_word{((static_cast<std::uint32_t>(value) & 4095u) << 20)
        | (std::uint32_t{rs.value()} << rs1_shift) | (std::uint32_t{rd.value()} << rd_shift) | pattern(scalar_opcode::ADDI)});
}

} // namespace holon_npu::runtime
