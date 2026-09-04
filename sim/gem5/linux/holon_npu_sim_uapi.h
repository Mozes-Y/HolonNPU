#pragma once

#include <linux/types.h>

enum holon_npu_sim_uapi_constant {
    HOLON_NPU_SIM_DMA_BUFFER_BYTES = 2 * 1024 * 1024,
};

struct holon_npu_sim_snapshot {
    __u32 status;
    __u32 fault_code;
    __u32 debug_pc;
    __u32 irq_status;
    __u64 cycles;
    __u64 instructions_retired;
    __u64 dma_address;
    __u64 dma_bytes;
};
