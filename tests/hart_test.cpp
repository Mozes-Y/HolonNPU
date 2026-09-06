#include "holon_npu_hart.hpp"

#include <format>
#include <iostream>
#include <random>
#include <source_location>
#include <stdexcept>

namespace {
using namespace holon_npu::semantic;
using namespace holon_npu::semantic::instruction;
using namespace holon_npu::semantic::scalar;

void require(bool ok, std::string_view message,
             std::source_location location = std::source_location::current()) {
    if (!ok) throw std::runtime_error(std::format("{}:{}: {}", location.file_name(), location.line(), message));
}
template<class T> T event(std::expected<hart_event, hart_error> result) {
    require(result && std::holds_alternative<T>(*result), "expected hart event");
    return std::get<T>(*result);
}
template<class T> void expect_error(const std::expected<T, hart_error>& result, hart_error error) {
    require(!result && result.error() == error, "expected API error without mutation");
}
std::uint32_t read(const hart_state& hart, unsigned address) {
    const auto result = hart.read_csr(csr_address{static_cast<std::uint16_t>(address)});
    require(result.has_value(), "CSR readable");
    return *result;
}
scalar_word csr_word(unsigned address, unsigned funct3, unsigned rd, unsigned source) {
    return {address << 20 | source << 15 | funct3 << 12 | rd << 7 | 0x73};
}
void constant(hart_state& hart, unsigned reg, std::uint32_t value) {
    const auto upper = (value + 0x800u) & 0xfffff000u;
    event<committed>(hart.issue({upper | reg << 7 | 0x37}));
    event<committed>(hart.issue({(value & 4095) << 20 | reg << 15 | reg << 7 | 0x13}));
    require(hart.reg(scalar_register{static_cast<std::uint8_t>(reg)}) == value, "constant via standard instructions");
}
void write(hart_state& hart, unsigned address, std::uint32_t value) {
    constant(hart, 1, value);
    event<committed>(hart.issue(csr_word(address, 1, 0, 1)));
}

void csr_inventory() {
    hart_state hart;
    for (const auto spec : machine_csrs)
        require(read(hart, static_cast<unsigned>(spec.address)) == spec.reset, "schema CSR reset");
    for (unsigned address = 0; address < 4096; ++address) {
        hart.reset();
        constexpr std::array standard_addresses{0x300u, 0x301u, 0x304u, 0x305u, 0x310u,
            0x320u, 0x340u, 0x341u, 0x342u, 0x343u, 0x344u, 0xb00u, 0xb02u, 0xb80u,
            0xb82u, 0xf11u, 0xf12u, 0xf13u, 0xf14u, 0xf15u};
        const bool implemented = std::ranges::contains(standard_addresses, address)
            || (address >= 0x323 && address <= 0x33f) || (address >= 0xb03 && address <= 0xb1f)
            || (address >= 0xb83 && address <= 0xb9f);
        require(hart.read_csr(csr_address{static_cast<std::uint16_t>(address)}).has_value() == implemented,
            "independent standard CSR inventory");
        const auto result = hart.issue(csr_word(address, 2, 2, 0));
        if (implemented) event<committed>(result);
        else {
            const auto trap = event<trap_taken>(result);
            require(trap.cause == 2 && trap.value == csr_word(address, 2, 2, 0).bits
                && hart.retired() == 0 && hart.reg(scalar_register{2}) == 0, "unknown CSR precise illegal trap");
        }
    }
    for (const auto range : machine_zero_csr_ranges) {
        for (unsigned address = range.first; address <= range.last; ++address) {
            hart.reset();
            write(hart, address, 0xffffffff);
            require(read(hart, address) == 0, "HPM fields remain zero");
        }
    }
    hart.reset();
    for (unsigned address = 0xf11; address <= 0xf15; ++address) {
        // A nonzero source register containing zero still has CSR write intent.
        event<committed>(hart.issue(csr_word(address, 2, 2, 0)));
        const auto total = hart.retired();
        const auto trap = event<trap_taken>(hart.issue(csr_word(address, 2, 2, 1)));
        require(trap.cause == 2 && hart.retired() == total, "read-only CSR write intent traps");
    }
    write(hart, 0x301, 0xffffffff);
    require(read(hart, 0x301) == 0x40001100, "MISA remains RV32IM without C");
    write(hart, 0x300, 0xffffffff);
    require(read(hart, 0x300) == 0x1888, "MSTATUS WARL M-only");
    write(hart, 0x304, 0xffffffff);
    require(read(hart, 0x304) == 0x888, "MIE unsupported sources zero");
    write(hart, 0x341, 0xffffffff);
    require(read(hart, 0x341) == 0xfffffffc, "MEPC IALIGN32");
    for (unsigned mode = 0; mode < 4; ++mode) {
        write(hart, 0x305, 0x100 | mode);
        require(read(hart, 0x305) == (mode == 1 ? 0x101u : 0x100u), "MTVEC WARL");
    }
    hart.reset();
    hart.set_interrupts({true, true, true});
    write(hart, 0x344, 0);
    require(read(hart, 0x344) == 0x888, "MIP write cannot clear physical levels");
}

void register_random() {
    hart_state hart;
    std::array<std::uint32_t, 32> reference{};
    std::mt19937 rng{0x52454732};
    for (unsigned step = 0; step < 8192; ++step) {
        const auto rd = rng() % 32, rs = rng() % 32;
        const auto immediate = static_cast<std::int32_t>(rng() % 4096) - 2048;
        const auto value = reference[rs] + static_cast<std::uint32_t>(immediate);
        event<committed>(hart.issue({(static_cast<std::uint32_t>(immediate) & 4095) << 20
            | static_cast<std::uint32_t>(rs << 15 | rd << 7) | 0x13u}));
        if (rd) reference[rd] = value;
        for (std::uint8_t reg = 0; reg < 32; ++reg)
            require(hart.reg(scalar_register{reg}) == reference[reg], "all scalar registers independent scoreboard");
        require(hart.retired() == step + 1 && hart.pc().value() == (step + 1) * 4, "precise sequential retirement");
    }
    const auto pc = hart.pc();
    expect_error(hart.start(instruction_address{2}), hart_error::invalid_entry);
    require(hart.pc() == pc, "invalid entry transactional");
    require(hart.start(instruction_address{0xfffffffcu}).has_value(), "last aligned address");
    event<committed>(hart.issue({0x13}));
    require(hart.pc().value() == 0, "PC wraps XLEN");
}

void csr_random() {
    hart_state hart;
    std::mt19937 rng{0x43535232};
    std::uint32_t reference = 0;
    for (unsigned i = 0; i < 8192; ++i) {
        const auto source = static_cast<std::uint32_t>(rng());
        const auto action = 1u + rng() % 3;
        const bool immediate = rng() & 1;
        const auto operand = immediate ? source & 31 : source;
        if (!immediate) constant(hart, 1, operand);
        const auto before = hart.retired();
        event<committed>(hart.issue(csr_word(0x340, action + (immediate ? 4 : 0), 1, immediate ? operand : 1)));
        require(hart.reg(scalar_register{1}) == reference, "aliased CSR rd gets old value");
        reference = action == 1 ? operand : (action == 2 ? reference | operand : reference & ~operand);
        require(read(hart, 0x340) == reference && hart.retired() == before + 1, "CSR independent random scoreboard");
    }
    write(hart, 0xb00, 0xffffffff);
    write(hart, 0xb80, 3);
    hart.account_cycles(elapsed_cycles{2});
    require(read(hart, 0xb00) == 1 && read(hart, 0xb80) == 4, "64-bit cycle carry");
    write(hart, 0xb02, 0xffffffff);
    event<committed>(hart.issue({0x13}));
    require(read(hart, 0xb02) == 0 && read(hart, 0xb82) == 1, "64-bit instret carry");
    constant(hart, 1, 7);
    const auto before = read(hart, 0xb02);
    const auto total = hart.retired();
    event<committed>(hart.issue(csr_word(0xb02, 1, 2, 1)));
    require(read(hart, 0xb02) == 7 && hart.reg(scalar_register{2}) == before
        && hart.retired() == total + 1, "counter write suppresses implicit increment, not budget count");
    constant(hart, 1, 5);
    const auto old = read(hart, 0xb02);
    event<committed>(hart.issue(csr_word(0x320, 1, 0, 1)));
    require(read(hart, 0xb02) == old + 1, "inhibit takes effect after its writing instruction");
    hart.account_cycles(elapsed_cycles{100});
    event<committed>(hart.issue({0x13}));
    require(read(hart, 0xb00) == 1 && read(hart, 0xb02) == old + 1, "CY/IR inhibition");
    event<committed>(hart.issue(csr_word(0x320, 1, 0, 0)));
    require(read(hart, 0xb02) == old + 1, "unfreeze instruction uses old inhibition");
    event<committed>(hart.issue({0x13}));
    require(read(hart, 0xb02) == old + 2, "counter resumes");
}

void exceptions_and_interrupts() {
    hart_state hart;
    write(hart, 0x305, 0x101);
    write(hart, 0x300, 8);
    require(hart.start(instruction_address{0x80001000}).has_value(), "start physical PC");
    const auto total = hart.retired();
    const auto trap = event<trap_taken>(hart.issue({0x73}));
    require(trap.cause == 11 && trap.value == 0 && trap.handler.value() == 0x100
        && read(hart, 0x341) == 0x80001000 && read(hart, 0x300) == 0x1880
        && hart.retired() == total, "ECALL enters direct trap even for vectored MTVEC");
    write(hart, 0x341, 0x80001004);
    const auto ret = event<committed>(hart.issue({0x30200073}));
    require(ret.next_pc.value() == 0x80001004 && read(hart, 0x300) == 0x1888, "MRET restores PC and MIE");
    const auto breakpoint = event<trap_taken>(hart.issue({0x00100073}));
    require(breakpoint.cause == 3 && breakpoint.value == 0x80001004, "EBREAK informative tval");
    require(hart.start(instruction_address{0x80002000}).has_value(), "fetch fault start");
    const auto fetch = hart.fetch_fault();
    require(fetch && fetch->cause == 1 && fetch->value == 0x80002000, "fetch access fault");

    for (unsigned mask = 1; mask < 8; ++mask) {
        hart.reset();
        write(hart, 0x305, 0x101);
        write(hart, 0x304, 0x888);
        write(hart, 0x300, 8);
        const auto pc = hart.pc();
        const auto count = hart.retired();
        hart.set_interrupts({bool(mask & 1), bool(mask & 2), bool(mask & 4)});
        const auto taken = event<trap_taken>(hart.issue({0x13}));
        const auto code = mask & 4 ? 11u : (mask & 1 ? 3u : 7u);
        require(taken.cause == (0x80000000u | code) && taken.handler.value() == 0x100 + code * 4
            && taken.pc == pc && hart.retired() == count, "interrupt priority and vectored boundary");
    }
    hart.reset();
    write(hart, 0x304, 0x80);
    event<committed>(hart.issue({0x10500073}));
    const auto pc = hart.pc();
    const auto count = hart.retired();
    event<sleeping>(hart.issue({0x13}));
    require(hart.pc() == pc && hart.retired() == count, "WFI wait is not another retirement");
    hart.set_interrupts({true, false, false});
    event<sleeping>(hart.issue({0x13}));
    hart.set_interrupts({false, true, false});
    event<committed>(hart.issue({0x13}));
    require(!hart.waiting() && read(hart, 0x342) == 0, "local enable wakes WFI with global MIE off");
}

void pending_protocol() {
    hart_state hart;
    constant(hart, 1, 0x80001000);
    constant(hart, 2, 0xdeadbeef);
    const auto pc = hart.pc();
    const auto count = hart.retired();
    const auto load = event<pending_effect>(hart.issue({0x0000a103})); // lw x2,0(x1)
    require(std::get<load_request>(load.request).address.value() == 0x80001000
        && hart.pc() == pc && hart.retired() == count, "physical load captures, does not retire");
    expect_error(hart.issue({0x13}), hart_error::operation_pending);
    expect_error(hart.poll_interrupt(), hart_error::operation_pending);
    expect_error(hart.fetch_fault(), hart_error::operation_pending);
    require(!hart.start(instruction_address{4}), "start rejects pending effect");
    expect_error(hart.complete(hart_token{load.token.value() + 1}, acknowledged{}), hart_error::wrong_token);
    expect_error(hart.complete(load.token, acknowledged{}), hart_error::invalid_result);
    const std::array bytes{std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}};
    require(!hart.complete(load.token, load_data{std::span{bytes}.first(3)}), "short payload rejection");
    require(hart.pc() == pc && hart.retired() == count && hart.reg(scalar_register{2}) == 0xdeadbeef, "rejected completions transactional");
    event<committed>(hart.complete(load.token, load_data{bytes}));
    require(hart.pc().value() == pc.value() + 4 && hart.retired() == count + 1
        && hart.reg(scalar_register{2}) == 0x12345678, "successful load commit");
    expect_error(hart.complete(load.token, load_data{bytes}), hart_error::no_pending);
    const auto store = event<pending_effect>(hart.issue({0x0020a023})); // sw x2,0(x1)
    require(std::get<store_request>(store.request).payload == bytes, "stable store payload");
    require(!hart.complete(store.token, load_data{bytes}), "store rejects read payload");
    const auto fault_pc = hart.pc();
    const auto fault_count = hart.retired();
    const auto failed = event<trap_taken>(hart.complete(store.token, access_fault{}));
    require(failed.cause == 7 && failed.pc == fault_pc && failed.value == 0x80001000
        && hart.retired() == fault_count, "store access fault precise");
    const auto fence = event<pending_effect>(hart.issue({0x0ff0000f}));
    require(!hart.complete(fence.token, access_fault{}), "fence cannot invent memory fault");
    event<committed>(hart.complete(fence.token, acknowledged{}));
    const auto canceled = event<pending_effect>(hart.issue({0x0000a003})); // lw x0,0(x1)
    hart.reset();
    const auto replacement = event<pending_effect>(hart.issue({0x00002003}));
    require(replacement.token != canceled.token && !hart.complete(canceled.token, load_data{bytes}), "reset cannot reuse tokens");
    const auto load_fault = event<trap_taken>(hart.complete(replacement.token, access_fault{}));
    require(load_fault.cause == 5 && hart.retired() == 0, "load to x0 still faults");

