#include "holon_npu_elf.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string>

namespace {
using namespace holon_npu::semantic;
constexpr memory::permissions rx{true, false, true}, rw{true, true, false};
using bytes = std::vector<std::byte>;
void require(bool condition, std::string_view text, std::source_location loc = std::source_location::current()) {
    if (!condition) throw std::runtime_error(std::string{text} + " at line " + std::to_string(loc.line()));
}
void put(bytes& data, unsigned offset, std::uint32_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) data.at(offset + i) = std::byte((value >> (8 * i)) & 255);
}
void text(bytes& data, std::string_view value) {
    for (char c : value) data.push_back(std::byte(c));
    data.push_back(std::byte{});
}
bytes profile(std::string_view arch = "rv32i2p1_m2p0_zicsr2p0_zmmul1p0", unsigned stack = 16) {
    bytes result(5);
    result[0] = std::byte{'A'};
    text(result, "riscv");
    const auto begin = static_cast<unsigned>(result.size());
    result.push_back(std::byte{1}); result.resize(result.size() + 4);
    result.push_back(std::byte{4}); result.push_back(std::byte(stack));
    result.push_back(std::byte{5}); text(result, arch);
    put(result, begin + 1, static_cast<unsigned>(result.size()) - begin);
    put(result, 1, static_cast<unsigned>(result.size()) - 1);
    return result;
}
void ph(bytes& file, unsigned index, unsigned type, unsigned offset, unsigned address,
        unsigned size, unsigned memory_size, unsigned flags, unsigned alignment = 4) {
    const unsigned base = 52 + index * 32;
    for (auto [field, value] : std::to_array<std::pair<unsigned, unsigned>>({{0u, type}, {4u, offset}, {8u, address},
         {12u, address}, {16u, size}, {20u, memory_size}, {24u, flags}, {28u, alignment}}))
        put(file, base + field, value);
}
bytes fixture(const bytes& attrs = profile()) {
    bytes file(0x300 + attrs.size());
    put(file, 0, 0x464c457f); put(file, 4, 0x010101, 3);
    put(file, 16, 2, 2); put(file, 18, 243, 2); put(file, 20, 1);
    put(file, 24, 0x1000); put(file, 28, 52); put(file, 40, 52, 2);
    put(file, 42, 32, 2); put(file, 44, 3, 2);
    ph(file, 0, 1, 0x200, 0x1000, 4, 8, 5);
    ph(file, 1, 1, 0x204, 0x80000000, 4, 16, 6);
    ph(file, 2, 0x70000003, 0x300, 0, static_cast<unsigned>(attrs.size()), 0, 4, 1);
    put(file, 0x200, 0x10500073); put(file, 0x204, 0x78563412);
    std::ranges::copy(attrs, file.begin() + 0x300);
    return file;
}
struct environment {
    std::array<std::byte, 32> code{}, spm{}, ram{};
    environment() { code.fill(std::byte{0xa5}); spm.fill(std::byte{0xa5}); ram.fill(std::byte{0xa5}); }
    memory::physical_map map(memory::permissions system = rw) const {
        const auto result = memory::physical_map::create(std::array{
            memory::region{physical_address{0x1000}, code.size(), memory::storage::program, rx},
            memory::region{physical_address{0x2000}, spm.size(), memory::storage::scratchpad, rw},
            memory::region{physical_address{0x80000000}, ram.size(), memory::storage::system, system}});
        require(result.has_value(), "fixture map");
        return *result;
    }
    elf::load_bindings bindings() { return {code, spm, {system_address{0x80000000}, ram}}; }
};
void loading() {
    auto file = fixture();
    const auto parsed = elf::image::parse(file);
    require(parsed && parsed->entry().value() == 0x1000 && parsed->segments().size() == 2, "ELF identity");
    auto owned = *parsed;
    std::ranges::fill(file, std::byte{});
    auto moved = std::move(owned);
    environment env;
    require(moved.load(env.map(), env.bindings()).has_value(), "owning image survives input modification and move");
    require(env.code[0] == std::byte{0x73} && env.code[3] == std::byte{0x10}, "initialized executable");
    require(env.ram[0] == std::byte{0x12} && env.ram[3] == std::byte{0x78}, "initialized globals");
    for (unsigned i = 4; i < 32; ++i) {
        require(env.code[i] == (i < 8 ? std::byte{} : std::byte{0xa5}), "code BSS extent exact");
        require(env.ram[i] == (i < 16 ? std::byte{} : std::byte{0xa5}), "data BSS extent exact");
    }
    require(std::ranges::all_of(env.spm, [](auto b) { return b == std::byte{0xa5}; }), "unloaded SPM preserved");
    for (bool short_backing : {false, true}) {
        environment bad;
        auto bindings = bad.bindings();
        if (short_backing) bindings.system.bytes = std::span{bad.ram}.first(15);
        const auto loaded = parsed->load(bad.map(short_backing ? rw : rx), bindings);
        require(!loaded && loaded.error() == (short_backing ? elf::error::backing : elf::error::mapping), "late segment rejected");
        require(bad.code == environment{}.code && bad.ram == environment{}.ram, "late failure never mutates early segment");
    }
    file = fixture();
    ph(file, 1, 1, 0x204, 0x2000, 4, 32, 6);
    const auto local = elf::image::parse(file);
    require(local && local->load(env.map(), env.bindings()).has_value() && env.spm[0] == std::byte{0x12}
        && env.spm.back() == std::byte{}, "SPM initialization uses local offset");
    auto short_code = env.bindings(); short_code.program = {};
    const auto before = env.code;
    require(!local->load(env.map(), short_code) && env.code == before, "program backing checked at initialization");
}
void rejection() {
    // Mutations use standard ELF offsets independently of the parser implementation.
    struct mutation { unsigned offset; std::uint32_t value; unsigned width; elf::error error; };
    for (const auto m : std::to_array<mutation>({
        {0, 0, 4, elf::error::header}, {4, 2, 1, elf::error::header},
        {5, 2, 1, elf::error::header}, {7, 3, 1, elf::error::header},
        {16, 3, 2, elf::error::header}, {18, 62, 2, elf::error::header},
        {36, 1, 4, elf::error::unsupported_profile}, {36, 4, 4, elf::error::unsupported_profile},
        {40, 51, 2, elf::error::header}, {42, 56, 2, elf::error::header},
        {44, 0xffff, 2, elf::error::header}, {28, 0xfffffff0, 4, elf::error::truncated},
        {50, 0xffff, 2, elf::error::header}, {32, 52, 4, elf::error::header},
        {24, 0x1002, 4, elf::error::entry}, {24, 0x1004, 4, elf::error::entry},
        {52, 2, 4, elf::error::unsupported_segment}, {52, 7, 4, elf::error::unsupported_segment},
        {56, 0xffffffff, 4, elf::error::truncated}, {64, 0x1004, 4, elf::error::segment},
        {72, 3, 4, elf::error::segment}, {76, 0, 4, elf::error::segment},
        {80, 3, 4, elf::error::alignment}, {80, 4096, 4, elf::error::alignment},
        {104, 0xffffffff, 4, elf::error::segment}})) {
        auto file = fixture(); put(file, m.offset, m.value, m.width);
        const auto parsed = elf::image::parse(file);
        require(!parsed && parsed.error() == m.error, "malformed header/segment rejected precisely");
    }
    auto file = fixture(); ph(file, 1, 1, 0x204, 0x1004, 4, 16, 6);
    const auto overlap = elf::image::parse(file);
    require(!overlap && overlap.error() == elf::error::overlap, "overlapping LOADs rejected");
    file = fixture(); ph(file, 1, 1, 0x204, 0xfffffffc, 4, 4, 6);
    require(elf::image::parse(file).has_value(), "final address-space word valid");
    ph(file, 1, 1, 0x204, 0xfffffffc, 4, 5, 6);
    require(!elf::image::parse(file), "address extent cannot wrap XLEN");
    for (std::string_view arch : {"rv64i2p1", "rv32i2p0", "rv32i2p1_c2p0", "rv32i2p1_f2p2",
         "rv32i2p1_m2p0_m2p0", "rv32i2p1_", "rv32i2p1__m2p0"})
        require(!elf::image::parse(fixture(profile(arch))), "unsupported ISA metadata");
    require(!elf::image::parse(fixture(profile("rv32i2p1", 4))), "ILP32 stack alignment");
    require(elf::image::parse(fixture(profile("rv32i2p1"))).has_value(), "compatible scalar subset");
    const auto complete = fixture();
    for (unsigned size = 0; size < complete.size(); ++size)
        require(!elf::image::parse(std::span{complete}.first(size)), "every truncation rejected");
}
void attribute_tables() {
    auto file = fixture();
    ph(file, 2, 0, 0, 0, 0, 0, 0);
    const unsigned section = static_cast<unsigned>(file.size());
    file.resize(file.size() + 80);
    put(file, 32, section); put(file, 46, 40, 2); put(file, 48, 2, 2);
    put(file, section + 44, 0x70000003); put(file, section + 56, 0x300);
    put(file, section + 60, static_cast<unsigned>(profile().size()));
    require(elf::image::parse(file).has_value(), "section attributes fallback");
    put(file, section + 60, 0xffffffff);
    require(!elf::image::parse(file), "truncated attribute section");
    for (unsigned tag : {6, 14, 16, 18, 64}) {
        auto attrs = profile(); attrs.push_back(std::byte(tag)); attrs.push_back(std::byte{2});
        put(attrs, 1, static_cast<unsigned>(attrs.size()) - 1);
        put(attrs, 12, static_cast<unsigned>(attrs.size()) - 11);
        require(elf::image::parse(fixture(attrs)).has_value() == (tag == 64), "mandatory/optional numeric attributes");
    }
    for (auto tail : std::array{bytes{std::byte{5}, std::byte{}}, bytes{std::byte{0x80}},
         bytes{std::byte{64}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0x10}}}) {
        auto attrs = profile(); attrs.insert(attrs.end(), tail.begin(), tail.end());
        put(attrs, 1, static_cast<unsigned>(attrs.size()) - 1);
        put(attrs, 12, static_cast<unsigned>(attrs.size()) - 11);
        require(!elf::image::parse(fixture(attrs)), "duplicate/unterminated/overflow attributes");
    }
}
void randomized() {
    std::mt19937 random{0x454c4632};
    for (unsigned iteration = 0; iteration < 16384; ++iteration) {
        auto file = fixture();
        for (unsigned byte = 0, count = 1 + random() % 8; byte < count; ++byte)
            file[random() % file.size()] = std::byte(random() & 255);
        const auto parsed = elf::image::parse(file);
        if (!parsed) continue;
        environment env;
        const auto result = parsed->load(env.map(), env.bindings());
        if (!result) {
            require(env.code == environment{}.code && env.spm == environment{}.spm && env.ram == environment{}.ram,
                "mutated ELF load failure is atomic");
        } else {
            auto expected_code = environment{}.code, expected_spm = environment{}.spm, expected_ram = environment{}.ram;
            for (const auto& segment : parsed->segments()) {
                auto& expected = segment.address.value() >= 0x80000000 ? expected_ram :
                    segment.address.value() >= 0x2000 ? expected_spm : expected_code;
                const auto base = segment.address.value() >= 0x80000000 ? 0x80000000u :
                    segment.address.value() >= 0x2000 ? 0x2000u : 0x1000u;
                for (unsigned i = 0; i < segment.memory_size; ++i)
                    expected.at(segment.address.value() - base + i) = i < segment.file_size ? file.at(segment.file_offset + i) : std::byte{};
            }
            require(env.code == expected_code && env.spm == expected_spm && env.ram == expected_ram, "exact randomized byte effects");
        }
    }
}
}
int main() {
    try {
        loading(); rejection(); attribute_tables(); randomized();
        std::cout << "ELF32: headers/profile/LOAD/BSS/atomicity and 16384 mutations seed=0x454c4632 passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
