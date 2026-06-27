#ifndef MY_NPU_DRIVER_H
#define MY_NPU_DRIVER_H

#include <stdint.h>

#include "my_npu_desc.h"
#include "my_npu_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum my_npu_result {
    MY_NPU_OK = 0,
    MY_NPU_E_ARG = -1,
    MY_NPU_E_BUSY = -2,
    MY_NPU_E_TIMEOUT = -3
} my_npu_result_t;

typedef struct my_npu_dev {
    volatile uint32_t* regs;
} my_npu_dev_t;

typedef struct my_npu_caps {
    uint32_t device_id;
    uint32_t abi_version;
    uint32_t cap0;
    uint32_t cap1;
} my_npu_caps_t;

typedef struct my_npu_status {
    uint32_t raw;
    int idle;
    int busy;
    int done;
    int error;
    int irq_pending;
} my_npu_status_t;

typedef struct my_npu_perf {
    uint64_t cycles;
    uint64_t busy_cycles;
    uint32_t desc_count;
    uint32_t error_count;
} my_npu_perf_t;

my_npu_result_t my_npu_init(my_npu_dev_t* dev, volatile void* base);
my_npu_result_t my_npu_get_caps(const my_npu_dev_t* dev, my_npu_caps_t* caps);
my_npu_result_t my_npu_build_gemm_desc(
    my_npu_gemm_desc_t* desc,
    const my_npu_gemm_config_t* cfg
);
my_npu_result_t my_npu_submit(const my_npu_dev_t* dev, uint64_t desc_pa);
my_npu_result_t my_npu_poll(const my_npu_dev_t* dev, my_npu_status_t* status);
my_npu_result_t my_npu_wait(
    const my_npu_dev_t* dev,
    uint32_t timeout_polls,
    my_npu_status_t* final_status
);
my_npu_result_t my_npu_error(const my_npu_dev_t* dev, uint32_t* error_code);
my_npu_result_t my_npu_clear(const my_npu_dev_t* dev, uint32_t mask);
my_npu_result_t my_npu_read_perf(const my_npu_dev_t* dev, my_npu_perf_t* perf);

#ifdef __cplusplus
}
#endif

#endif
