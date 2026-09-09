#include "holon_npu_execution.hpp"
#include "holon_npu_runtime.hpp"

#include <iostream>
#include <random>
#include <source_location>
#include <stdexcept>

namespace {
using namespace holon_npu::semantic;
using namespace holon_npu::semantic::instruction;
using holon_npu::runtime::program_builder;

constexpr auto entry = instruction_address{0x1000};
constexpr scalar_register x0{0}, x1{1}, x2{2}, x31{31};

void require(bool value, std::string_view message,
             std::source_location at = std::source_location::current()) {
    if (!value) throw std::runtime_error(std::string{message} + " at line " + std::to_string(at.line()));
}

program_machine machine() {
    auto map = memory::physical_map::create(std::array{
        memory::region{physical_address{entry.value()}, 4096, memory::storage::program, {true, false, true}},
        memory::region{physical_address{0x10000}, 4096, memory::storage::scratchpad, {true, true, false}}});
    require(map.has_value(), "fixture physical map");
    return program_machine{*map, {.program_bytes = 4096, .scratchpad_bytes = 4096}};
}

void execute(program_machine& dut, const program_builder& code, std::uint32_t status,
             std::uint64_t retired) {
    require(dut.boot(code.bytes(), physical_address{entry.value()}, entry).has_value(), "boot constructed bytes");
    const auto report = run_program(dut, {}, 10000);
    require(report && report->reason == run_reason::stopped && !report->traps, "constructed program terminates without traps");
    require(report->status == status && dut.hart().retired() == retired, "status and retirement scoreboard");
    require(dut.hart().pc().value() == entry.value() + code.offset(), "byte-addressed STOP continuation");
}

void literal_construction() {
    auto dut = machine();
    std::vector<std::uint32_t> values{0, 1, 0x7ff, 0x800, 0xfff, 0x1000,
        0x7ffff7ff, 0x7ffff800, 0x7fffffff, 0x80000000, 0xfffff7ff,
        0xfffff800, 0xfffffffe, 0xffffffff};
    constexpr std::uint32_t seed = 0x484f4c4e;
    std::mt19937 random(seed);
    for (unsigned i = 0; i < 1024; ++i) values.push_back(random());
    for (const auto rd : {x1, x31}) {
        for (const auto value : values) {
            program_builder code;
            code.li(rd, value).npu(npu_opcode::STOP, {{npu_role::status, rd}});
            require(code.offset() == 16, "LI is two scalar words followed by one Holon word");
            execute(dut, code, value, 3);
            require(dut.hart().reg(rd) == value && dut.hart().reg(x0) == 0, "full-width LI value and x0");
        }
    }
    program_builder discard;
    discard.li(x0, 0xffffffff).npu(npu_opcode::STOP, {{npu_role::status, x0}});
    execute(dut, discard, 0, 3);
    std::cout << "LI: " << values.size() * 2 + 1 << " programs, seed=" << seed << '\n';
}

void scalar_immediates() {
    auto dut = machine();
    for (std::int32_t immediate = -2048; immediate <= 2047; ++immediate) {
        program_builder code;
        code.addi(x31, x0, immediate).npu(npu_opcode::STOP, {{npu_role::status, x31}});
        require(code.offset() == 12, "ADDI/STOP size");
        const auto frame = fetch(code.bytes(), entry, entry);
        require(frame && std::holds_alternative<scalar_word>(*frame), "first parcel is scalar");
        const auto decoded = decode_scalar(std::get<scalar_word>(*frame));
        require(decoded && decoded->pattern.opcode == scalar_opcode::ADDI
            && decoded->rd == x31 && decoded->rs1 == x0 && decoded->immediate == immediate,
            "every signed imm12 and register field round trips");
        execute(dut, code, static_cast<std::uint32_t>(immediate), 2);
    }
    std::cout << "ADDI: all 4096 signed immediates execute\n";
}

template<class Append>
void rejected_append(program_builder& code, Append append) {
    const std::vector before(code.bytes().begin(), code.bytes().end());
    bool rejected = false;
    try { append(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid typed construction rejects input");
    require(std::ranges::equal(code.bytes(), before), "rejected append preserves complete existing image");
}

void validation() {
    program_builder code;
    code.addi(x1, x0, 17);
    rejected_append(code, [&] { code.li(scalar_register{32}, 0); });
    rejected_append(code, [&] { code.addi(x1, scalar_register{255}, 0); });
    rejected_append(code, [&] { code.addi(scalar_register{32}, x1, 0); });
    rejected_append(code, [&] { code.addi(x1, x0, -2049); });
    rejected_append(code, [&] { code.addi(x1, x0, 2048); });
    rejected_append(code, [&] { code.npu(npu_opcode::STOP, {}); });
    rejected_append(code, [&] { code.npu(npu_opcode::STOP, {{npu_role::status, vector_register{1}}}); });
    rejected_append(code, [&] { code.npu(npu_opcode::STOP, {{npu_role::status, x1}, {npu_role::status, x2}}); });
    rejected_append(code, [&] { code.npu(npu_opcode::STOP, {{npu_role::status, scalar_register{32}}}); });
    rejected_append(code, [&] { code.npu(static_cast<npu_opcode>(0xffff), {}); });

    // Raw words deliberately bypass semantic validation for assembler and fault tests.
    program_builder raw;
    raw.emit(scalar_word{0x1234567b}).emit(holon_word{0x89abcdef01234560ull});
    const std::array expected{std::byte{0x7b}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
        std::byte{0x60}, std::byte{0x45}, std::byte{0x23}, std::byte{0x01},
        std::byte{0xef}, std::byte{0xcd}, std::byte{0xab}, std::byte{0x89}};
    require(std::ranges::equal(raw.bytes(), expected), "raw words serialize little endian, including upper Holon parcel");
    std::cout << "Validation: bad operands leave image unchanged; raw emission preserves all bits\n";
}

void mixed_width_control_flow() {
    program_builder code;
    code.addi(x1, x0, 5).addi(x2, x0, 0);
    const auto loop = code.offset();
    code.npu(npu_opcode::CAPS, {{npu_role::rd, x31}, {npu_role::selector, resource_capacity::vector_bytes}})
        .addi(x2, x2, 3).addi(x1, x1, -1);
    const auto displacement = static_cast<std::uint32_t>(static_cast<std::int32_t>(loop) - static_cast<std::int32_t>(code.offset()));
    // Standard BNE x1,x0,byte-displacement; independent of encoder metadata.
    code.emit(scalar_word{0x1063u | ((displacement >> 12 & 1) << 31)
        | ((displacement >> 5 & 63) << 25) | (1u << 15)
        | ((displacement >> 1 & 15) << 8) | ((displacement >> 11 & 1) << 7)});
    code.npu(npu_opcode::STOP, {{npu_role::status, x2}});
    auto dut = machine();
    execute(dut, code, 15, 23);
    require(dut.hart().reg(x31) == dut.config().vector_bytes, "guest capability query result");
    require(loop == 8 && code.offset() == 36, "loop uses byte offsets across four/eight-byte instructions");
    std::cout << "Control flow: guest loop over mixed scalar/Holon instruction widths\n";
}
} // namespace

int main() {
    try {
        literal_construction(); scalar_immediates(); validation(); mixed_width_control_flow();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "PASS: canonical mixed-width runtime construction and execution\n";
}
