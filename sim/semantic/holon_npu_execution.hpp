#pragma once

#include "holon_npu_semantic.hpp"

namespace holon_npu::semantic {

struct system_memory_view {
    system_address base{};
    std::span<std::byte> bytes;
};

struct instruction_budget_exhausted {};
using execution_error = std::variant<api_error, instruction_budget_exhausted>;

[[nodiscard]] operation_result service_operation(
    const operation& request,
    system_memory_view memory
);

[[nodiscard]] std::expected<run_result, execution_error> run_program(
    program_machine& machine,
    system_memory_view memory,
    std::uint64_t instruction_budget
);

}  // namespace holon_npu::semantic
