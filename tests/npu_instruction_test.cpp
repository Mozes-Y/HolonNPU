#include "holon_npu_instruction.hpp"

#include <format>
#include <iostream>
#include <random>
#include <source_location>
#include <stdexcept>
#include <utility>

namespace {
using namespace holon_npu::semantic::instruction;
void require(bool ok, std::string_view message, std::source_location loc = std::source_location::current()) {
    if (!ok) throw std::runtime_error(std::format("NPU operands line {}: {}", loc.line(), message));
}
npu_operand value(const npu_field& field, unsigned bits) {
    const auto reg = static_cast<std::uint8_t>(bits);
    switch (field.kind) {
    case npu_kind::scalar: return scalar_register{reg};
    case npu_kind::vector: return vector_register{reg};
    case npu_kind::predicate: return predicate_register{reg};
    case npu_kind::tile: return tile_register{reg};
    case npu_kind::view: return tile_view{reg};
    case npu_kind::element: return static_cast<npu_type>(bits);
    case npu_kind::policy: return static_cast<mask_policy>(bits);
    case npu_kind::rounding: return static_cast<rounding_mode>(bits);
    case npu_kind::scale: return index_scale{reg};
    case npu_kind::capability: return static_cast<resource_capacity>(bits);
    case npu_kind::displacement:
        return displacement{static_cast<std::int32_t>(bits) - (bits >= (1u << 19) ? (1 << 20) : 0)};
    }
    std::unreachable();
}
void directed() {
    const auto encoded = encode_holon(npu_opcode::VADD, {
        {npu_role::vd, vector_register{31}}, {npu_role::va, vector_register{1}}, {npu_role::vb, vector_register{30}},
        {npu_role::pg, predicate_register{17}}, {npu_role::vl, scalar_register{29}},
        {npu_role::type, npu_type::f32}, {npu_role::policy, mask_policy::zero}});
    const std::uint64_t golden = 4ull | (31ull << 12) | (1ull << 17) | (30ull << 22)
        | (17ull << 32) | (29ull << 37) | (6ull << 42) | (1ull << 46);
    require(encoded && encoded->bits == golden, "independent VADD allocation");
    const auto decoded = decode_holon({golden});
    require(decoded && std::get<predicate_register>(*decoded->find(npu_role::pg)).value() == 17,
        "predicate bank is independently addressable, not implicit p0");
    require(disassemble(*decoded) == "vadd vd=v31, va=v1, vb=v30, pg=p17, vl=x29, type=f32, policy=zero", "typed disassembly");
    auto args = decoded->operands;
    args[0].value = scalar_register{31};
    auto bad = encode_holon(npu_opcode::VADD, std::span{args}.first(decoded->pattern.count));
    require(!bad && bad.error() == encode_error::operand_type, "equal-width register domains cannot be interchanged");
    args[0].value = vector_register{32};
    bad = encode_holon(npu_opcode::VADD, std::span{args}.first(decoded->pattern.count));
    require(!bad && bad.error() == encode_error::operand_range, "vector register limit");
    args = decoded->operands; args[1] = args[0];
    require(!encode_holon(npu_opcode::VADD, std::span{args}.first(decoded->pattern.count)), "duplicate/missing roles");
    require(!encode_holon(npu_opcode::VADD, {}), "missing operand set");
    require(!encode_holon(static_cast<npu_opcode>(0xffff), {}), "unknown opcode");
    require(!decode_holon({0}) && !decode_holon({0xffffffffffffffffull}) && !decode_holon({0x13}), "zero/unknown/scalar words rejected");
    const auto gather = encode_holon(npu_opcode::VGATHER, {
        {npu_role::vd, vector_register{3}}, {npu_role::base, scalar_register{7}}, {npu_role::indices, vector_register{9}},
        {npu_role::pg, predicate_register{31}}, {npu_role::vl, scalar_register{11}},
        {npu_role::type, npu_type::i8}, {npu_role::policy, mask_policy::merge},
        {npu_role::scale, index_scale{3}}, {npu_role::offset, displacement{-524288}}});
    require(gather && gather->bits == ((36ull << 2) | (3ull << 12) | (7ull << 17) | (9ull << 22)
        | (31ull << 27) | (11ull << 32) | (3ull << 42) | (0x80000ull << 44)), "independent gather allocation");
    auto g = *decode_holon(*gather);
    require(std::get<displacement>(*g.find(npu_role::offset)).value() == -524288, "signed displacement not local offset");
    g.operands[8].value = displacement{524288};
    require(!encode_holon(g.pattern.opcode, g.arguments()), "positive displacement overflow");
    g.operands[8].value = displacement{-524289};
    require(!encode_holon(g.pattern.opcode, g.arguments()), "negative displacement overflow");
}
}

void npu_operand_tests() {
    using namespace holon_npu::semantic::instruction;
    directed();
    std::mt19937 random{0x4e505536};
    unsigned checked = 0;
    for (const auto& pattern : npu_patterns) {
        for (unsigned iteration = 0; iteration < 1024; ++iteration) {
            std::array<named_operand, npu_max_operands> args{};
            std::uint64_t raw = std::to_underlying(pattern.opcode);
            for (unsigned i = 0; i < pattern.count; ++i) {
                const auto& field = pattern.fields[i];
                unsigned bits = static_cast<unsigned>(random()) & ((1u << field.width) - 1);
                if (field.kind == npu_kind::element) {
                    const unsigned allowed = field.role == npu_role::type ? pattern.type_mask : 0x7f;
                    while (!(allowed & (1u << bits))) bits = (bits + 1) % 7;
                }
                args[i] = {field.role, value(field, bits)};
                raw |= std::uint64_t{bits} << field.shift;
            }
            const auto decoded = decode_holon({raw});
            require(decoded && decoded->pattern.opcode == pattern.opcode, "every opcode decodes with randomized fields");
            for (unsigned i = 0; i < pattern.count; ++i)
                require(decoded->operands[i].role == args[i].role && decoded->operands[i].value == args[i].value, "typed operands preserved");
            std::shuffle(args.begin(), args.begin() + pattern.count, random);
            const auto encoded = encode_holon(pattern.opcode, std::span{args}.first(pattern.count));
            require(encoded && encoded->bits == raw, "encoding independent of named-operand order");
            if (!iteration) {
                for (unsigned bit = 12; bit < 64; ++bit)
                    if (!(pattern.variable_mask & (1ull << bit)))
                        require(!decode_holon({raw | (1ull << bit)}), "every reserved bit must be zero");
                for (unsigned i = 0; i < pattern.count; ++i) {
                    const auto& f = pattern.fields[i];
                    if (f.kind != npu_kind::element) continue;
                    for (unsigned type = 0; type < 16; ++type) {
                        const auto word = (raw & ~(15ull << f.shift)) | (std::uint64_t{type} << f.shift);
                        const auto allowed = f.role == npu_role::type ? pattern.type_mask : 0x7f;
                        require(decode_holon({word}).has_value() == bool(allowed & (1u << type)), "all encoded type values checked");
                    }
                }
            }
            ++checked;
        }
    }
    std::cout << std::format("NPU operands: {}/{} opcodes, {} round trips seed=0x4e505536, reserved/domain checks PASS\n",
        npu_patterns.size(), npu_patterns.size(), checked);
}
