#include "holon_npu_scalar.hpp"

#include <array>
#include <bitset>
#include <format>
#include <iostream>
#include <random>
#include <source_location>
#include <stdexcept>
#include <utility>

namespace {
using namespace holon_npu::semantic;
using namespace holon_npu::semantic::instruction;
using namespace holon_npu::semantic::scalar;
using enum scalar_opcode;

std::bitset<scalar_patterns.size()> observed;

void require(bool ok, std::string_view message,
             std::source_location location = std::source_location::current()) {
    if (!ok) throw std::runtime_error(std::format("{}:{}: {}", location.file_name(), location.line(), message));
}

scalar_word encode(scalar_opcode op, unsigned rd = 3, unsigned rs1 = 1, unsigned rs2 = 2,
                   std::int32_t immediate = 0) {
    const auto pattern = std::ranges::find(scalar_patterns, op, &scalar_pattern::opcode);
    require(pattern != scalar_patterns.end(), "test opcode exists");
    const auto imm = static_cast<std::uint32_t>(immediate);
    auto bits = pattern->value;
    switch (pattern->format) {
    case scalar_format::r: bits |= rd << 7 | rs1 << 15 | rs2 << 20; break;
    case scalar_format::i: bits |= rd << 7 | rs1 << 15 | (imm & 4095) << 20; break;
    case scalar_format::shift: bits |= rd << 7 | rs1 << 15 | (imm & 31) << 20; break;
    case scalar_format::s:
        bits |= (imm & 31) << 7 | rs1 << 15 | rs2 << 20 | ((imm >> 5) & 127) << 25;
        break;
    case scalar_format::b:
        bits |= ((imm >> 11) & 1) << 7 | ((imm >> 1) & 15) << 8 | rs1 << 15 | rs2 << 20
            | ((imm >> 5) & 63) << 25 | ((imm >> 12) & 1) << 31;
        break;
    case scalar_format::j:
        bits |= rd << 7 | (imm & 0xFF000) | ((imm >> 11) & 1) << 20
            | ((imm >> 1) & 1023) << 21 | ((imm >> 20) & 1) << 31;
        break;
    case scalar_format::u: bits |= rd << 7 | (imm & 0xFFFFF000); break;
    case scalar_format::csr: case scalar_format::csr_immediate:
        bits |= rd << 7 | rs1 << 15 | (imm & 4095) << 20;
        break;
    case scalar_format::fence: bits |= imm; break;
    case scalar_format::system: break;
    }
    return {bits};
}

step evaluated(scalar_word word, operands values = {}, std::uint32_t pc = 0x1000) {
    const auto result = evaluate(word, instruction_address{pc}, values);
    require(result.has_value(), std::format("unexpected trap: word={:08x} PC={:08x}", word.bits, pc));
    require(result->pc.value() == pc, "effect retains issuer PC");
    return *result;
}

void mark(scalar_opcode op) { observed.set(std::to_underlying(op)); }

std::uint32_t written(const step& effect) {
    const auto* result = std::get_if<register_result>(&effect.value);
    require(result && result->write.has_value(), "expected scalar destination update");
    require(result->write->destination.value() == 3, "destination identity");
    return result->write->value;
}

void expect_trap(scalar_word word, operands values, std::uint32_t pc,
                 scalar_trap_cause cause, std::uint32_t tval) {
    const auto effect = evaluate(word, instruction_address{pc}, values);
    require(!effect && effect.error().cause == cause && effect.error().pc.value() == pc &&
            effect.error().value == tval, "precise trap cause/PC/value; no success effect");
}

// The reference uses signed magnitude and partial products, not the implementation's signed product/divide.
std::uint32_t high_product(std::uint32_t a, std::uint32_t b, bool sa, bool sb) {
    const auto lo = std::uint64_t{a & 65535} * (b & 65535);
    const auto cross = (lo >> 16) + std::uint64_t{a >> 16} * (b & 65535)
        + std::uint64_t{a & 65535} * (b >> 16);
    auto hi = static_cast<std::uint32_t>(std::uint64_t{a >> 16} * (b >> 16) + (cross >> 16));
    if (sa && (a >> 31)) hi -= b;
    if (sb && (b >> 31)) hi -= a;
    return hi;
}

std::uint32_t signed_division(std::uint32_t a, std::uint32_t b, bool remainder) {
    if (b == 0) return remainder ? a : 0xFFFFFFFF;
    const auto magnitude_a = a >> 31 ? 0U - a : a;
    const auto magnitude_b = b >> 31 ? 0U - b : b;
    const auto result = remainder ? magnitude_a % magnitude_b : magnitude_a / magnitude_b;
    const auto negative = remainder ? a >> 31 : (a ^ b) >> 31;
    return negative ? 0U - result : result;
}

std::uint32_t arithmetic_reference(scalar_opcode op, std::uint32_t a, std::uint32_t b,
                                  std::int32_t immediate, std::uint32_t pc) {
    const auto imm = static_cast<std::uint32_t>(immediate);
    const auto signed_less = [](std::uint32_t l, std::uint32_t r) {
        return (l ^ 0x80000000U) < (r ^ 0x80000000U);
    };
    const auto shift_signed = [](std::uint32_t x, unsigned amount) {
        return amount == 0 ? x : (x >> amount) | (x >> 31 ? 0xFFFFFFFFU << (32 - amount) : 0);
    };
    switch (op) {
    case LUI: return imm;
    case AUIPC: return static_cast<std::uint32_t>(std::uint64_t{pc} + imm);
    case ADDI: return static_cast<std::uint32_t>(std::uint64_t{a} + imm);
    case SLTI: return signed_less(a, imm);
    case SLTIU: return a < imm;
    case XORI: return a ^ imm;
    case ORI: return a | imm;
    case ANDI: return a & imm;
    case SLLI: return static_cast<std::uint32_t>(std::uint64_t{a} << imm);
    case SRLI: return a >> imm;
    case SRAI: return shift_signed(a, imm);
    case ADD: return static_cast<std::uint32_t>(std::uint64_t{a} + b);
    case SUB: return static_cast<std::uint32_t>(std::uint64_t{a} + (std::uint64_t{1} << 32) - b);
    case SLL: return static_cast<std::uint32_t>(std::uint64_t{a} << (b % 32));
    case SLT: return signed_less(a, b);
    case SLTU: return a < b;
    case XOR: return a ^ b;
    case SRL: return a >> (b % 32);
    case SRA: return shift_signed(a, b % 32);
    case OR: return a | b;
    case AND: return a & b;
    case MUL: return static_cast<std::uint32_t>(std::uint64_t{a} * b);
    case MULH: return high_product(a, b, true, true);
    case MULHSU: return high_product(a, b, true, false);
    case MULHU: return high_product(a, b, false, false);
    case DIV: return signed_division(a, b, false);
    case DIVU: return b ? a / b : 0xFFFFFFFF;
    case REM: return signed_division(a, b, true);
    case REMU: return b ? a % b : a;
    default: throw std::runtime_error("missing arithmetic reference");
    }
}

void arithmetic() {
    constexpr std::array ops{LUI, AUIPC, ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI,
        ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU};
    constexpr std::array<std::uint32_t, 12> edges{0, 1, 2, 31, 32, 0x7FFFFFFF, 0x80000000,
        0xFFFFFFFF, 0x80000001, 0x7FFFFFFE, 0x55555555, 0xAAAAAAAA};
    std::mt19937 random{0x52563332};
    std::size_t count = 0;
    for (auto op : ops) {
        const auto check = [&](std::uint32_t a, std::uint32_t b, std::int32_t raw) {
            const auto word = encode(op, 3, 1, 2, raw);
            const auto immediate = decode_scalar(word)->immediate;
            const auto pc = static_cast<std::uint32_t>(random()) & ~3U;
            const auto effect = evaluated(word, {a, b}, pc);
            const auto expected = arithmetic_reference(op, a, b, immediate, pc);
            require(written(effect) == expected, std::format(
                "ALU op={} a={:08x} b={:08x} imm={} pc={:08x} expected={:08x}",
                disassemble(*decode_scalar(word)), a, b, immediate, pc, expected));
            require(effect.next_pc.value() == pc + 4, "RV32 sequential PC wraps at XLEN");
            ++count;
        };
        for (auto a : edges) for (auto b : edges) check(a, b, std::bit_cast<std::int32_t>(b));
        for (unsigned i = 0; i < 4096; ++i) {
            const auto a = static_cast<std::uint32_t>(random());
            const auto b = static_cast<std::uint32_t>(random());
            check(a, b, std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(random())));
        }
        mark(op);
    }
    for (auto reg : {0U, 1U, 31U}) {
        const auto e = evaluated(encode(ADD, reg, reg, reg), {5, 5});
        const auto update = std::get<register_result>(e.value).write;
        require(reg ? update && update->destination.value() == reg && update->value == 10 : !update,
                "source/destination aliases and x0");
    }
    require(written(evaluated(encode(ADD, 3, 0, 0), {7, 9})) == 0, "x0 ignores stale captured input");
    std::cout << std::format("scalar arithmetic: {} scoreboard cases, seed=0x52563332 PASS\n", count);
}

