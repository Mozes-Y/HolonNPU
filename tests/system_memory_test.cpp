#include "holon_npu_execution.hpp"
#include "holon_npu_runtime.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace holon_npu::semantic;
using namespace holon_npu::semantic::instruction;
using holon_npu::runtime::program_builder;
using enum npu_role;
constexpr std::uint32_t code_base = 0x1000, spm_base = 0x10000, ram_base = 0x80000000;
constexpr std::uint32_t external_code = 0x1ffc, output = 0x5f00, transfer_bytes = 601;
constexpr std::size_t memory_bytes = 0x8000;
scalar_register x(unsigned reg) { return scalar_register{static_cast<std::uint8_t>(reg)}; }
void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string{message});
}
void put(std::span<std::byte> memory, unsigned address, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) memory[address + i] = static_cast<std::byte>(value >> (8 * i));
}
std::uint32_t get(std::span<const std::byte> memory, unsigned address) {
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i) value |= std::to_integer<std::uint32_t>(memory[address + i]) << (8 * i);
    return value;
}
void csr_read(program_builder& code, unsigned rd, unsigned csr) {
    code.emit(scalar_word{(csr << 20) | (2u << 12) | (rd << 7) | 0x73});
}
void store(program_builder& code, unsigned rs, unsigned offset) {
    code.emit(scalar_word{((offset >> 5) << 25) | (rs << 20) | (10u << 15)
        | (2u << 12) | ((offset & 31) << 7) | 0x23});
}
void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    file.close();
    require(bool(file), "fixture write failed");
}

