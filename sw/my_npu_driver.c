#include "my_npu_driver.h"

#include <string.h>

static uint32_t my_npu_reg_index(uint32_t offset) {
    return offset / sizeof(uint32_t);
}

static uint32_t my_npu_read32(const my_npu_dev_t* dev, uint32_t offset) {
    return dev->regs[my_npu_reg_index(offset)];
}

static void my_npu_write32(const my_npu_dev_t* dev, uint32_t offset, uint32_t value) {
    dev->regs[my_npu_reg_index(offset)] = value;
}

static int my_npu_is_aligned_u64(uint64_t value, uint32_t alignment) {
    return (value & (uint64_t)(alignment - 1U)) == 0U;
}

static int my_npu_is_aligned_u32(uint32_t value, uint32_t alignment) {
    return (value & (alignment - 1U)) == 0U;
}

static int my_npu_dev_valid(const my_npu_dev_t* dev) {
    return (dev != NULL) && (dev->regs != NULL);
}

static uint64_t my_npu_read_counter64(
    const my_npu_dev_t* dev,
    uint32_t lo_offset,
    uint32_t hi_offset
) {
    uint32_t hi_before = 0;
    uint32_t lo = 0;
    uint32_t hi_after = 0;

    do {
        hi_before = my_npu_read32(dev, hi_offset);
        lo = my_npu_read32(dev, lo_offset);
        hi_after = my_npu_read32(dev, hi_offset);
    } while (hi_before != hi_after);

    return ((uint64_t)hi_after << 32) | lo;
}

my_npu_result_t my_npu_init(my_npu_dev_t* dev, volatile void* base) {
    if ((dev == NULL) || (base == NULL)) {
        return MY_NPU_E_ARG;
    }

    dev->regs = (volatile uint32_t*)base;
    return MY_NPU_OK;
}

my_npu_result_t my_npu_get_caps(const my_npu_dev_t* dev, my_npu_caps_t* caps) {
    if (!my_npu_dev_valid(dev) || (caps == NULL)) {
        return MY_NPU_E_ARG;
    }

    caps->device_id = my_npu_read32(dev, MY_NPU_REG_DEVICE_ID);
    caps->abi_version = my_npu_read32(dev, MY_NPU_REG_ABI_VERSION);
    caps->cap0 = my_npu_read32(dev, MY_NPU_REG_CAP0);
    caps->cap1 = my_npu_read32(dev, MY_NPU_REG_CAP1);
    return MY_NPU_OK;
}

my_npu_result_t my_npu_build_gemm_desc(
    my_npu_gemm_desc_t* desc,
    const my_npu_gemm_config_t* cfg
) {
    uint32_t min_c_stride = 0;

    if ((desc == NULL) || (cfg == NULL)) {
        return MY_NPU_E_ARG;
    }

    if ((cfg->flags & ~MY_NPU_DESC_FLAG_VALID_MASK) != 0U) {
        return MY_NPU_E_ARG;
    }

    if ((cfg->m == 0U) || (cfg->n == 0U) || (cfg->k == 0U)) {
        return MY_NPU_E_ARG;
    }

    if ((cfg->m > UINT32_C(65535)) ||
        (cfg->n > UINT32_C(65535)) ||
        (cfg->k > UINT32_C(65535))) {
        return MY_NPU_E_ARG;
    }

    min_c_stride = cfg->n * UINT32_C(4);
    if (!my_npu_is_aligned_u64(cfg->a_addr, MY_NPU_TENSOR_ALIGN) ||
        !my_npu_is_aligned_u64(cfg->b_addr, MY_NPU_TENSOR_ALIGN) ||
        !my_npu_is_aligned_u64(cfg->c_addr, MY_NPU_TENSOR_ALIGN) ||
        !my_npu_is_aligned_u32(cfg->a_row_stride_bytes, MY_NPU_TENSOR_ALIGN) ||
        !my_npu_is_aligned_u32(cfg->b_row_stride_bytes, MY_NPU_TENSOR_ALIGN) ||
        !my_npu_is_aligned_u32(cfg->c_row_stride_bytes, MY_NPU_TENSOR_ALIGN) ||
        (cfg->a_row_stride_bytes < cfg->k) ||
        (cfg->b_row_stride_bytes < cfg->n) ||
        (cfg->c_row_stride_bytes < min_c_stride)) {
        return MY_NPU_E_ARG;
    }

    memset(desc, 0, sizeof(*desc));
    desc->size_bytes = (uint16_t)MY_NPU_DESC_SIZE;
    desc->version = (uint8_t)MY_NPU_ABI_MAJOR;
    desc->opcode = (uint8_t)MY_NPU_OPCODE_GEMM_I8I8I32;
    desc->flags = cfg->flags;
    desc->m = cfg->m;
    desc->n = cfg->n;
    desc->k = cfg->k;
    desc->a_addr = cfg->a_addr;
    desc->b_addr = cfg->b_addr;
    desc->c_addr = cfg->c_addr;
    desc->a_row_stride_bytes = cfg->a_row_stride_bytes;
    desc->b_row_stride_bytes = cfg->b_row_stride_bytes;
    desc->c_row_stride_bytes = cfg->c_row_stride_bytes;
    return MY_NPU_OK;
}

