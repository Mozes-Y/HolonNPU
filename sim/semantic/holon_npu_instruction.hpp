#pragma once

#include "holon_npu_scalar_metadata.hpp"
#include "holon_npu_types.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <variant>

namespace holon_npu::semantic::instruction {

struct scalar_word { std::uint32_t bits{}; };
struct holon_word { std::uint64_t bits{}; };
using instruction_frame = std::variant<scalar_word, holon_word>;

enum class fetch_error { invalid_image, misaligned_pc, out_of_bounds, truncated };
enum class decode_error { illegal_scalar };

// Framing does not establish opcode legality, especially for unallocated NPU words.
[[nodiscard]] constexpr std::expected<instruction_frame, fetch_error> fetch(
    std::span<const std::byte> image, instruction_address pc,
    instruction_address base = {}) {
    constexpr auto address_space = std::uint64_t{1} << 32;
    if (base.value() % alignment_bytes != 0 || image.size() > address_space - base.value())
        return std::unexpected(fetch_error::invalid_image);
    if (pc.value() % alignment_bytes != 0)
        return std::unexpected(fetch_error::misaligned_pc);
    if (pc < base || pc.value() - base.value() >= image.size())
        return std::unexpected(fetch_error::out_of_bounds);
    const auto bytes = image.subspan(pc.value() - base.value());
    if (bytes.size() < scalar_bytes)
        return std::unexpected(fetch_error::truncated);
    std::uint32_t low = 0;
    for (unsigned i = 0; i < scalar_bytes; ++i)
        low |= std::to_integer<std::uint32_t>(bytes[i]) << (i * 8);
    if ((low & prefix_mask) == scalar_prefix)
        return scalar_word{low};
    if (bytes.size() < holon_bytes)
        return std::unexpected(fetch_error::truncated);
    std::uint64_t word = low;
    for (unsigned i = scalar_bytes; i < holon_bytes; ++i)
        word |= std::to_integer<std::uint64_t>(bytes[i]) << (i * 8);
    return holon_word{word};
}

struct scalar_register_tag;
using scalar_register = strong_value<std::uint8_t, scalar_register_tag>;

struct scalar_instruction {
    scalar_pattern pattern;
    scalar_register rd{}, rs1{}, rs2{};
    std::int32_t immediate{};
    std::uint16_t csr_address{};
    std::uint8_t fence_mode{}, predecessor{}, successor{};
};

[[nodiscard]] constexpr std::expected<scalar_instruction, decode_error> decode_scalar(scalar_word word) {
    const auto bits = word.bits;
    const auto match = std::ranges::find_if(scalar_patterns, [bits](const auto& pattern) {
        return (bits & pattern.mask) == pattern.value;
    });
    if (match == scalar_patterns.end())
        return std::unexpected(decode_error::illegal_scalar);
    const auto reg = [bits](unsigned shift) {
        return scalar_register{static_cast<std::uint8_t>((bits >> shift) & (register_count - 1))};
    };
    const auto sign_extend = [](std::uint32_t value, unsigned width) {
        const auto sign = std::uint32_t{1} << (width - 1);
        return std::bit_cast<std::int32_t>((value ^ sign) - sign);
    };
    scalar_instruction result{*match, reg(rd_shift), reg(rs1_shift), reg(rs2_shift)};
    switch (match->format) {
    case scalar_format::i:
        result.immediate = sign_extend(bits >> 20, 12);
        break;
    case scalar_format::s:
        result.immediate = sign_extend(((bits >> 25) << 5) | ((bits >> 7) & 31), 12);
        break;
    case scalar_format::b:
        result.immediate = sign_extend(((bits >> 31) << 12) | (((bits >> 7) & 1) << 11)
            | (((bits >> 25) & 63) << 5) | (((bits >> 8) & 15) << 1), 13);
        break;
    case scalar_format::u:
        result.immediate = std::bit_cast<std::int32_t>(bits & 0xFFFFF000u);
        break;
    case scalar_format::j:
        result.immediate = sign_extend(((bits >> 31) << 20) | (bits & 0xFF000u)
            | (((bits >> 20) & 1) << 11) | (((bits >> 21) & 1023) << 1), 21);
        break;
    case scalar_format::shift:
        result.immediate = result.rs2.value();
        break;
    case scalar_format::csr:
    case scalar_format::csr_immediate:
        result.csr_address = static_cast<std::uint16_t>(bits >> 20);
        result.immediate = result.rs1.value();
        break;
    case scalar_format::fence:
        result.fence_mode = static_cast<std::uint8_t>(bits >> 28);
        result.predecessor = static_cast<std::uint8_t>((bits >> 24) & 15);
        result.successor = static_cast<std::uint8_t>((bits >> 20) & 15);
        break;
    case scalar_format::r:
    case scalar_format::system:
        break;
    }
    return result;
}

[[nodiscard]] std::string disassemble(const scalar_instruction& instruction);

} // namespace holon_npu::semantic::instruction
