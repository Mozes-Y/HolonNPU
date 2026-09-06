#include "holon_npu_execution.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <limits>
#include <type_traits>

namespace holon_npu::semantic {
namespace {

operation_result read_memory(system_memory_view memory, system_address address, std::size_t size) {
    if (const auto range = memory.range(address, size)) {
        return read_payload{{range->begin(), range->end()}};
    }
    return operation_failure{architectural_fault::axi_read};
}

operation_result write_memory(
    system_memory_view memory,
    system_address address,
    std::span<const std::byte> bytes
) {
    if (const auto range = memory.range(address, bytes.size())) {
        std::ranges::copy(bytes, range->begin());
        return operation_success{};
    }
    return operation_failure{architectural_fault::axi_write};
}

}  // namespace

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

operation_result service_operation(const operation& request, system_memory_view memory) {
    return std::visit([memory]<typename Request>(const Request& value) -> operation_result {
        if constexpr (std::same_as<Request, descriptor_fetch> ||
                      std::same_as<Request, code_fetch> ||
                      std::same_as<Request, argument_fetch>) {
            return read_memory(memory, value.address, value.byte_count);
        } else if constexpr (std::same_as<Request, completion_record_write>) {
            return write_memory(memory, value.address, value.payload);
        } else if constexpr (std::same_as<Request, program_dma_operation>) {
            if (value.direction == dma_direction::system_to_local) {
                return read_memory(memory, value.system, value.byte_count);
            }
            if (value.store_payload.size() != value.byte_count) {
                return operation_failure{architectural_fault::dma_request};
            }
            return write_memory(memory, value.system, value.store_payload);
        } else {
            // Local/engine effects are committed by program_machine, never
            // reinterpreted by this synchronous environment.
            static_assert(std::same_as<Request, scalar_local_operation> ||
                          std::same_as<Request, vector_operation> ||
                          std::same_as<Request, matrix_operation> ||
                          std::same_as<Request, sync_operation>);
            return operation_success{};
        }
    }, request);
}

std::expected<run_result, execution_error> run_program(
    program_machine& machine,
    system_memory_view memory,
    std::uint64_t instruction_budget
) {
    if (machine.state() != lifecycle_state::running &&
        machine.state() != lifecycle_state::done && machine.state() != lifecycle_state::fault) {
        return std::unexpected(execution_error{api_error::invalid_state});
    }
    const auto initial_retirement = machine.retired();
    while (machine.state() != lifecycle_state::done && machine.state() != lifecycle_state::fault) {
        if (machine.retired() - initial_retirement >= instruction_budget) {
            return std::unexpected(execution_error{instruction_budget_exhausted{}});
        }
        auto event = machine.advance();
        if (!event) {
            return std::unexpected(execution_error{event.error()});
        }
        if (const auto* request = std::get_if<pending_operation>(&*event)) {
            event = machine.complete(request->token, service_operation(request->value, memory));
            if (!event) {
                return std::unexpected(execution_error{event.error()});
            }
        }
    }
    return machine.snapshot();
}

}  // namespace holon_npu::semantic