void control_flow() {
    constexpr std::array ops{BEQ, BNE, BLT, BGE, BLTU, BGEU};
    for (auto op : ops) for (auto a : {0U, 1U, 0x80000000U, 0xFFFFFFFFU})
        for (auto b : {0U, 1U, 0x7FFFFFFFU, 0xFFFFFFFFU}) {
            const auto less = (a ^ 0x80000000U) < (b ^ 0x80000000U);
            const auto taken = op == BEQ ? a == b : op == BNE ? a != b : op == BLT ? less
                : op == BGE ? !less : op == BLTU ? a < b : a >= b;
            for (auto offset : {-4096, -4, 0, 4, 4092}) {
                const auto e = evaluated(encode(op, 0, 1, 2, offset), {a, b});
                require(!std::get<register_result>(e.value).write, "branch never writes rd");
                require(e.next_pc.value() == (taken ? 0x1000U + offset : 0x1004U), "branch predicate/target");
            }
            const auto misaligned = encode(op, 0, 1, 2, 2);
            if (taken) expect_trap(misaligned, {a, b}, 0x1000,
                                  scalar_trap_cause::instruction_address_misaligned, 0x1002);
            else require(evaluated(misaligned, {a, b}).next_pc.value() == 0x1004, "unused target cannot trap");
            mark(op);
        }
    for (auto offset : {-1048576, -4, 0, 4, 1048572}) {
        const auto e = evaluated(encode(JAL, 3, 0, 0, offset));
        require(written(e) == 0x1004 && e.next_pc.value() == 0x1000U + offset, "JAL link and target");
    }
    expect_trap(encode(JAL, 3, 0, 0, 2), {}, 0x1000,
                scalar_trap_cause::instruction_address_misaligned, 0x1002);
    const auto indirect = evaluated(encode(JALR, 3, 3, 0, -4), {0x80000005, 0});
    require(written(indirect) == 0x1004 && indirect.next_pc.value() == 0x80000000,
            "JALR uses pre-write source, clears bit zero and does not perform target fetch");
    require(evaluated(encode(JALR, 0, 0, 0), {0xBAD, 0}).next_pc.value() == 0, "JALR x0 source");
    expect_trap(encode(JALR), {3, 0}, 0x1000, scalar_trap_cause::instruction_address_misaligned, 2);
    require(evaluated(encode(ADDI, 3, 0, 0), {}, 0xFFFFFFFC).next_pc.value() == 0, "sequential PC wrap");
    mark(JAL); mark(JALR);
    std::cout << "scalar control: all branch predicates, alignment, aliases, wrap and precise PC PASS\n";
}

