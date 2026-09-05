#include "holon_npu_instruction.hpp"

#include <array>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <source_location>
#include <stdexcept>
#include <vector>

namespace {
using namespace holon_npu::semantic::instruction;
using holon_npu::semantic::instruction_address;

void require(bool ok, std::string_view message,
             std::source_location location = std::source_location::current()) {
    if (!ok) throw std::runtime_error(std::format("{}:{}: {}", location.file_name(), location.line(), message));
}

void put(std::span<std::byte> image, std::size_t offset, std::uint64_t word, unsigned size = 4) {
    for (unsigned i = 0; i < size; ++i)
        image[offset + i] = std::byte{static_cast<std::uint8_t>(word >> (i * 8))};
}

static_assert(decode_scalar({0xFFF00093})->immediate == -1);
static_assert(decode_scalar({0x00000013})->pattern.opcode == scalar_opcode::ADDI);
static_assert(!decode_scalar({0x02001013})); // RV64 shamt bit is illegal for RV32.

void framing() {
    const auto expect_error = [](const auto& result, fetch_error error, std::string_view message) {
        require(!result && result.error() == error, message);
    };
    std::array<std::byte, 16> image{};
    put(image, 0, 0x00000013);
    put(image, 12, 0x30200073);
    for (const auto prefix : {0U, 1U, 2U}) {
        const auto bits = std::uint64_t{0x876543219ABCDEF0} | prefix;
        put(image, 4, bits, 8);
        const auto scalar = fetch(image, instruction_address{0});
        const auto custom = fetch(image, instruction_address{4});
        const auto tail = fetch(image, instruction_address{12});
        require(scalar && std::get<scalar_word>(*scalar).bits == 0x13, "initial scalar frame");
        require(custom && std::get<holon_word>(*custom).bits == bits, "64-bit unaligned-to-eight frame");
        require(tail && std::get<scalar_word>(*tail).bits == 0x30200073, "following scalar not swallowed");
        for (std::size_t size = 1; size < 8; ++size) {
            const auto truncated = fetch(std::span{image}.subspan(4, size), instruction_address{0});
            require(!truncated && truncated.error() == fetch_error::truncated, "truncated custom frame");
        }
    }
    expect_error(fetch({}, {}), fetch_error::out_of_bounds, "empty image");
    expect_error(fetch(image, instruction_address{2}), fetch_error::misaligned_pc, "no 16-bit alignment");
    expect_error(fetch(image, instruction_address{16}), fetch_error::out_of_bounds, "end-of-image fetch");
    expect_error(fetch(image, instruction_address{0}, instruction_address{4}), fetch_error::out_of_bounds,
                 "PC below base");
    expect_error(fetch(image, instruction_address{0}, instruction_address{2}), fetch_error::invalid_image,
                 "misaligned image base");
    expect_error(fetch(image, instruction_address{0xFFFFFFFC}, instruction_address{0xFFFFFFFC}),
                 fetch_error::invalid_image, "image cannot wrap address space");
    const auto end = fetch(std::span{image}.first(4), instruction_address{0xFFFFFFFC}, instruction_address{0xFFFFFFFC});
    require(end && std::holds_alternative<scalar_word>(*end), "last aligned scalar word is valid");
    put(image, 0, 0xFFFFFFFF);
    const auto illegal = fetch(image, {});
    require(illegal && std::holds_alternative<scalar_word>(*illegal) &&
            !decode_scalar(std::get<scalar_word>(*illegal)), "standard long prefix cannot select Holon");
    std::cout << "framing: 3 Holon prefixes, mixed widths, bounds/alignment/truncation PASS\n";
}

void patterns() {
    std::mt19937 random{0x484F4C4F};
    std::size_t checked = 0;
    for (const auto& pattern : scalar_patterns) {
        for (unsigned sample = 0; sample < 1024; ++sample) {
            const auto bits = pattern.value | (static_cast<std::uint32_t>(random()) & ~pattern.mask);
            const auto result = decode_scalar({bits});
            require(result && result->pattern.opcode == pattern.opcode, "random operand decode");
            require(result->rd.value() == ((bits >> 7) & 31) &&
                    result->rs1.value() == ((bits >> 15) & 31) &&
                    result->rs2.value() == ((bits >> 20) & 31), "five-bit register extraction");
            require(disassemble(*result).starts_with(pattern.mnemonic), "mnemonic uses matched metadata");
            ++checked;
        }
        for (unsigned bit = 0; bit < 32; ++bit) {
            if ((pattern.mask & (1U << bit)) == 0) continue;
            const auto mutated = decode_scalar({pattern.value ^ (1U << bit)});
            require(!mutated || mutated->pattern.opcode != pattern.opcode, "fixed bit cannot be ignored");
        }
    }
    for (auto word : {0U, 0xFFFFFFFFU, 0x00003003U, 0x0000001BU, 0x0000100FU,
                     0x02001013U, 0x02005013U, 0x001000F3U, 0x10200073U, 0x0050100FU}) {
        require(!decode_scalar({word}), "unsupported/reserved instruction rejected");
    }
    require(decode_scalar({0x30200073})->pattern.opcode == scalar_opcode::MRET, "machine return decode");
    require(decode_scalar({0x10500073})->pattern.opcode == scalar_opcode::WFI, "machine wait decode");
    std::cout << std::format("scalar patterns: {}/{} observed, {} operand cases seed=0x484f4c4f PASS\n",
                             scalar_patterns.size(), scalar_patterns.size(), checked);
}

void immediates() {
    for (std::uint32_t raw = 0; raw < 4096; ++raw) {
        const auto expected = static_cast<std::int32_t>(raw) - (raw >= 2048 ? 4096 : 0);
        require(decode_scalar({(raw << 20) | 0x13})->immediate == expected, "I immediate");
        const auto store = ((raw >> 5) << 25) | ((raw & 31) << 7) | 0x2023;
        require(decode_scalar({store})->immediate == expected, "S immediate");
        const auto csr = decode_scalar({(raw << 20) | (31 << 15) | 0x5073});
        require(csr && csr->csr_address == raw && csr->immediate == 31, "CSR address/zimm are unsigned");
    }
    for (std::uint32_t raw = 0; raw < 8192; raw += 2) {
        const auto bits = ((raw >> 12) << 31) | (((raw >> 11) & 1) << 7)
            | (((raw >> 5) & 63) << 25) | (((raw >> 1) & 15) << 8) | 0x63;
        const auto expected = static_cast<std::int32_t>(raw) - (raw >= 4096 ? 8192 : 0);
        require(decode_scalar({bits})->immediate == expected, "B immediate is byte-relative, not word-relative");
    }
    for (std::uint32_t raw = 0; raw < (1U << 21); raw += 2) {
        const auto bits = ((raw >> 20) << 31) | (((raw >> 1) & 1023) << 21)
            | (((raw >> 11) & 1) << 20) | (raw & 0xFF000) | 0x6F;
        const auto expected = static_cast<std::int32_t>(raw) - (raw >= (1U << 20) ? (1 << 21) : 0);
        require(decode_scalar({bits})->immediate == expected, "J immediate bit permutation");
    }
    require(decode_scalar({0xFFFFF0B7})->immediate == -4096, "U immediate preserves high sign bit");
    require(decode_scalar({0x41F05013})->immediate == 31, "RV32 SRAI maximum shift");
    require(disassemble(*decode_scalar({0xFFF10093})) == "addi x1, x2, -1", "canonical scalar disassembly");
    require(disassemble(*decode_scalar({0x340FD0F3})) == "csrrwi x1, 0x340, 31", "CSR disassembly");
    std::cout << "operands: exhaustive I/S/B/J immediates and CSR addresses PASS\n";
}

void disassemble_file(const char* path) {
    std::ifstream file{path, std::ios::binary};
    require(file.is_open(), "open scalar instruction stream");
    const std::vector<char> chars{std::istreambuf_iterator<char>{file}, {}};
    std::vector<std::byte> bytes;
    for (char value : chars) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    require(!bytes.empty() && bytes.size() % 4 == 0, "whole scalar stream");
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
        const auto frame = fetch(bytes, instruction_address{static_cast<std::uint32_t>(offset)});
        require(frame && std::holds_alternative<scalar_word>(*frame), "upstream stream contains no RVC/Holon words");
        const auto decoded = decode_scalar(std::get<scalar_word>(*frame));
        require(decoded.has_value(), "upstream encoding is supported");
        std::cout << disassemble(*decoded) << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view{argv[1]} == "--scalar-words") {
            disassemble_file(argv[2]);
        } else {
            require(argc == 1, "usage: instruction_test [--scalar-words file]");
            framing();
            patterns();
            immediates();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
