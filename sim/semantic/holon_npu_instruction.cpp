#include "holon_npu_instruction.hpp"

#include <format>
#include <type_traits>
#include <utility>

namespace holon_npu::semantic::instruction {

namespace {
template<class T> constexpr npu_kind operand_kind() {
    if constexpr (std::same_as<T, scalar_register>) return npu_kind::scalar;
    else if constexpr (std::same_as<T, vector_register>) return npu_kind::vector;
    else if constexpr (std::same_as<T, predicate_register>) return npu_kind::predicate;
    else if constexpr (std::same_as<T, tile_register>) return npu_kind::tile;
    else if constexpr (std::same_as<T, tile_view>) return npu_kind::view;
    else if constexpr (std::same_as<T, npu_type>) return npu_kind::element;
    else if constexpr (std::same_as<T, mask_policy>) return npu_kind::policy;
    else if constexpr (std::same_as<T, rounding_mode>) return npu_kind::rounding;
    else if constexpr (std::same_as<T, displacement>) return npu_kind::displacement;
    else if constexpr (std::same_as<T, index_scale>) return npu_kind::scale;
    else if constexpr (std::same_as<T, resource_capacity>) return npu_kind::capability;
}
std::optional<npu_operand> operand(npu_kind kind, std::uint32_t bits, unsigned width) {
    const auto reg = static_cast<std::uint8_t>(bits);
    switch (kind) {
    case npu_kind::scalar: return scalar_register{reg};
    case npu_kind::vector: return vector_register{reg};
    case npu_kind::predicate: return predicate_register{reg};
    case npu_kind::tile: return tile_register{reg};
    case npu_kind::view: return tile_view{reg};
    case npu_kind::element:
        if (bits > std::to_underlying(npu_type::f32)) return std::nullopt;
        return static_cast<npu_type>(bits);
    case npu_kind::policy: return static_cast<mask_policy>(bits);
    case npu_kind::rounding: return static_cast<rounding_mode>(bits);
    case npu_kind::scale: return index_scale{reg};
    case npu_kind::capability: return static_cast<resource_capacity>(bits);
    case npu_kind::displacement: {
        const auto sign = std::uint32_t{1} << (width - 1);
        return displacement{std::bit_cast<std::int32_t>((bits ^ sign) - sign)};
    }
    }
    std::unreachable();
}
}

std::expected<npu_instruction, decode_error> decode_holon(holon_word word) {
    const auto pattern = std::ranges::find_if(npu_patterns, [&](const auto& p) {
        return (word.bits & ~p.variable_mask) == std::to_underlying(p.opcode);
    });
    if (pattern == npu_patterns.end()) return std::unexpected(decode_error::illegal_holon);
    npu_instruction result{*pattern};
    for (unsigned i = 0; i < pattern->count; ++i) {
        const auto& field = pattern->fields[i];
        const auto raw = static_cast<std::uint32_t>((word.bits >> field.shift) & ((std::uint64_t{1} << field.width) - 1));
        if (field.role == npu_role::type && !(pattern->type_mask & (1u << raw)))
            return std::unexpected(decode_error::illegal_holon);
        const auto value = operand(field.kind, raw, field.width);
        if (!value) return std::unexpected(decode_error::illegal_holon);
        result.operands[i] = {field.role, *value};
    }
    return result;
}

std::expected<holon_word, encode_error> encode_holon(npu_opcode opcode, std::span<const named_operand> arguments) {
    const auto pattern = std::ranges::find(npu_patterns, opcode, &npu_pattern::opcode);
    if (pattern == npu_patterns.end()) return std::unexpected(encode_error::unknown_opcode);
    if (arguments.size() != pattern->count) return std::unexpected(encode_error::operand_set);
    holon_word result{std::to_underlying(opcode)};
    for (unsigned i = 0; i < pattern->count; ++i) {
        const auto& field = pattern->fields[i];
        if (std::ranges::count(arguments, field.role, &named_operand::role) != 1)
            return std::unexpected(encode_error::operand_set);
        const auto& arg = std::ranges::find(arguments, field.role, &named_operand::role)->value;
        const auto encoded = std::visit([&]<class T>(const T& value) -> std::expected<std::uint64_t, encode_error> {
            if (operand_kind<T>() != field.kind) return std::unexpected(encode_error::operand_type);
            const std::int64_t raw = [&] {
                if constexpr (std::is_enum_v<T>) return std::int64_t{std::to_underlying(value)};
                else return std::int64_t{value.value()};
            }();
            const auto limit = std::int64_t{1} << field.width;
            if (field.kind == npu_kind::displacement) {
                if (raw < -limit / 2 || raw >= limit / 2) return std::unexpected(encode_error::operand_range);
            } else if (raw < 0 || raw >= limit) return std::unexpected(encode_error::operand_range);
            const auto bits = static_cast<std::uint32_t>(raw & (limit - 1));
            if (field.role == npu_role::type && !(pattern->type_mask & (1u << bits)))
                return std::unexpected(encode_error::operand_range);
            if (!operand(field.kind, bits, field.width)) return std::unexpected(encode_error::operand_range);
            return std::uint64_t{bits} << field.shift;
        }, arg);
        if (!encoded) return std::unexpected(encoded.error());
        result.bits |= *encoded;
    }
    return result;
}

std::string disassemble(const npu_instruction& instruction) {
    std::string result{instruction.pattern.name};
    for (unsigned i = 0; i < instruction.pattern.count; ++i) {
        const auto text = std::visit([]<class T>(const T& value) {
            if constexpr (std::same_as<T, scalar_register>) return std::format("x{}", value.value());
            else if constexpr (std::same_as<T, vector_register>) return std::format("v{}", value.value());
            else if constexpr (std::same_as<T, predicate_register>) return std::format("p{}", value.value());
            else if constexpr (std::same_as<T, tile_register>) return std::format("t{}", value.value());
            else if constexpr (std::same_as<T, tile_view>) return std::format("view{}", value.value());
            else if constexpr (std::same_as<T, npu_type>) return std::string{npu_type_names[std::to_underlying(value)]};
            else if constexpr (std::same_as<T, mask_policy>) return std::string{mask_policy_names[std::to_underlying(value)]};
            else if constexpr (std::same_as<T, rounding_mode>) return std::string{rounding_mode_names[std::to_underlying(value)]};
            else if constexpr (std::same_as<T, resource_capacity>) return std::string{resource_capacity_names[std::to_underlying(value)]};
            else if constexpr (std::is_enum_v<T>) return std::format("{}", std::to_underlying(value));
            else return std::format("{}", value.value());
        }, instruction.operands[i].value);
        result += std::format("{}{}={}", i ? ", " : " ", instruction.pattern.fields[i].name, text);
    }
    return result;
}

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
