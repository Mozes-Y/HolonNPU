#include "holon_npu_instruction.hpp"

#include <format>
#include <utility>

namespace holon_npu::semantic::instruction {

std::string disassemble(const scalar_instruction& instruction) {
    const auto& d = instruction;
    const auto name = d.pattern.mnemonic;
    const auto rd = d.rd.value(), rs1 = d.rs1.value(), rs2 = d.rs2.value();
    switch (d.pattern.format) {
    case scalar_format::r:
        return std::format("{} x{}, x{}, x{}", name, rd, rs1, rs2);
    case scalar_format::i:
        switch (d.pattern.opcode) {
        case scalar_opcode::LB: case scalar_opcode::LH: case scalar_opcode::LW:
        case scalar_opcode::LBU: case scalar_opcode::LHU: case scalar_opcode::JALR:
            return std::format("{} x{}, {}(x{})", name, rd, d.immediate, rs1);
        default:
            return std::format("{} x{}, x{}, {}", name, rd, rs1, d.immediate);
        }
    case scalar_format::shift:
        return std::format("{} x{}, x{}, {}", name, rd, rs1, d.immediate);
    case scalar_format::s:
        return std::format("{} x{}, {}(x{})", name, rs2, d.immediate, rs1);
    case scalar_format::b:
        return std::format("{} x{}, x{}, {}", name, rs1, rs2, d.immediate);
    case scalar_format::u:
        return std::format("{} x{}, 0x{:x}", name, rd,
            static_cast<std::uint32_t>(d.immediate) >> 12);
    case scalar_format::j:
        return std::format("{} x{}, {}", name, rd, d.immediate);
    case scalar_format::csr:
        return std::format("{} x{}, 0x{:03x}, x{}", name, rd, d.csr_address, rs1);
    case scalar_format::csr_immediate:
        return std::format("{} x{}, 0x{:03x}, {}", name, rd, d.csr_address, d.immediate);
    case scalar_format::fence:
        return std::format("{} fm={}, pred=0x{:x}, succ=0x{:x}", name,
            d.fence_mode, d.predecessor, d.successor);
    case scalar_format::system:
        return std::string{name};
    }
    std::unreachable();
}

} // namespace holon_npu::semantic::instruction
