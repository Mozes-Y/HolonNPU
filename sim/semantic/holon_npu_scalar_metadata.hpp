// Generated from spec/holon_npu_isa.json by tools/gen_isa.py. Do not edit.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace holon_npu::semantic::instruction {

inline constexpr std::uint32_t alignment_bytes = 0x00000004u;
inline constexpr std::uint32_t scalar_bytes = 0x00000004u;
inline constexpr std::uint32_t holon_bytes = 0x00000008u;
inline constexpr std::uint32_t prefix_mask = 0x00000003u;
inline constexpr std::uint32_t scalar_prefix = 0x00000003u;
inline constexpr std::uint32_t register_count = 0x00000020u;
inline constexpr unsigned rd_shift = 7;
inline constexpr unsigned rs1_shift = 15;
inline constexpr unsigned rs2_shift = 20;

enum class scalar_trap_cause : std::uint8_t {
    instruction_address_misaligned = 0,
    instruction_access_fault = 1,
    illegal_instruction = 2,
    breakpoint = 3,
    load_address_misaligned = 4,
    load_access_fault = 5,
    store_address_misaligned = 6,
    store_access_fault = 7,
    machine_environment_call = 11,
};

enum class scalar_opcode : std::uint8_t {
    LUI,
    AUIPC,
    JAL,
    JALR,
    BEQ,
    BNE,
    BLT,
    BGE,
    BLTU,
    BGEU,
    LB,
    LH,
    LW,
    LBU,
    LHU,
    SB,
    SH,
    SW,
    ADDI,
    SLTI,
    SLTIU,
    XORI,
    ORI,
    ANDI,
    SLLI,
    SRLI,
    SRAI,
    ADD,
    SUB,
    SLL,
    SLT,
    SLTU,
    XOR,
    SRL,
    SRA,
    OR,
    AND,
    FENCE,
    ECALL,
    EBREAK,
    MUL,
    MULH,
    MULHSU,
    MULHU,
    DIV,
    DIVU,
    REM,
    REMU,
    CSRRW,
    CSRRS,
    CSRRC,
    CSRRWI,
    CSRRSI,
    CSRRCI,
    MRET,
    WFI,
};

enum class scalar_format : std::uint8_t {
    b,
    csr,
    csr_immediate,
    fence,
    i,
    j,
    r,
    s,
    shift,
    system,
    u,
};

struct scalar_pattern {
    scalar_opcode opcode;
    scalar_format format;
    std::string_view mnemonic;
    std::uint32_t value;
    std::uint32_t mask;
};

inline constexpr std::array scalar_patterns{
    scalar_pattern{scalar_opcode::LUI, scalar_format::u, "lui", 0x00000037u, 0x0000007Fu},
    scalar_pattern{scalar_opcode::AUIPC, scalar_format::u, "auipc", 0x00000017u, 0x0000007Fu},
    scalar_pattern{scalar_opcode::JAL, scalar_format::j, "jal", 0x0000006Fu, 0x0000007Fu},
    scalar_pattern{scalar_opcode::JALR, scalar_format::i, "jalr", 0x00000067u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::BEQ, scalar_format::b, "beq", 0x00000063u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::BNE, scalar_format::b, "bne", 0x00001063u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::BLT, scalar_format::b, "blt", 0x00004063u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::BGE, scalar_format::b, "bge", 0x00005063u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::BLTU, scalar_format::b, "bltu", 0x00006063u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::BGEU, scalar_format::b, "bgeu", 0x00007063u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::LB, scalar_format::i, "lb", 0x00000003u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::LH, scalar_format::i, "lh", 0x00001003u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::LW, scalar_format::i, "lw", 0x00002003u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::LBU, scalar_format::i, "lbu", 0x00004003u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::LHU, scalar_format::i, "lhu", 0x00005003u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::SB, scalar_format::s, "sb", 0x00000023u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::SH, scalar_format::s, "sh", 0x00001023u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::SW, scalar_format::s, "sw", 0x00002023u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::ADDI, scalar_format::i, "addi", 0x00000013u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::SLTI, scalar_format::i, "slti", 0x00002013u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::SLTIU, scalar_format::i, "sltiu", 0x00003013u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::XORI, scalar_format::i, "xori", 0x00004013u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::ORI, scalar_format::i, "ori", 0x00006013u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::ANDI, scalar_format::i, "andi", 0x00007013u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::SLLI, scalar_format::shift, "slli", 0x00001013u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::SRLI, scalar_format::shift, "srli", 0x00005013u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::SRAI, scalar_format::shift, "srai", 0x40005013u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::ADD, scalar_format::r, "add", 0x00000033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::SUB, scalar_format::r, "sub", 0x40000033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::SLL, scalar_format::r, "sll", 0x00001033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::SLT, scalar_format::r, "slt", 0x00002033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::SLTU, scalar_format::r, "sltu", 0x00003033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::XOR, scalar_format::r, "xor", 0x00004033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::SRL, scalar_format::r, "srl", 0x00005033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::SRA, scalar_format::r, "sra", 0x40005033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::OR, scalar_format::r, "or", 0x00006033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::AND, scalar_format::r, "and", 0x00007033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::FENCE, scalar_format::fence, "fence", 0x0000000Fu, 0x0000707Fu},
    scalar_pattern{scalar_opcode::ECALL, scalar_format::system, "ecall", 0x00000073u, 0xFFFFFFFFu},
    scalar_pattern{scalar_opcode::EBREAK, scalar_format::system, "ebreak", 0x00100073u, 0xFFFFFFFFu},
    scalar_pattern{scalar_opcode::MUL, scalar_format::r, "mul", 0x02000033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::MULH, scalar_format::r, "mulh", 0x02001033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::MULHSU, scalar_format::r, "mulhsu", 0x02002033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::MULHU, scalar_format::r, "mulhu", 0x02003033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::DIV, scalar_format::r, "div", 0x02004033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::DIVU, scalar_format::r, "divu", 0x02005033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::REM, scalar_format::r, "rem", 0x02006033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::REMU, scalar_format::r, "remu", 0x02007033u, 0xFE00707Fu},
    scalar_pattern{scalar_opcode::CSRRW, scalar_format::csr, "csrrw", 0x00001073u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::CSRRS, scalar_format::csr, "csrrs", 0x00002073u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::CSRRC, scalar_format::csr, "csrrc", 0x00003073u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::CSRRWI, scalar_format::csr_immediate, "csrrwi", 0x00005073u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::CSRRSI, scalar_format::csr_immediate, "csrrsi", 0x00006073u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::CSRRCI, scalar_format::csr_immediate, "csrrci", 0x00007073u, 0x0000707Fu},
    scalar_pattern{scalar_opcode::MRET, scalar_format::system, "mret", 0x30200073u, 0xFFFFFFFFu},
    scalar_pattern{scalar_opcode::WFI, scalar_format::system, "wfi", 0x10500073u, 0xFFFFFFFFu},
};

} // namespace holon_npu::semantic::instruction
