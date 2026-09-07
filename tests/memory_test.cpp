#include "holon_npu_execution.hpp"
#include "holon_npu_elf.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <source_location>
#include <stdexcept>

namespace {
using namespace holon_npu::semantic;
using namespace holon_npu::semantic::memory;
using namespace holon_npu::semantic::scalar;
constexpr permissions rx{true, false, true}, rw{true, true, false};

void require(bool ok, std::string_view message, std::source_location where = std::source_location::current()) {
    if (!ok) throw std::runtime_error(std::string{message} + " at line " + std::to_string(where.line()));
}
template<class T> T event(std::expected<hart_event, hart_error> result) {
    require(result && std::holds_alternative<T>(*result), "expected scalar event");
    return std::get<T>(*result);
}
physical_map mapped(std::initializer_list<region> regions) {
    auto result = physical_map::create(regions);
    require(result.has_value(), "valid physical map");
    return std::move(*result);
}
void constant(hart_state& hart, unsigned reg, std::uint32_t value) {
    event<committed>(hart.issue({((value + 0x800u) & 0xfffff000u) | reg << 7 | 0x37}));
    event<committed>(hart.issue({(value & 4095) << 20 | reg << 15 | reg << 7 | 0x13}));
}

void regions_and_transfers() {
    for (const auto invalid : {
        region{physical_address{0}, 0, storage::system, rw},
        region{physical_address{0xfffffffcu}, 8, storage::system, rw},
        region{physical_address{0}, 4, storage::program, rw},
        region{physical_address{0}, 4, storage::scratchpad, rx},
        region{physical_address{0}, 4, storage::system, {}}})
        require(!physical_map::create(std::array{invalid}), "invalid region rejected");
    require(!physical_map::create(std::array{region{physical_address{10}, 10, storage::system, rw},
        region{physical_address{9}, 2, storage::system, rw}}), "overlap rejected irrespective of order");
    require(!physical_map::create(std::array{region{physical_address{0}, 4, storage::scratchpad, rw},
        region{physical_address{8}, 4, storage::scratchpad, rw}}), "duplicate local owner rejected");

    std::array<std::byte, 32> code{}, spm{}, system{};
    bindings memory{code, spm, {system_address{0x80000000}, system}};
    const auto map = mapped({{physical_address{0x80000000}, 64, storage::system, rw},
        {physical_address{0x1000}, 32, storage::program, rx},
        {physical_address{0x2000}, 32, storage::scratchpad, rw}});
    const std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    require(map.write(physical_address{0x2004}, data, memory).has_value(), "SPM write");
    require(spm[4] == data[0] && spm[7] == data[3] && system[4] == std::byte{}, "SPM ownership");
    require(map.write(physical_address{0x80000004}, data, memory).has_value(), "system identity write");
    require(system[4] == data[0] && system[7] == data[3], "caller system storage changed");
    const auto before_spm = spm, before_system = system;
    require(!map.write(physical_address{0x101c}, data, memory), "program write denied");
    require(!map.write(physical_address{0x201f}, data, memory), "cross-region write denied");
    require(!map.write(physical_address{0x8000001f}, data, memory), "short backing write denied");
    require(spm == before_spm && system == before_system, "failed writes have no partial effects");
    std::array<std::byte, 4> result{std::byte{9}, std::byte{9}, std::byte{9}, std::byte{9}};
    const auto before_result = result;
    require(!map.read(physical_address{0x8000001f}, result, memory) && result == before_result,
        "failed reads have no partial effects");
    require(!map.read(physical_address{0x2000}, result, memory, access::execute), "SPM non-executable");
    require(!map.resolve(physical_address{0xfffffffcu}, 8, access::read), "32-bit overflow");
    require(!map.resolve(physical_address{0x2000}, 0, access::read), "empty transfer rejected");
    const auto top = mapped({{physical_address{0xfffffffc}, 4, storage::system, rw}});
    memory.system = {system_address{0xfffffffc}, system};
    require(top.write(physical_address{0xfffffffc}, data, memory).has_value(), "last full word is valid");
    memory.system = {system_address{0xffffffffffffffffull}, system};
    require(!memory.system.range(system_address{0xffffffffffffffffull}, 1), "invalid wrapping system view rejected");
}

void randomized_bounds() {
    std::mt19937 rng{0x4d415033};
    std::array<std::byte, 128> spm{};
    for (unsigned i = 0; i < 8192; ++i) {
        const auto base = static_cast<std::uint32_t>(rng() & 0xffff0000u);
        const unsigned capacity = rng() % 128 + 1;
        const unsigned backing = rng() % 129;
        const unsigned offset = rng() % 150;
        const unsigned size = rng() % 9 + 1;
        auto map = mapped({{physical_address{base}, capacity, storage::scratchpad, rw}});
        std::array<std::byte, 9> payload{};
        payload.fill(std::byte{0x5a});
        spm.fill(std::byte{0});
        const auto before = spm;
        const bool expected = offset + size <= capacity && offset + size <= backing;
        const auto result = map.write(physical_address{base + offset}, std::span{payload}.first(size),
            {{}, std::span{spm}.first(backing), {}});
        require(result.has_value() == expected, "independent random interval scoreboard");
        if (!expected) require(spm == before, "random failed writes atomic");
        else for (unsigned byte = 0; byte < spm.size(); ++byte)
            require(spm[byte] == ((byte >= offset && byte < offset + size) ? std::byte{0x5a} : std::byte{0}),
                "random writes touch exactly requested bytes");
    }
}

void fetch_and_complete() {
    std::array<std::byte, 8> code{}, spm{}, ram{};
    code[0] = std::byte{0x13};
    auto map = mapped({{physical_address{0x1000}, 4, storage::program, rx},
        {physical_address{0x1004}, 4, storage::system, rx},
        {physical_address{0x2000}, 8, storage::scratchpad, rw}});
    bindings memory{code, spm, {system_address{0x1004}, ram}};
    auto frame = fetch_instruction(map, memory, instruction_address{0x1000});
    require(frame && std::holds_alternative<instruction::scalar_word>(*frame), "scalar fetch needs only one parcel");
    code[0] = std::byte{};
    ram[0] = std::byte{0x42};
    frame = fetch_instruction(map, memory, instruction_address{0x1000});
    require(frame && std::get<instruction::holon_word>(*frame).bits == 0x4200000000ull,
        "Holon fetch crosses adjacent executable owners without duplicated framing");
    memory.system.bytes = {};
    frame = fetch_instruction(map, memory, instruction_address{0x1000});
    require(!frame && frame.error().cause == instruction::scalar_trap_cause::instruction_access_fault
        && frame.error().pc.value() == 0x1000 && frame.error().value == 0x1004, "second parcel precise address");
    hart_state fetch_hart;
    require(fetch_hart.start(instruction_address{0x1000}).has_value(), "fetch fault test entry");
    const auto fetch_trap = fetch_hart.fetch_fault(instruction_address{frame.error().value});
    require(fetch_trap && fetch_trap->pc.value() == 0x1000 && fetch_trap->value == 0x1004
        && fetch_hart.retired() == 0, "second parcel fault retains instruction PC and failing byte address");
    require(!fetch_instruction(map, memory, instruction_address{0x1002}), "fetch alignment");
    require(!fetch_instruction(map, memory, instruction_address{0x2000}), "no SPM fetch");
    const auto top = mapped({{physical_address{0xfffffffc}, 4, storage::program, rx}});
    require(!fetch_instruction(top, memory, instruction_address{0xfffffffc}), "Holon instruction cannot wrap XLEN");
    code[0] = std::byte{0x13};
    require(fetch_instruction(top, memory, instruction_address{0xfffffffc}).has_value(), "final scalar word fetch valid");

    hart_state hart;
    constant(hart, 1, 0x2000);
    constant(hart, 2, 0xfedcba98);
    event<pending_effect>(hart.issue({0x0020a023}));
    const auto count = hart.retired();
    event<committed>(service_scalar(hart, map, memory));
    require(hart.retired() == count + 1 && spm[0] == std::byte{0x98} && spm[3] == std::byte{0xfe}, "captured store little endian");
    spm[0] = std::byte{0};
    require(!service_scalar(hart, map, memory) && spm[0] == std::byte{}, "no stale store replay");
    event<pending_effect>(hart.issue({0x00308183})); // lb x3,3(x1)
    event<committed>(service_scalar(hart, map, memory));
    require(hart.reg(instruction::scalar_register{3}) == 0xfffffffe, "routed load sign extension");
    event<pending_effect>(hart.issue({0x0ff0000f}));
    event<committed>(service_scalar(hart, map, memory));
    constant(hart, 1, 0x1000);
    const auto fault_pc = hart.pc();
    event<pending_effect>(hart.issue({0x0020a023}));
    const auto fault = event<trap_taken>(service_scalar(hart, map, memory));
    require(fault.cause == 7 && fault.pc == fault_pc && fault.value == 0x1000, "write permission failure traps precisely");
}

std::vector<std::byte> read_file(const char* path) {
    std::ifstream stream{path, std::ios::binary};
    require(stream.good(), "input binary readable");
    std::vector<char> chars{std::istreambuf_iterator<char>{stream}, {}};
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char value) { return static_cast<std::byte>(value); });
    return bytes;
}

