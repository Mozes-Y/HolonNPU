#include "holon_npu_execution.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <limits>
#include <type_traits>

namespace holon_npu::semantic {

std::expected<instruction::instruction_frame, scalar::trap> fetch_instruction(
    const memory::physical_map& map, memory::bindings memory, instruction_address pc) {
    using enum instruction::scalar_trap_cause;
    if (pc.value() % instruction::alignment_bytes)
        return std::unexpected(scalar::trap{instruction_address_misaligned, pc, pc.value()});
    std::array<std::byte, instruction::holon_bytes> bytes{};
    if (!map.read(physical_address{pc.value()}, std::span{bytes}.first(instruction::scalar_bytes), memory, memory::access::execute))
        return std::unexpected(scalar::trap{instruction_access_fault, pc, pc.value()});
    const auto first = instruction::fetch(std::span{bytes}.first(instruction::scalar_bytes), pc, pc);
    if (first) return *first;
    if (pc.value() > std::numeric_limits<std::uint32_t>::max() - (instruction::holon_bytes - 1))
        return std::unexpected(scalar::trap{instruction_access_fault, pc, pc.value()});
    const auto second = pc.value() + instruction::scalar_bytes;
    if (!map.read(physical_address{second}, std::span{bytes}.subspan(instruction::scalar_bytes), memory, memory::access::execute))
        return std::unexpected(scalar::trap{instruction_access_fault, pc, second});
    return *instruction::fetch(bytes, pc, pc);
}

std::expected<scalar::hart_event, scalar::hart_error> service_scalar(
    scalar::hart_state& hart, const memory::physical_map& map, memory::bindings memory) {
    const auto pending = hart.pending_request();
    if (!pending) return std::unexpected(scalar::hart_error::no_pending);
    return std::visit([&](const auto& request) -> std::expected<scalar::hart_event, scalar::hart_error> {
        using T = std::remove_cvref_t<decltype(request)>;
        if constexpr (std::same_as<T, scalar::load_request>) {
            std::array<std::byte, 4> bytes{};
            const auto payload = std::span{bytes}.first(static_cast<std::size_t>(request.width));
            if (!map.read(request.address, payload, memory))
                return hart.complete(pending->token, scalar::access_fault{});
            return hart.complete(pending->token, scalar::load_data{payload});
        } else if constexpr (std::same_as<T, scalar::store_request>) {
            const auto payload = std::span{request.payload}.first(static_cast<std::size_t>(request.width));
            if (!map.write(request.address, payload, memory))
                return hart.complete(pending->token, scalar::access_fault{});
            return hart.complete(pending->token, scalar::acknowledged{});
        } else {
            static_assert(std::same_as<T, scalar::fence_request>);
            // This environment completes every prior memory request synchronously.
            return hart.complete(pending->token, scalar::acknowledged{});
        }
    }, pending->request);
}

std::expected<execution_event, api_error> service_operation(program_machine& machine, system_memory_view memory) {
    const auto pending = machine.pending();
    if (!pending) return std::unexpected(api_error::no_pending);
    const auto* request = std::get_if<memory_request>(&pending->value);
    if (!request || request->storage != memory::storage::system) return machine.complete(pending->token, operation_success{});
    const auto range = memory.range(system_address{request->address.value()}, request->size);
    if (!range) return machine.complete(pending->token, bus_fault{});
    if (request->access == memory::access::write) {
        std::ranges::copy(request->payload, range->begin());
        return machine.complete(pending->token, operation_success{});
    }
    return machine.complete(pending->token, read_payload{{range->begin(), range->end()}});
}

std::expected<run_report, api_error> run_program(program_machine& machine, system_memory_view memory, std::uint64_t budget) {
    const auto initial = machine.hart().retired();
    std::uint64_t traps{};
    for (;;) {
        if (machine.done()) return run_report{run_reason::stopped, machine.hart().retired() - initial, traps, *machine.exit_status()};
        if (machine.hart().retired() - initial + traps >= budget) return run_report{run_reason::budget, machine.hart().retired() - initial, traps};
        auto event = machine.pending() ? service_operation(machine, memory) : machine.advance();
        if (!event) return std::unexpected(event.error());
        if (std::holds_alternative<scalar::trap_taken>(*event)) ++traps;
        if (std::holds_alternative<scalar::sleeping>(*event)) return run_report{run_reason::waiting, machine.hart().retired() - initial, traps};
    }
}

}  // namespace holon_npu::semantic
