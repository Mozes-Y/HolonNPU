#pragma once
#include "holon_npu_semantic.hpp"

namespace holon_npu::gem5_model {
struct timing_parameters {
    unsigned frontend_cycles=1, vector_lanes=16, vector_startup=1;
    unsigned divide_cycles=12, sqrt_cycles=16, matrix_rows=16, matrix_cols=16;
    unsigned scratchpad_bytes_per_cycle=4, dma_setup_cycles=4;
};
enum class resource { frontend, local_memory, vector, matrix, memory, sync, memory_wait };
struct operation_timing {
    std::uint64_t cycles{}, active_lanes{}, lane_slots{}, matrix_macs{}, local_bytes{};
    resource unit{};
};
// gem5-only latency accounting, never architectural execution or memory servicing.
class timing_model {
public:
    explicit timing_model(timing_parameters parameters={});
    [[nodiscard]] operation_timing estimate(const semantic::operation& operation) const;
private:
    timing_parameters parameters_;
};
}