void run_case(unsigned page_offset, const std::filesystem::path& destination) {
    const unsigned source = page_offset, target = 0x3000 + page_offset;
    program_builder boot;
    boot.li(x(1), code_base + 256).emit(scalar_word{0x30509073}) // csrw mtvec,x1
        .li(x(1), spm_base).li(x(2), ram_base + source).li(x(3), transfer_bytes)
        .li(x(6), ram_base + external_code).emit(scalar_word{0x00030067}); // jalr x0,0(x6)
    while (boot.offset() < 256) boot.emit(scalar_word{0x13});
    // Record the precise trap in guest registers, skip the illegal 64-bit word and return.
    csr_read(boot, 20, 0x341); csr_read(boot, 21, 0x342); csr_read(boot, 22, 0x343);
    boot.addi(x(5), x(20), 8).emit(scalar_word{0x34129073}).emit(scalar_word{0x30200073});

    program_builder remote;
    remote.npu(npu_opcode::DLOAD, {{dst,x(1)}, {src,x(2)}, {count,x(3)}})
        .li(x(1), ram_base + target).li(x(2), spm_base)
        .npu(npu_opcode::DSTORE, {{dst,x(1)}, {src,x(2)}, {count,x(3)}})
        .li(x(10), ram_base + output).li(x(11), ram_base + source + (4 - source % 4) % 4)
        .emit(scalar_word{0x0005a603}); // lw x12,0(x11)
    store(remote, 12, 0);
    const auto fault_pc = ram_base + external_code + remote.offset();
    remote.emit(holon_word{0});
    store(remote, 20, 4); store(remote, 21, 8); store(remote, 22, 12);
    remote.npu(npu_opcode::STOP, {{status,x(0)}});

    std::vector<std::byte> ram(memory_bytes, std::byte{0xa5});
    for (unsigned i = 0; i < transfer_bytes; ++i) ram[source + i] = static_cast<std::byte>((i * 37 + 11) & 255);
    std::ranges::copy(remote.bytes(), ram.begin() + external_code);
    const auto initial = ram;
    auto expected = initial;
    std::ranges::copy(std::span{initial}.subspan(source, transfer_bytes), expected.begin() + target);
    put(expected, output, get(initial, source + (4 - source % 4) % 4));
    put(expected, output + 4, fault_pc); put(expected, output + 8, 2); put(expected, output + 12, 0);
    const auto map = memory::physical_map::create(std::array{
        memory::region{physical_address{code_base},49152,memory::storage::program,{true,false,true}},
        memory::region{physical_address{spm_base},65536,memory::storage::scratchpad,{true,true,false}},
        memory::region{physical_address{ram_base},memory_bytes,memory::storage::system,{true,true,true}}});
    require(map.has_value(), "system memory map");
    program_machine machine(*map);
    require(machine.boot(boot.bytes(), physical_address{code_base}, instruction_address{code_base}).has_value(), "local boot");
    std::uint64_t packets{}, bytes{}, traps{}, external_fetches{};
    bool stopped = false;
    for (unsigned steps = 0; steps < 10000 && !stopped; ++steps) {
        const auto pending = machine.pending();
        if (pending) {
            if (const auto* request = std::get_if<memory_request>(&pending->value);
                request && request->storage == memory::storage::system) {
                bytes += request->size;
                external_fetches += request->access == memory::access::execute;
                // Count the current adapter's packet contract, not a second timing model.
                auto address = std::uint64_t{request->address.value()};
                auto remaining = std::uint64_t{request->size};
                while (remaining) {
                    const auto size = std::min({remaining, std::uint64_t{256}, 4096 - address % 4096, 64 - address % 64});
                    ++packets; remaining -= size; address += size;
                }
            }
        }
        const auto event = pending ? service_operation(machine, {system_address{ram_base},ram}) : machine.advance();
        require(event.has_value(), "guest execution API");
        if (const auto* trap = std::get_if<scalar::trap_taken>(&*event)) {
            require(trap->pc.value() == fault_pc && trap->cause == 2 && trap->value == 0, "precise external illegal instruction trap");
            ++traps;
        }
        if (const auto* stop = std::get_if<holon_npu::semantic::stopped>(&*event)) {
            require(stop->status == 0, "guest stop status"); stopped = true;
        }
    }
    require(stopped && traps == 1, "external program must recover one trap and stop");
    // Twelve boot, six handler and sixteen remote instructions retire; the illegal word does not.
    require(machine.hart().retired() == 34, "independent retirement count excludes faulted instruction and fetch parcels");
    require(bytes == 2 * transfer_bytes + remote.offset() + 20, "external traffic is code, DMA and five scalar words only");
    require(ram == expected, "independent full-memory and trap-register scoreboard");
    require(external_fetches > 2 && machine.hart().pc().value() == ram_base + external_code + remote.offset(), "external mixed-width fetch and final PC");
    std::cout << "System memory offset=0x" << std::hex << page_offset << std::dec
        << " bytes=" << bytes << " packets=" << packets << " traps=" << traps
        << " retired=" << machine.hart().retired() << '\n';
    if (!destination.empty()) {
        std::filesystem::create_directories(destination);
        write(destination / "program.bin", boot.bytes()); write(destination / "input.bin", initial);
        write(destination / "expected.bin", expected);
        std::ofstream reference(destination / "reference.json");
        reference << "{\"vector_bytes\":64,\"memory_bytes\":" << memory_bytes
            << ",\"pc\":" << machine.hart().pc().value() << ",\"retired\":" << machine.hart().retired()
            << ",\"traps\":" << traps << ",\"bytes\":" << bytes << ",\"transactions\":" << packets << "}\n";
        reference.close(); require(bool(reference), "reference manifest write");
    }
}
}
int main(int argc, char** argv) {
    try {
        std::filesystem::path destination;
        if (argc == 2 && std::string_view{argv[1]}.starts_with("--export=")) {
            destination = std::string_view{argv[1]}.substr(9);
            require(!destination.empty(), "empty export directory");
        } else require(argc == 1, "usage: holon_npu_system_memory_test [--export=directory]");
        for (const auto offset : {0xff0u, 0xff3u, 0xffcu})
            run_case(offset, destination.empty() ? destination : destination / std::to_string(offset));
        std::cout << "PASS: external fetch, page/packet tails, scalar memory and guest trap/MRET\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
