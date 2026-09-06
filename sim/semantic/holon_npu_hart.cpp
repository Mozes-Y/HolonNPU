#include "holon_npu_hart.hpp"

#include <limits>
#include <concepts>
#include <type_traits>

namespace holon_npu::semantic::scalar {
namespace {
using instruction::machine_csr;
constexpr std::uint32_t mie_bit = 1u << 3, mpie_bit = 1u << 7;
constexpr std::uint32_t cy_bit = 1, ir_bit = 4, interrupt_bit = 1u << 31;
constexpr auto csr_spec(machine_csr address) {
    return std::ranges::find(instruction::machine_csrs, address, &instruction::machine_csr_spec::address);
}
constexpr auto csr_index(machine_csr address) {
    return static_cast<std::size_t>(csr_spec(address) - instruction::machine_csrs.begin());
}
constexpr bool zero_csr(std::uint16_t address) {
    return std::ranges::any_of(instruction::machine_zero_csr_ranges, [address](auto r) {
        return address >= r.first && address <= r.last;
    });
}
constexpr auto cause(scalar_trap_cause value) { return static_cast<std::uint32_t>(value); }
}

hart_state::hart_state() { reset(); }

void hart_state::reset() {
    registers_.fill(0);
    pc_ = 0;
    retired_ = cycle_ = instret_ = 0;
    for (std::size_t i = 0; i < csrs_.size(); ++i) csrs_[i] = instruction::machine_csrs[i].reset;
    waiting_ = false;
    pending_.reset();
    // Tokens survive external reset so delayed callbacks cannot complete new work.
}

std::expected<void, hart_error> hart_state::start(instruction_address entry) {
    if (pending_) return std::unexpected(hart_error::operation_pending);
    if (entry.value() % instruction::alignment_bytes) return std::unexpected(hart_error::invalid_entry);
    pc_ = entry.value();
    waiting_ = false;
    return {};
}

std::uint32_t hart_state::reg(scalar_register index) const {
    return index.value() == 0 ? 0 : std::bit_cast<std::uint32_t>(registers_.at(index.value()));
}

std::uint32_t& hart_state::csr(machine_csr address) { return csrs_.at(csr_index(address)); }
std::uint32_t hart_state::csr(machine_csr address) const { return csrs_.at(csr_index(address)); }

std::expected<std::uint32_t, hart_error> hart_state::read_csr(csr_address address) const {
    const auto id = static_cast<machine_csr>(address.value());
    if (csr_spec(id) == instruction::machine_csrs.end()) {
        if (zero_csr(address.value())) return 0;
        return std::unexpected(hart_error::unknown_csr);
    }
    switch (id) {
    case machine_csr::mcycle: return static_cast<std::uint32_t>(cycle_);
    case machine_csr::mcycleh: return static_cast<std::uint32_t>(cycle_ >> 32);
    case machine_csr::minstret: return static_cast<std::uint32_t>(instret_);
    case machine_csr::minstreth: return static_cast<std::uint32_t>(instret_ >> 32);
    default: return csr(id);
    }
}

void hart_state::write_csr(machine_csr address, std::uint32_t value) {
    const auto spec = csr_spec(address);
    if (spec == instruction::machine_csrs.end()) return; // Zero HPM fields.
    value = (value & spec->write_mask) | (spec->reset & ~spec->write_mask);
    const auto low = [value](std::uint64_t& counter) { counter = (counter & 0xffffffff00000000ull) | value; };
    const auto high = [value](std::uint64_t& counter) { counter = (counter & 0xffffffffull) | (std::uint64_t{value} << 32); };
    switch (address) {
    case machine_csr::mcycle: low(cycle_); break;
    case machine_csr::mcycleh: high(cycle_); break;
    case machine_csr::minstret: low(instret_); break;
    case machine_csr::minstreth: high(instret_); break;
    case machine_csr::mip: break; // External level inputs, not software latches.
    case machine_csr::mtvec: csr(address) = (value & ~3u) | ((value & 3u) == 1 ? 1u : 0u); break;
    default: csr(address) = value;
    }
}

void hart_state::account_cycles(elapsed_cycles elapsed) {
    if (!(csr(machine_csr::mcountinhibit) & cy_bit)) cycle_ += elapsed.value();
}

void hart_state::retire(bool count) {
    ++retired_;
    if (count && !(csr(machine_csr::mcountinhibit) & ir_bit)) ++instret_;
}

committed hart_state::commit(instruction_address next_pc, bool count) {
    const auto old = pc();
    pc_ = next_pc.value();
    retire(count);
    return {old, next_pc, retired_};
}

void hart_state::write_register(const register_result& result) {
    if (result.write && result.write->destination.value() != 0)
        registers_.at(result.write->destination.value()) = std::bit_cast<std::int32_t>(result.write->value);
}

trap_taken hart_state::enter_trap(std::uint32_t trap_cause, std::uint32_t value) {
    const auto old = pc();
    csr(machine_csr::mepc) = pc_;
    csr(machine_csr::mcause) = trap_cause;
    csr(machine_csr::mtval) = value;
    auto& status = csr(machine_csr::mstatus);
    status = (status & ~(mie_bit | mpie_bit)) | ((status & mie_bit) ? mpie_bit : 0);
    const auto vector = csr(machine_csr::mtvec);
    pc_ = (vector & ~3u) + (((trap_cause & interrupt_bit) && (vector & 3u) == 1)
        ? 4u * (trap_cause & ~interrupt_bit) : 0u);
    waiting_ = false;
    return {old, pc(), trap_cause, value};
}

void hart_state::set_interrupts(interrupt_lines lines) {
    csr(machine_csr::mip) = (lines.software ? 1u << 3 : 0)
        | (lines.timer ? 1u << 7 : 0) | (lines.external ? 1u << 11 : 0);
}

std::expected<std::optional<trap_taken>, hart_error> hart_state::poll_interrupt() {
    if (pending_) return std::unexpected(hart_error::operation_pending);
    const auto enabled = csr(machine_csr::mip) & csr(machine_csr::mie);
    if (enabled) waiting_ = false;
    if (enabled && (csr(machine_csr::mstatus) & mie_bit)) {
        const auto code = (enabled & (1u << 11)) ? 11u : ((enabled & (1u << 3)) ? 3u : 7u);
        return enter_trap(interrupt_bit | code, 0);
    }
    return std::nullopt;
}

std::expected<trap_taken, hart_error> hart_state::fetch_fault() {
    if (pending_) return std::unexpected(hart_error::operation_pending);
    return enter_trap(cause(scalar_trap_cause::instruction_access_fault), pc_);
}

std::expected<hart_event, hart_error> hart_state::issue(instruction::scalar_word word) {
    const auto interrupt = poll_interrupt();
    if (!interrupt) return std::unexpected(interrupt.error());
    if (*interrupt) return **interrupt;
    if (waiting_) return sleeping{};
    const auto rs1 = scalar_register{static_cast<std::uint8_t>((word.bits >> instruction::rs1_shift) & (instruction::register_count - 1))};
    const auto rs2 = scalar_register{static_cast<std::uint8_t>((word.bits >> instruction::rs2_shift) & (instruction::register_count - 1))};
    const auto evaluated = evaluate(word, pc(), {reg(rs1), reg(rs2)});
    if (!evaluated) return enter_trap(cause(evaluated.error().cause), evaluated.error().value);
    return std::visit([&](const auto& effect) -> std::expected<hart_event, hart_error> {
        using T = std::remove_cvref_t<decltype(effect)>;
        if constexpr (std::same_as<T, register_result>) {
            write_register(effect);
            return commit(evaluated->next_pc);
        } else if constexpr (std::same_as<T, csr_request>) {
            const auto previous = read_csr(effect.address);
            if (!previous || (effect.write && (effect.address.value() >> 10) == 3))
                return enter_trap(cause(scalar_trap_cause::illegal_instruction), word.bits);
            const auto address = static_cast<machine_csr>(effect.address.value());
            const auto value = effect.action == csr_action::replace ? effect.source
                : (effect.action == csr_action::set ? *previous | effect.source : *previous & ~effect.source);
            if (effect.read) write_register({register_write{effect.destination, *previous}});
            const bool counter_write = effect.write && (address == machine_csr::minstret || address == machine_csr::minstreth);
            auto event = commit(evaluated->next_pc, !counter_write);
            if (effect.write) write_csr(address, value);
            return event;
        } else if constexpr (std::same_as<T, machine_request>) {
            if (effect == machine_request::return_from_trap) {
                auto& status = csr(machine_csr::mstatus);
                status = (status & ~mie_bit) | ((status & mpie_bit) ? mie_bit : 0) | mpie_bit;
                return commit(instruction_address{csr(machine_csr::mepc)});
            }
            waiting_ = true;
            return commit(evaluated->next_pc);
        } else {
            if (next_token_ == std::numeric_limits<std::uint64_t>::max())
                return std::unexpected(hart_error::token_exhausted);
            pending_effect event{hart_token{next_token_++}, pc(), effect};
            pending_ = pending_context{event, evaluated->next_pc};
            return event;
        }
    }, evaluated->value);
}

std::expected<hart_event, hart_error> hart_state::complete(hart_token token, external_result result) {
    if (!pending_) return std::unexpected(hart_error::no_pending);
    if (pending_->event.token != token) return std::unexpected(hart_error::wrong_token);
    const auto& request = pending_->event.request;
    if (std::holds_alternative<access_fault>(result)) {
        if (std::holds_alternative<fence_request>(request)) return std::unexpected(hart_error::invalid_result);
        const auto load = std::get_if<load_request>(&request);
        const auto address = load ? load->address : std::get<store_request>(request).address;
        const auto exception = load ? scalar_trap_cause::load_access_fault : scalar_trap_cause::store_access_fault;
        pending_.reset();
        return enter_trap(cause(exception), address.value());
    }
    register_result write;
    if (const auto* load = std::get_if<load_request>(&request)) {
        const auto* data = std::get_if<load_data>(&result);
        if (!data) return std::unexpected(hart_error::invalid_result);
        const auto completed = complete_load(*load, data->bytes);
        if (!completed) return std::unexpected(hart_error::invalid_result);
        write = *completed;
    } else if (!std::holds_alternative<acknowledged>(result)) {
        return std::unexpected(hart_error::invalid_result);
    }
    const auto next_pc = pending_->next_pc;
    write_register(write);
    pending_.reset();
    return commit(next_pc);
}

} // namespace holon_npu::semantic::scalar