void memory() {
    for (auto op : {LB, LBU, LH, LHU, LW, SB, SH, SW}) {
        const bool store = op == SB || op == SH || op == SW;
        const unsigned width = op == SB || op == LB || op == LBU ? 1 : op == SW || op == LW ? 4 : 2;
        for (auto base : {0U, 0x10000000U, 0x80000000U, 0xFFFFFFFCU}) {
            const auto word = encode(op, store ? 0 : 3, 1, 2, 0);
            for (unsigned low = 0; low < 4; ++low) {
                const auto address = base + low;
                if (low % width) {
                    expect_trap(word, {address, 0xFEDCBA98}, 0x1000, store
                        ? scalar_trap_cause::store_address_misaligned : scalar_trap_cause::load_address_misaligned,
                        address);
                    continue;
                }
                const auto e = evaluated(word, {address, 0xFEDCBA98});
                require(e.next_pc.value() == 0x1004, "memory continuation PC; not retired by evaluation");
                if (store) {
                    const auto& request = std::get<store_request>(e.value);
                    require(request.address.value() == address && std::to_underlying(request.width) == width,
                            "store physical address/width");
                    constexpr std::array bytes{std::byte{0x98}, std::byte{0xBA}, std::byte{0xDC}, std::byte{0xFE}};
                    for (unsigned i = 0; i < 4; ++i)
                        require(request.payload[i] == (i < width ? bytes[i] : std::byte{0}), "captured LE store");
                } else {
                    const auto& request = std::get<load_request>(e.value);
                    require(request.address.value() == address && std::to_underlying(request.width) == width,
                            "load physical address/width");
                    require(request.destination.value() == 3, "load destination retained until completion");
                }
            }
        }
        mark(op);
    }
    require(std::get<load_request>(evaluated(encode(LW, 3, 1, 0, 4), {0xFFFFFFFC, 0}).value)
            .address.value() == 0, "RV32 address generation wraps, not host signed overflow");
    require(std::get<load_request>(evaluated(encode(LB, 3, 0, 0, -1), {123, 0}).value)
            .address.value() == 0xFFFFFFFF, "negative offset sign extension and x0 base");

    std::array<std::byte, 4> payload{};
    for (auto op : {LB, LBU, LH, LHU}) {
        const auto request = std::get<load_request>(evaluated(encode(op)).value);
        const auto width = std::to_underlying(request.width);
        const std::uint32_t range = 1U << (8 * width);
        for (std::uint32_t bits = 0; bits < range; ++bits) {
            payload[0] = std::byte{static_cast<std::uint8_t>(bits)};
            payload[1] = std::byte{static_cast<std::uint8_t>(bits >> 8)};
            const auto done = complete_load(request, std::span{payload}.first(width));
            const bool negative = (op == LB || op == LH) && bits >= range / 2;
            const auto expected = negative ? static_cast<std::uint32_t>(std::int64_t{bits} - range) : bits;
            require(done && done->write && done->write->value == expected, "exhaustive signed/unsigned load");
        }
    }
    const auto zero = std::get<load_request>(evaluated(encode(LW, 0)).value);
    require(!complete_load(zero, {}).has_value(), "load-to-x0 cannot skip access/completion validation");
    const auto zero_done = complete_load(zero, payload);
    require(zero_done && !zero_done->write, "successful load-to-x0 has no destination update");
    const auto full = std::get<load_request>(evaluated(encode(LW)).value);
    payload = {std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x92}};
    const auto full_done = complete_load(full, payload);
    require(full_done && full_done->write && full_done->write->value == 0x92345678, "LW bit-preserving load");
    for (std::size_t size = 0; size < 4; ++size) {
        const auto done = complete_load(full, std::span{payload}.first(size));
        require(!done && done.error() == completion_error::payload_size, "short load is API error");
    }
    const std::array<std::byte, 5> oversized{};
    const auto too_big = complete_load(full, oversized);
    require(!too_big && too_big.error() == completion_error::payload_size, "oversized payload");
    for (auto bad : {load_request{{}, static_cast<access_width>(0), extension::zero, {}},
                    load_request{{}, access_width::word, extension::zero, scalar_register{32}},
                    load_request{physical_address{1}, access_width::word, extension::zero, {}},
                    load_request{{}, access_width::word, static_cast<extension>(3), {}}}) {
        const auto done = complete_load(bad, payload);
        require(!done && done.error() == completion_error::invalid_request, "malformed load request");
    }
    std::cout << "scalar memory: unified physical requests, alignment, captured stores, exhaustive byte/halfword loads PASS\n";
}

