#pragma once

#include "holon_npu_semantic.hpp"

#include <cstdint>

namespace holon_npu::gem5_model {

struct timing_parameters {
    std::uint32_t frontend_cycles = 1;
    std::uint32_t scalar_local_cycles = 2;
    std::uint32_t vector_issue_cycles = 1;
    std::uint32_t vector_lanes = 16;
    std::uint32_t quant_parameter_words = 6;
    std::uint32_t matrix_descriptor_words = 8;
    std::uint32_t matrix_tile_m = 16;
    std::uint32_t matrix_array_k = 16;
    std::uint32_t matrix_array_n = 16;
    std::uint32_t matrix_validate_cycles = 1;
    std::uint32_t matrix_clear_cycles = 1;
    std::uint32_t matrix_drain_cycles = 1;
    std::uint32_t dma_setup_cycles = 4;
    std::uint32_t sync_cycles = 1;
    std::uint32_t scratchpad_read_cycles = 2;
    std::uint32_t scratchpad_write_cycles = 2;
    std::uint32_t scratchpad_read_ports = 1;
    std::uint32_t scratchpad_write_ports = 1;
};

struct operation_timing {
    std::uint64_t cycles = 1;
    std::uint64_t active_lanes = 0;
    std::uint64_t available_lanes = 0;
    std::uint64_t matrix_macs = 0;
    std::uint64_t scratchpad_reads = 0;
    std::uint64_t scratchpad_writes = 0;
};

class timing_model {
public:
    explicit timing_model(timing_parameters parameters = {}) : parameters_(parameters) {}

    [[nodiscard]] operation_timing estimate(const semantic::operation& operation) const;
    [[nodiscard]] const timing_parameters& parameters() const { return parameters_; }

private:
    timing_parameters parameters_;
};

}  // namespace holon_npu::gem5_model
