#include "holon_npu_execution.hpp"

#include <algorithm>
#include <concepts>
#include <limits>

namespace holon_npu::semantic {
namespace {

std::optional<std::span<std::byte>> mapped_range(
    system_memory_view memory,
    system_address address,
    std::size_t byte_count
) {
    if (address < memory.base ||
        (byte_count != 0 && byte_count - 1 >
            std::numeric_limits<std::uint64_t>::max() - address.value())) {
        return std::nullopt;
    }
    const auto offset = address.value() - memory.base.value();
    if (offset > memory.bytes.size() || byte_count > memory.bytes.size() - offset) {
        return std::nullopt;
    }
    return memory.bytes.subspan(static_cast<std::size_t>(offset), byte_count);
}

operation_result read_memory(system_memory_view memory, system_address address, std::size_t size) {
    if (const auto range = mapped_range(memory, address, size)) {
        return read_payload{{range->begin(), range->end()}};
    }
    return operation_failure{architectural_fault::axi_read};
}

operation_result write_memory(
    system_memory_view memory,
    system_address address,
    std::span<const std::byte> bytes
) {
    if (const auto range = mapped_range(memory, address, bytes.size())) {
        std::ranges::copy(bytes, range->begin());
        return operation_success{};
    }
    return operation_failure{architectural_fault::axi_write};
}

}  // namespace

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