void privileged_requests() {
    for (auto op : {CSRRW, CSRRS, CSRRC, CSRRWI, CSRRSI, CSRRCI}) {
        const bool replace = op == CSRRW || op == CSRRWI;
        const bool immediate = op == CSRRWI || op == CSRRSI || op == CSRRCI;
        for (auto rd : {0U, 31U}) for (auto source : {0U, 1U, 31U})
            for (auto bits : {0U, 0xFFFFFFFFU}) for (auto address : {0U, 0x340U, 0xFFFU}) {
                const auto e = evaluated(encode(op, rd, source, 0, address), {bits, 0});
                const auto& csr = std::get<csr_request>(e.value);
                require(csr.address.value() == address && csr.destination.value() == rd, "CSR address/destination");
                require(csr.read == (!replace || rd != 0) && csr.write == (replace || source != 0),
                        "CSR enables use operand identity, not value");
                require(csr.source == (immediate ? source : source ? bits : 0), "CSR source/zimm/x0");
                require(csr.action == (replace ? csr_action::replace
                    : op == CSRRS || op == CSRRSI ? csr_action::set : csr_action::clear), "CSR RMW action");
            }
        mark(op);
    }
    for (unsigned mode = 0; mode < 16; ++mode) for (unsigned pred = 0; pred < 16; ++pred)
        for (unsigned succ = 0; succ < 16; ++succ) {
            const auto bits = mode << 28 | pred << 24 | succ << 20 | 31 << 15 | 31 << 7;
            const auto e = evaluated(encode(FENCE, 0, 0, 0, std::bit_cast<std::int32_t>(bits)), {99, 99});
            const auto& request = std::get<fence_request>(e.value);
            require(request.predecessor == pred && request.successor == succ, "FENCE preserves sets, ignores reserved fields");
        }
    mark(FENCE);
    expect_trap(encode(ECALL), {}, 0x1000, scalar_trap_cause::machine_environment_call, 0);
    expect_trap(encode(EBREAK), {}, 0x1000, scalar_trap_cause::breakpoint, 0x1000);
    for (auto op : {MRET, WFI}) {
        require(std::get<machine_request>(evaluated(encode(op)).value) == (op == MRET
            ? machine_request::return_from_trap : machine_request::wait_for_interrupt), "machine operation not retirement");
        mark(op);
    }
    mark(ECALL); mark(EBREAK);
    for (auto bits : {0U, 0xFFFFFFFFU, 0x02001013U, 0x10200073U})
        expect_trap({bits}, {}, 0x1000, scalar_trap_cause::illegal_instruction, bits);
    expect_trap(encode(ADDI), {}, 2, scalar_trap_cause::instruction_address_misaligned, 2);
    std::cout << "scalar privileged requests: CSR read/write matrix, FENCE fields, ECALL/EBREAK/MRET/WFI PASS\n";
}
} // namespace

int main() {
    try {
        arithmetic();
        control_flow();
        memory();
        privileged_requests();
        require(observed.all(), "each selected opcode must have verified effect evidence");
        std::cout << std::format("scalar effects: {}/{} opcodes checked; full machine/CSR/router integration remains next\n",
                                 observed.count(), observed.size());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
