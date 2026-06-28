#ifndef HOLON_NPU_DRIVER_H
#define HOLON_NPU_DRIVER_H

#include <stdint.h>

#include "holon_npu_desc.h"
#include "holon_npu_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum holon_npu_result {
    HOLON_NPU_OK = 0,
    HOLON_NPU_E_ARG = -1,
    HOLON_NPU_E_BUSY = -2,
    HOLON_NPU_E_TIMEOUT = -3
} holon_npu_result_t;

typedef struct holon_npu_dev {
    volatile uint32_t* regs;
} holon_npu_dev_t;

typedef struct holon_npu_caps {
    uint32_t device_id;
    uint32_t abi_version;
    uint32_t cap0;
    uint32_t cap1;
} holon_npu_caps_t;

typedef struct holon_npu_status {
    uint32_t raw;
    int idle;
    int busy;
    int done;
    int error;
    int irq_pending;
} holon_npu_status_t;

typedef struct holon_npu_perf {
    uint64_t cycles;
    uint64_t busy_cycles;
    uint32_t desc_count;
    uint32_t error_count;
} holon_npu_perf_t;

holon_npu_result_t holon_npu_init(holon_npu_dev_t* dev, volatile void* base);
holon_npu_result_t holon_npu_get_caps(const holon_npu_dev_t* dev, holon_npu_caps_t* caps);
holon_npu_result_t holon_npu_build_gemm_desc(
    holon_npu_gemm_desc_t* desc,
    const holon_npu_gemm_config_t* cfg
);
holon_npu_result_t holon_npu_submit(const holon_npu_dev_t* dev, uint64_t desc_pa);
holon_npu_result_t holon_npu_poll(const holon_npu_dev_t* dev, holon_npu_status_t* status);
holon_npu_result_t holon_npu_wait(
    const holon_npu_dev_t* dev,
    uint32_t timeout_polls,
    holon_npu_status_t* final_status
);
holon_npu_result_t holon_npu_error(const holon_npu_dev_t* dev, uint32_t* error_code);
holon_npu_result_t holon_npu_clear(const holon_npu_dev_t* dev, uint32_t mask);
holon_npu_result_t holon_npu_read_perf(const holon_npu_dev_t* dev, holon_npu_perf_t* perf);

#ifdef __cplusplus
}
#endif

#endif
