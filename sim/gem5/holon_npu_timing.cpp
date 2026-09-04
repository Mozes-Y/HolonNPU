#include "holon_npu_timing.hpp"

#include <algorithm>
#include <type_traits>

namespace holon_npu::gem5_model {
namespace {

std::uint64_t divide_ceil(std::uint64_t numerator, std::uint64_t denominator) {
    return denominator == 0 ? 0 : (numerator + denominator - 1U) / denominator;
}

}  // namespace

operation_timing timing_model::estimate(const semantic::operation& operation) const {
    return std::visit(
        [this](const auto& request) -> operation_timing {
            using request_type = std::remove_cvref_t<decltype(request)>;
            if constexpr (
                std::same_as<request_type, semantic::descriptor_fetch> ||
                std::same_as<request_type, semantic::code_fetch> ||
                std::same_as<request_type, semantic::argument_fetch> ||
                std::same_as<request_type, semantic::completion_record_write>
            ) {
                return {.cycles = parameters_.dma_setup_cycles};
            } else if constexpr (std::same_as<request_type, semantic::scalar_local_operation>) {
                return {
                    .cycles = parameters_.scalar_local_cycles,
                    .scratchpad_reads = request.write ? 0U : 1U,
                    .scratchpad_writes = request.write ? 1U : 0U,
                };
            } else if constexpr (std::same_as<request_type, semantic::vector_operation>) {
                const auto groups = divide_ceil(request.vl, parameters_.vector_lanes);
                const auto memory_class =
                    request.instruction.isa_class == HOLON_NPU_ISA_ENUM_VECTOR_MEMORY;
                const auto memory_opcode = static_cast<semantic::instruction_opcode>(
                    request.instruction.opcode
                );
                const auto vector_load = memory_class &&
                    memory_opcode == semantic::instruction_opcode::vector_memory_load;
                const auto vector_store = memory_class &&
                    memory_opcode == semantic::instruction_opcode::vector_memory_store;
                const auto quant_class =
                    request.instruction.isa_class == HOLON_NPU_ISA_ENUM_QUANTIZATION;
                const auto predicate_load =
                    request.instruction.isa_class == HOLON_NPU_ISA_ENUM_PREDICATE &&
                    request.instruction.opcode == HOLON_NPU_ISA_OPCODE_PREDICATE_LOAD;
                auto cycles = static_cast<std::uint64_t>(parameters_.vector_issue_cycles);
                if (vector_load || vector_store) {
                    const auto active_lanes = std::min(request.active_lanes, request.vl);
                    cycles += active_lanes * (vector_load
                        ? parameters_.scratchpad_read_cycles
                        : parameters_.scratchpad_write_cycles);
                    cycles += request.vl - active_lanes;
                } else if (predicate_load) {
                    cycles += parameters_.scratchpad_read_cycles;
                } else if (quant_class) {
                    cycles += static_cast<std::uint64_t>(parameters_.quant_parameter_words) *
                        parameters_.scratchpad_read_cycles + 1U;
                }
                return {
                    .cycles = cycles,
                    .active_lanes = request.active_lanes,
                    .available_lanes = groups * parameters_.vector_lanes,
                    .scratchpad_reads = vector_load ? request.active_lanes
                        : (predicate_load ? 1U
                           : (quant_class ? parameters_.quant_parameter_words : 0U)),
                    .scratchpad_writes = vector_store ? request.active_lanes : 0U,
                };
            } else if constexpr (std::same_as<request_type, semantic::matrix_operation>) {
                const auto& command = request.command;
                const auto wavefront = static_cast<std::uint64_t>(parameters_.matrix_tile_m) +
                    parameters_.matrix_array_k + parameters_.matrix_array_n - 1U;
                const auto operand_reads =
                    static_cast<std::uint64_t>(command.m) * command.k +
                    static_cast<std::uint64_t>(command.k) * command.n;
                const auto stores = command.store_result
                    ? static_cast<std::uint64_t>(command.m) * command.n
                    : 0U;
                return {
                    .cycles = parameters_.vector_issue_cycles +
                        static_cast<std::uint64_t>(parameters_.matrix_descriptor_words) *
                            parameters_.scratchpad_read_cycles +
                        parameters_.matrix_validate_cycles + parameters_.matrix_clear_cycles +
                        operand_reads * parameters_.scratchpad_read_cycles + command.k +
                        wavefront + parameters_.matrix_drain_cycles +
                        stores * parameters_.scratchpad_write_cycles,
                    .matrix_macs = static_cast<std::uint64_t>(command.m) * command.n * command.k,
                    .scratchpad_reads = parameters_.matrix_descriptor_words + operand_reads,
                    .scratchpad_writes = stores,
                };
            } else if constexpr (std::same_as<request_type, semantic::program_dma_operation>) {
                return {
                    .cycles = parameters_.dma_setup_cycles,
                    .scratchpad_reads = request.direction == semantic::dma_direction::local_to_system
                        ? divide_ceil(request.byte_count, HOLON_NPU_ISA_DMA_WORD_BYTES) : 0U,
                    .scratchpad_writes = request.direction == semantic::dma_direction::system_to_local
                        ? divide_ceil(request.byte_count, HOLON_NPU_ISA_DMA_WORD_BYTES) : 0U,
                };
            } else if constexpr (std::same_as<request_type, semantic::sync_operation>) {
                return {.cycles = parameters_.sync_cycles};
            }
            return {.cycles = parameters_.frontend_cycles};
        },
        operation
    );
}

}  // namespace holon_npu::gem5_model