my_npu_result_t my_npu_submit(const my_npu_dev_t* dev, uint64_t desc_pa) {
    uint32_t status = 0;

    if (!my_npu_dev_valid(dev) || !my_npu_is_aligned_u64(desc_pa, MY_NPU_DESC_ALIGN)) {
        return MY_NPU_E_ARG;
    }

    status = my_npu_read32(dev, MY_NPU_REG_STATUS);
    if ((status & MY_NPU_STATUS_BUSY) != 0U) {
        return MY_NPU_E_BUSY;
    }

    my_npu_write32(dev, MY_NPU_REG_DESC_ADDR_LO, (uint32_t)(desc_pa & UINT64_C(0xFFFFFFFF)));
    my_npu_write32(dev, MY_NPU_REG_DESC_ADDR_HI, (uint32_t)(desc_pa >> 32));
    my_npu_write32(dev, MY_NPU_REG_DOORBELL, MY_NPU_DOORBELL_START);
    return MY_NPU_OK;
}

my_npu_result_t my_npu_poll(const my_npu_dev_t* dev, my_npu_status_t* status) {
    uint32_t raw = 0;

    if (!my_npu_dev_valid(dev) || (status == NULL)) {
        return MY_NPU_E_ARG;
    }

    raw = my_npu_read32(dev, MY_NPU_REG_STATUS);
    status->raw = raw;
    status->idle = (raw & MY_NPU_STATUS_IDLE) != 0U;
    status->busy = (raw & MY_NPU_STATUS_BUSY) != 0U;
    status->done = (raw & MY_NPU_STATUS_DONE) != 0U;
    status->error = (raw & MY_NPU_STATUS_ERROR) != 0U;
    status->irq_pending = (raw & MY_NPU_STATUS_IRQ_PENDING) != 0U;
    return MY_NPU_OK;
}

my_npu_result_t my_npu_wait(
    const my_npu_dev_t* dev,
    uint32_t timeout_polls,
    my_npu_status_t* final_status
) {
    my_npu_status_t local_status;

    if (!my_npu_dev_valid(dev)) {
        return MY_NPU_E_ARG;
    }

    for (uint32_t poll = 0; poll <= timeout_polls; poll++) {
        my_npu_result_t result = my_npu_poll(dev, &local_status);
        if (result != MY_NPU_OK) {
            return result;
        }

        if (final_status != NULL) {
            *final_status = local_status;
        }

        if (local_status.done || local_status.error) {
            return MY_NPU_OK;
        }

        if (poll == UINT32_MAX) {
            break;
        }
    }

    return MY_NPU_E_TIMEOUT;
}

my_npu_result_t my_npu_error(const my_npu_dev_t* dev, uint32_t* error_code) {
    if (!my_npu_dev_valid(dev) || (error_code == NULL)) {
        return MY_NPU_E_ARG;
    }

    *error_code = my_npu_read32(dev, MY_NPU_REG_ERROR_CODE);
    return MY_NPU_OK;
}

my_npu_result_t my_npu_clear(const my_npu_dev_t* dev, uint32_t mask) {
    uint32_t clear_mask = 0;

    if (!my_npu_dev_valid(dev) || ((mask & ~MY_NPU_CLEAR_VALID_MASK) != 0U)) {
        return MY_NPU_E_ARG;
    }

    clear_mask = mask & (MY_NPU_CLEAR_DONE | MY_NPU_CLEAR_ERROR | MY_NPU_CLEAR_PERF);
    if (clear_mask != 0U) {
        my_npu_write32(dev, MY_NPU_REG_CLEAR, clear_mask);
    }

    return MY_NPU_OK;
}

my_npu_result_t my_npu_read_perf(const my_npu_dev_t* dev, my_npu_perf_t* perf) {
    if (!my_npu_dev_valid(dev) || (perf == NULL)) {
        return MY_NPU_E_ARG;
    }

    perf->cycles = my_npu_read_counter64(
        dev,
        MY_NPU_REG_PERF_CYCLES_LO,
        MY_NPU_REG_PERF_CYCLES_HI
    );
    perf->busy_cycles = my_npu_read_counter64(
        dev,
        MY_NPU_REG_PERF_BUSY_LO,
        MY_NPU_REG_PERF_BUSY_HI
    );
    perf->desc_count = my_npu_read32(dev, MY_NPU_REG_PERF_DESC_COUNT);
    perf->error_count = my_npu_read32(dev, MY_NPU_REG_PERF_ERROR_COUNT);
    return MY_NPU_OK;
}
