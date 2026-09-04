#include "holon_npu_timing.hpp"

#include <cstdint>
#include <iostream>

int main() {
    using namespace holon_npu;
    const gem5_model::timing_model timing;

    const semantic::vector_operation vector{
        .instruction = {.isa_class = HOLON_NPU_ISA_ENUM_VECTOR_ALU},
        .vl = 4,
        .active_lanes = 4,
        .element_bytes = 4,
    };
    const auto vector_result = timing.estimate(vector);
    if (vector_result.cycles != 1 || vector_result.active_lanes != 4 ||
        vector_result.available_lanes != 16) {
        std::cerr << "vector timing mismatch\n";
        return 1;
    }

    auto vector_memory = vector;
    vector_memory.instruction.isa_class = HOLON_NPU_ISA_ENUM_VECTOR_MEMORY;
    vector_memory.instruction.opcode = static_cast<std::uint8_t>(
        semantic::instruction_opcode::vector_memory_load
    );
    const auto vector_load = timing.estimate(vector_memory);
    if (vector_load.cycles != 9 || vector_load.scratchpad_reads != 4 ||
        vector_load.scratchpad_writes != 0) {
        std::cerr << "vector load accounting mismatch\n";
        return 1;
    }
    vector_memory.instruction.opcode = static_cast<std::uint8_t>(
        semantic::instruction_opcode::vector_memory_store
    );
    const auto vector_store = timing.estimate(vector_memory);
    if (vector_store.cycles != 9 || vector_store.scratchpad_reads != 0 ||
        vector_store.scratchpad_writes != 4) {
        std::cerr << "vector store accounting mismatch\n";
        return 1;
    }

    const semantic::matrix_operation matrix{
        .command = {
            .m = 16,
            .n = 16,
            .k = 16,
            .store_result = true,
        },
    };
    const auto matrix_result = timing.estimate(matrix);
    if (matrix_result.matrix_macs != 4096 || matrix_result.cycles != 1619) {
        std::cerr << "matrix timing mismatch: " << matrix_result.cycles << '\n';
        return 1;
    }
    return 0;
}
