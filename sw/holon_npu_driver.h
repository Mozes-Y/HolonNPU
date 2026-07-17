#pragma once

#include <stdint.h>

#include "holon_npu_program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum holon_npu_result {
    HOLON_NPU_OK = 0,
    HOLON_NPU_E_ARG = -1,
    HOLON_NPU_E_BUSY = -2,
    HOLON_NPU_E_TIMEOUT = -3,
    HOLON_NPU_E_STATE = -4
} holon_npu_result_t;

typedef struct holon_npu_dev {
    volatile uint32_t* regs;
} holon_npu_dev_t;

typedef struct holon_npu_caps {
    uint32_t device_id;
    uint32_t abi_version;
    uint32_t isa_version;
    uint64_t capabilities;
    uint64_t operation_classes;
    uint32_t program_mem_bytes;
    uint32_t local_mem_bytes;
    uint32_t vector_cap0;
    uint32_t matrix_cap0;
} holon_npu_caps_t;

typedef struct holon_npu_program_config {
    uint64_t required_caps;
    uint64_t required_op_classes;
    uint64_t code_addr;
    uint32_t code_size_bytes;
    uint32_t entry_pc;
    uint64_t arg_addr;
    uint32_t arg_size_bytes;
    uint32_t local_mem_bytes;
    uint32_t program_mem_bytes;
    uint32_t stack_bytes;
    uint64_t completion_addr;
    uint32_t flags;
} holon_npu_program_config_t;

typedef struct holon_npu_status {
    uint32_t raw;
    int idle;
    int loading;
    int running;
    int halted;
    int done;
    int fault;
    int irq_pending;
} holon_npu_status_t;

typedef struct holon_npu_fault_snapshot {
    uint32_t code;
    uint32_t pc;
} holon_npu_fault_snapshot_t;

typedef struct holon_npu_perf {
    uint64_t cycles;
    uint64_t instructions_retired;
} holon_npu_perf_t;

holon_npu_result_t holon_npu_init(holon_npu_dev_t* dev, volatile void* base);
holon_npu_result_t holon_npu_get_caps(const holon_npu_dev_t* dev, holon_npu_caps_t* caps);
holon_npu_result_t holon_npu_build_program_desc(
    holon_npu_program_desc_t* desc,
    const holon_npu_program_config_t* config
);
holon_npu_result_t holon_npu_submit_program(const holon_npu_dev_t* dev, uint64_t desc_pa);
holon_npu_result_t holon_npu_poll(const holon_npu_dev_t* dev, holon_npu_status_t* status);
holon_npu_result_t holon_npu_wait(
    const holon_npu_dev_t* dev,
    uint32_t timeout_polls,
    holon_npu_status_t* final_status
);
holon_npu_result_t holon_npu_halt(const holon_npu_dev_t* dev);
holon_npu_result_t holon_npu_resume(const holon_npu_dev_t* dev);
holon_npu_result_t holon_npu_debug_step(const holon_npu_dev_t* dev);
holon_npu_result_t holon_npu_soft_reset(const holon_npu_dev_t* dev);
holon_npu_result_t holon_npu_clear_terminal(const holon_npu_dev_t* dev);
holon_npu_result_t holon_npu_get_fault_snapshot(
    const holon_npu_dev_t* dev,
    holon_npu_fault_snapshot_t* snapshot
);
holon_npu_result_t holon_npu_set_irq_enable(const holon_npu_dev_t* dev, uint32_t mask);
holon_npu_result_t holon_npu_get_irq_status(const holon_npu_dev_t* dev, uint32_t* status);
holon_npu_result_t holon_npu_clear_irq(const holon_npu_dev_t* dev, uint32_t mask);
holon_npu_result_t holon_npu_read_perf(const holon_npu_dev_t* dev, holon_npu_perf_t* perf);

#ifdef __cplusplus
}
#endif
