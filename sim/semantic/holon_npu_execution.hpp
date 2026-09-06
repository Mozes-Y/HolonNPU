#pragma once

#include "holon_npu_semantic.hpp"
#include "holon_npu_memory.hpp"

namespace holon_npu::semantic {

[[nodiscard]] std::expected<instruction::instruction_frame, scalar::trap> fetch_instruction(
    const memory::physical_map& map, memory::bindings memory, instruction_address pc);

// Synchronous environment only; services the live request, never a stale caller copy.
[[nodiscard]] std::expected<scalar::hart_event, scalar::hart_error> service_scalar(
    scalar::hart_state& hart, const memory::physical_map& map, memory::bindings memory);

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