void compiled_program(const char* path) {
    const auto image = elf::image::parse(read_file(path));
    require(image.has_value(), "valid compiled ELF32 image");
    std::array<std::byte, 4096> code{}, spm{}, ram{};
    code.fill(std::byte{0xa5}); spm.fill(std::byte{0xa5}); ram.fill(std::byte{0xa5});
    const auto map = mapped({{physical_address{0x1000}, code.size(), storage::program, rx},
        {physical_address{0x10000000}, spm.size(), storage::scratchpad, rw},
        {physical_address{0x80000000}, ram.size(), storage::system, rw}});
    bindings memory{code, spm, {system_address{0x80000000}, ram}};
    require(image->load(map, {code, spm, memory.system}).has_value(), "complete ELF load");
    require(std::ranges::all_of(std::span{ram}.subspan(0x800, 72), [](auto b) { return b == std::byte{}; }),
        "ELF BSS zeroed before execution");
    require(ram.back() == std::byte{0xa5} && spm.front() == std::byte{0xa5}, "unloaded storage preserved");
    hart_state hart;
    require(hart.start(image->entry()).has_value(), "compiled program entry");
    unsigned stack_accesses = 0, system_accesses = 0;
    // Test harness supplies memory and instruction bytes; all execution is in the shared hart.
    for (unsigned attempt = 0; attempt < 100000 && !hart.waiting(); ++attempt) {
        const auto frame = fetch_instruction(map, memory, hart.pc());
        require(frame && std::holds_alternative<instruction::scalar_word>(*frame), "compiled scalar fetch");
        auto result = hart.issue(std::get<instruction::scalar_word>(*frame));
        require(result.has_value(), "compiled instruction issue");
        if (const auto pending = hart.pending_request()) {
            std::visit([&](const auto& request) {
                if constexpr (requires { request.address; }) {
                    if (request.address.value() >= 0x80000000) ++system_accesses;
                    else if (request.address.value() >= 0x10000000) ++stack_accesses;
                }
            }, pending->request);
            result = service_scalar(hart, map, memory);
        }
        event<committed>(result);
    }
    require(hart.waiting(), "program reached WFI after publishing results, not a simulated exit");
    const auto word = [&](unsigned offset) {
        std::uint32_t value = 0;
        for (unsigned byte = 0; byte < 4; ++byte)
            value |= std::to_integer<std::uint32_t>(ram[offset + byte]) << (byte * 8);
        return value;
    };
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < 16; ++i) {
        const auto expected = ((i + 1) * (i + 7) + 11) / (i + 1);
        require(word(0x800 + i * 4) == expected, "independent compiled program value");
        sum += expected;
    }
    require(word(0x840) == sum && word(0x844) == 0x484f4c4f, "complete workload output and publication");
    require(stack_accesses && system_accesses, "compiled stack and globals reach different owners");
    std::cout << "RV32 program: instret=" << hart.retired() << " stack accesses=" << stack_accesses
              << " system accesses=" << system_accesses << " checksum=" << sum << " verified, WFI waiting\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2) compiled_program(argv[1]);
        else {
            require(argc == 1, "expected ELF file or no arguments");
            regions_and_transfers(); randomized_bounds(); fetch_and_complete();
            std::cout << "physical memory: directed permissions/fetch/completion and 8192 interval cases seed=0x4d415033 passed\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