    hart.reset();
    write(hart, 0x304, 0x800);
    write(hart, 0x300, 8);
    const auto inflight = event<pending_effect>(hart.issue({0x00002083}));
    hart.set_interrupts({false, false, true});
    const auto finished = event<committed>(hart.complete(inflight.token, load_data{bytes}));
    const auto interrupt = event<trap_taken>(hart.issue({0x13}));
    require(interrupt.pc == finished.next_pc && hart.reg(scalar_register{1}) == 0x12345678,
        "accepted load commits before interrupt at next boundary");
    hart.reset();
    constant(hart, 2, 0xcafebabe);
    const auto successful_store = event<pending_effect>(hart.issue({0x00202023}));
    const auto store_count = hart.retired();
    event<committed>(hart.complete(successful_store.token, acknowledged{}));
    require(hart.retired() == store_count + 1 && !hart.pending(), "store acknowledgement retires exactly once");
    constant(hart, 1, 3);
    const auto misaligned = event<trap_taken>(hart.issue({0x0000a103}));
    require(misaligned.cause == 4 && misaligned.value == 3 && !hart.pending(), "misaligned load traps without issuing");
}
}

int main() {
    try {
        csr_inventory();
        register_random();
        csr_random();
        exceptions_and_interrupts();
        pending_protocol();
        std::cout << "hart: 4096 CSR addresses, 8192 register steps seed=0x52454732, "
                     "8192 random CSR steps seed=0x43535232, "
                     "counters, M-mode traps/interrupts/WFI, precise pending completion passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
