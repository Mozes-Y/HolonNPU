#pragma once

#include "holon_npu_semantic.hpp"

namespace holon_npu::semantic {

[[nodiscard]] std::expected<instruction::instruction_frame, scalar::trap> fetch_instruction(
    const memory::physical_map& map, memory::bindings memory, instruction_address pc);
[[nodiscard]] std::expected<scalar::hart_event, scalar::hart_error> service_scalar(
    scalar::hart_state& hart, const memory::physical_map& map, memory::bindings memory);

// Services the live request only. The environment never interprets instructions.
[[nodiscard]] std::expected<execution_event, api_error> service_operation(
    program_machine& machine, system_memory_view system);
enum class run_reason { stopped, waiting, budget };
struct run_report { run_reason reason{}; std::uint64_t retired{}, traps{}; std::uint32_t status{}; };
[[nodiscard]] std::expected<run_report, api_error> run_program(
    program_machine& machine, system_memory_view memory, std::uint64_t instruction_budget);

} // namespace holon_npu::semantic
