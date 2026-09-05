#include "holon_npu_scalar.hpp"

#include <bit>
#include <utility>

namespace holon_npu::semantic::scalar {
namespace {

register_result result(scalar_register destination, std::uint32_t value) {
    if (destination.value() == 0) return {};
    return {register_write{destination, value}};
}

} // namespace

std::expected<step, trap> evaluate(
    instruction::scalar_word word, instruction_address pc, operands sources) {
    using enum instruction::scalar_opcode;
    using enum scalar_trap_cause;
    const auto fail = [pc](scalar_trap_cause cause, std::uint32_t value) {
        return std::unexpected(trap{cause, pc, value});
    };
    if (pc.value() % instruction::alignment_bytes != 0)
        return fail(instruction_address_misaligned, pc.value());
    const auto decoded = instruction::decode_scalar(word);
    if (!decoded) return fail(illegal_instruction, word.bits);
    const auto& inst = *decoded;
    const auto op = inst.pattern.opcode;
    const auto a = inst.rs1.value() == 0 ? 0U : sources.rs1;
    const auto b = inst.rs2.value() == 0 ? 0U : sources.rs2;
    const auto signed_a = std::bit_cast<std::int32_t>(a);
    const auto signed_b = std::bit_cast<std::int32_t>(b);
    const auto immediate = std::bit_cast<std::uint32_t>(inst.immediate);
    const instruction_address next{pc.value() + instruction::scalar_bytes};
    const auto value = [&](std::uint32_t bits) -> std::expected<step, trap> {
        return step{pc, next, result(inst.rd, bits)};
    };
    const auto jump = [&](std::uint32_t target, bool link) -> std::expected<step, trap> {
        if (target % instruction::alignment_bytes != 0)
            return fail(instruction_address_misaligned, target);
        return step{pc, instruction_address{target}, link ? result(inst.rd, next.value()) : register_result{}};
    };
    const auto branch = [&](bool taken) -> std::expected<step, trap> {
        return taken ? jump(pc.value() + immediate, false) : step{pc, next, register_result{}};
    };

    switch (op) {
    case LUI: return value(immediate);
    case AUIPC: return value(pc.value() + immediate);
    case ADDI: return value(a + immediate);
    case SLTI: return value(signed_a < inst.immediate);
    case SLTIU: return value(a < immediate);
    case XORI: return value(a ^ immediate);
    case ORI: return value(a | immediate);
    case ANDI: return value(a & immediate);
    case SLLI: return value(a << immediate);
    case SRLI: return value(a >> immediate);
    case SRAI: return value(std::bit_cast<std::uint32_t>(signed_a >> immediate));
    case ADD: return value(a + b);
    case SUB: return value(a - b);
    case SLL: return value(a << (b & 31));
    case SLT: return value(signed_a < signed_b);
    case SLTU: return value(a < b);
    case XOR: return value(a ^ b);
    case SRL: return value(a >> (b & 31));
    case SRA: return value(std::bit_cast<std::uint32_t>(signed_a >> (b & 31)));
    case OR: return value(a | b);
    case AND: return value(a & b);
    case MUL: return value(a * b);
    case MULH: {
        const auto product = std::int64_t{signed_a} * signed_b;
        return value(static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(product) >> 32));
    }
    case MULHSU: {
        const auto product = std::int64_t{signed_a} * std::int64_t{b};
        return value(static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(product) >> 32));
    }
    case MULHU: return value(static_cast<std::uint32_t>((std::uint64_t{a} * b) >> 32));
    // Widen before division so INT32_MIN / -1 is defined in the host language.
    case DIV: return value(b == 0 ? 0xFFFFFFFFU : static_cast<std::uint32_t>(std::int64_t{signed_a} / signed_b));
    case DIVU: return value(b == 0 ? 0xFFFFFFFFU : a / b);
    case REM: return value(b == 0 ? a : static_cast<std::uint32_t>(std::int64_t{signed_a} % signed_b));
    case REMU: return value(b == 0 ? a : a % b);
    case JAL: return jump(pc.value() + immediate, true);
    case JALR: return jump((a + immediate) & ~1U, true);
    case BEQ: return branch(a == b);
    case BNE: return branch(a != b);
    case BLT: return branch(signed_a < signed_b);
    case BGE: return branch(signed_a >= signed_b);
    case BLTU: return branch(a < b);
    case BGEU: return branch(a >= b);
    case LB: case LH: case LW: case LBU: case LHU:
    case SB: case SH: case SW: {
        const auto store = op == SB || op == SH || op == SW;
        const auto width = op == LB || op == LBU || op == SB ? access_width::byte
            : op == LH || op == LHU || op == SH ? access_width::halfword : access_width::word;
        const physical_address address{a + immediate};
        if (address.value() % std::to_underlying(width) != 0)
            return fail(store ? store_address_misaligned : load_address_misaligned, address.value());
        if (!store) {
            const auto extend = op == LBU || op == LHU ? extension::zero : extension::sign;
            return step{pc, next, load_request{address, width, extend, inst.rd}};
        }
        store_request request{address, width, {}};
        for (unsigned i = 0; i < std::to_underlying(width); ++i)
            request.payload[i] = std::byte{static_cast<std::uint8_t>(b >> (8 * i))};
        return step{pc, next, request};
    }
    case FENCE: return step{pc, next, fence_request{inst.predecessor, inst.successor}};
    case CSRRW: case CSRRS: case CSRRC: case CSRRWI: case CSRRSI: case CSRRCI: {
        const auto replace = op == CSRRW || op == CSRRWI;
        const auto action = replace ? csr_action::replace
            : op == CSRRS || op == CSRRSI ? csr_action::set : csr_action::clear;
        const auto source = inst.pattern.format == instruction::scalar_format::csr_immediate
            ? immediate : a;
        return step{pc, next, csr_request{csr_address{inst.csr_address}, inst.rd, action, source,
            !replace || inst.rd.value() != 0, replace || inst.rs1.value() != 0}};
    }
    case ECALL: return fail(machine_environment_call, 0);
    case EBREAK: return fail(breakpoint, pc.value());
    case MRET: return step{pc, next, machine_request::return_from_trap};
    case WFI: return step{pc, next, machine_request::wait_for_interrupt};
    }
    return fail(illegal_instruction, word.bits);
}

std::expected<register_result, completion_error> complete_load(
    const load_request& request, std::span<const std::byte> payload) {
    if ((request.width != access_width::byte && request.width != access_width::halfword &&
         request.width != access_width::word) || request.destination.value() >= instruction::register_count ||
        (request.extend != extension::zero && request.extend != extension::sign) ||
        request.address.value() % std::to_underlying(request.width) != 0)
        return std::unexpected(completion_error::invalid_request);
    if (payload.size() != std::to_underlying(request.width))
        return std::unexpected(completion_error::payload_size);
    std::uint32_t bits = 0;
    for (unsigned i = 0; i < payload.size(); ++i)
        bits |= std::to_integer<std::uint32_t>(payload[i]) << (8 * i);
    if (request.extend == extension::sign) {
        const auto sign = 1U << (8 * payload.size() - 1);
        bits = (bits ^ sign) - sign;
    }
    return result(request.destination, bits);
}

} // namespace holon_npu::semantic::scalar
