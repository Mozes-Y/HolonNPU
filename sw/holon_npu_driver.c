#include "holon_npu_driver.h"

#include <string.h>

static uint32_t holon_npu_reg_index(uint32_t offset) {
    return offset / sizeof(uint32_t);
}

static uint32_t holon_npu_read32(const holon_npu_dev_t* dev, uint32_t offset) {
    return dev->regs[holon_npu_reg_index(offset)];
}

static void holon_npu_write32(const holon_npu_dev_t* dev, uint32_t offset, uint32_t value) {
    dev->regs[holon_npu_reg_index(offset)] = value;
}

static int holon_npu_is_aligned_u64(uint64_t value, uint32_t alignment) {
    return (value & (uint64_t)(alignment - 1U)) == 0U;
}

static int holon_npu_is_aligned_u32(uint32_t value, uint32_t alignment) {
    return (value & (alignment - 1U)) == 0U;
}

static int holon_npu_dev_valid(const holon_npu_dev_t* dev) {
    return (dev != NULL) && (dev->regs != NULL);
}

static uint64_t holon_npu_read_counter64(
    const holon_npu_dev_t* dev,
    uint32_t lo_offset,
    uint32_t hi_offset
) {
    uint32_t hi_before = 0;
    uint32_t lo = 0;
    uint32_t hi_after = 0;

    do {
        hi_before = holon_npu_read32(dev, hi_offset);
        lo = holon_npu_read32(dev, lo_offset);
        hi_after = holon_npu_read32(dev, hi_offset);
    } while (hi_before != hi_after);

    return ((uint64_t)hi_after << 32) | lo;
}

holon_npu_result_t holon_npu_init(holon_npu_dev_t* dev, volatile void* base) {
    if ((dev == NULL) || (base == NULL)) {
        return HOLON_NPU_E_ARG;
    }

    dev->regs = (volatile uint32_t*)base;
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_get_caps(const holon_npu_dev_t* dev, holon_npu_caps_t* caps) {
    if (!holon_npu_dev_valid(dev) || (caps == NULL)) {
        return HOLON_NPU_E_ARG;
    }

    caps->device_id = holon_npu_read32(dev, HOLON_NPU_REG_DEVICE_ID);
    caps->abi_version = holon_npu_read32(dev, HOLON_NPU_REG_ABI_VERSION);
    caps->cap0 = holon_npu_read32(dev, HOLON_NPU_REG_CAP0);
    caps->cap1 = holon_npu_read32(dev, HOLON_NPU_REG_CAP1);
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_build_gemm_desc(
    holon_npu_gemm_desc_t* desc,
    const holon_npu_gemm_config_t* cfg
) {
    uint32_t min_c_stride = 0;

    if ((desc == NULL) || (cfg == NULL)) {
        return HOLON_NPU_E_ARG;
    }

    if ((cfg->flags & ~HOLON_NPU_DESC_FLAG_VALID_MASK) != 0U) {
        return HOLON_NPU_E_ARG;
    }

    if ((cfg->m == 0U) || (cfg->n == 0U) || (cfg->k == 0U)) {
        return HOLON_NPU_E_ARG;
    }

    if ((cfg->m > UINT32_C(65535)) ||
        (cfg->n > UINT32_C(65535)) ||
        (cfg->k > UINT32_C(65535))) {
        return HOLON_NPU_E_ARG;
    }

    min_c_stride = cfg->n * UINT32_C(4);
    if (!holon_npu_is_aligned_u64(cfg->a_addr, HOLON_NPU_TENSOR_ALIGN) ||
        !holon_npu_is_aligned_u64(cfg->b_addr, HOLON_NPU_TENSOR_ALIGN) ||
        !holon_npu_is_aligned_u64(cfg->c_addr, HOLON_NPU_TENSOR_ALIGN) ||
        !holon_npu_is_aligned_u32(cfg->a_row_stride_bytes, HOLON_NPU_TENSOR_ALIGN) ||
        !holon_npu_is_aligned_u32(cfg->b_row_stride_bytes, HOLON_NPU_TENSOR_ALIGN) ||
        !holon_npu_is_aligned_u32(cfg->c_row_stride_bytes, HOLON_NPU_TENSOR_ALIGN) ||
        (cfg->a_row_stride_bytes < cfg->k) ||
        (cfg->b_row_stride_bytes < cfg->n) ||
        (cfg->c_row_stride_bytes < min_c_stride)) {
        return HOLON_NPU_E_ARG;
    }

    memset(desc, 0, sizeof(*desc));
    desc->size_bytes = (uint16_t)HOLON_NPU_DESC_SIZE;
    desc->version = (uint8_t)HOLON_NPU_ABI_MAJOR;
    desc->opcode = (uint8_t)HOLON_NPU_OPCODE_GEMM_I8I8I32;
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
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_submit(const holon_npu_dev_t* dev, uint64_t desc_pa) {
    uint32_t status = 0;

    if (!holon_npu_dev_valid(dev) || !holon_npu_is_aligned_u64(desc_pa, HOLON_NPU_DESC_ALIGN)) {
        return HOLON_NPU_E_ARG;
    }

    status = holon_npu_read32(dev, HOLON_NPU_REG_STATUS);
    if ((status & HOLON_NPU_STATUS_BUSY) != 0U) {
        return HOLON_NPU_E_BUSY;
    }

    holon_npu_write32(dev, HOLON_NPU_REG_DESC_ADDR_LO, (uint32_t)(desc_pa & UINT64_C(0xFFFFFFFF)));
    holon_npu_write32(dev, HOLON_NPU_REG_DESC_ADDR_HI, (uint32_t)(desc_pa >> 32));
    holon_npu_write32(dev, HOLON_NPU_REG_DOORBELL, HOLON_NPU_DOORBELL_START);
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_poll(const holon_npu_dev_t* dev, holon_npu_status_t* status) {
    uint32_t raw = 0;

    if (!holon_npu_dev_valid(dev) || (status == NULL)) {
        return HOLON_NPU_E_ARG;
    }

    raw = holon_npu_read32(dev, HOLON_NPU_REG_STATUS);
    status->raw = raw;
    status->idle = (raw & HOLON_NPU_STATUS_IDLE) != 0U;
    status->busy = (raw & HOLON_NPU_STATUS_BUSY) != 0U;
    status->done = (raw & HOLON_NPU_STATUS_DONE) != 0U;
    status->error = (raw & HOLON_NPU_STATUS_ERROR) != 0U;
    status->irq_pending = (raw & HOLON_NPU_STATUS_IRQ_PENDING) != 0U;
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_wait(
    const holon_npu_dev_t* dev,
    uint32_t timeout_polls,
    holon_npu_status_t* final_status
) {
    holon_npu_status_t local_status;

    if (!holon_npu_dev_valid(dev)) {
        return HOLON_NPU_E_ARG;
    }

    for (uint32_t poll = 0; poll <= timeout_polls; poll++) {
        holon_npu_result_t result = holon_npu_poll(dev, &local_status);
        if (result != HOLON_NPU_OK) {
            return result;
        }

        if (final_status != NULL) {
            *final_status = local_status;
        }

        if (local_status.done || local_status.error) {
            return HOLON_NPU_OK;
        }

        if (poll == UINT32_MAX) {
            break;
        }
    }

    return HOLON_NPU_E_TIMEOUT;
}

holon_npu_result_t holon_npu_error(const holon_npu_dev_t* dev, uint32_t* error_code) {
    if (!holon_npu_dev_valid(dev) || (error_code == NULL)) {
        return HOLON_NPU_E_ARG;
    }

    *error_code = holon_npu_read32(dev, HOLON_NPU_REG_ERROR_CODE);
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_clear(const holon_npu_dev_t* dev, uint32_t mask) {
    uint32_t clear_mask = 0;

    if (!holon_npu_dev_valid(dev) || ((mask & ~HOLON_NPU_CLEAR_VALID_MASK) != 0U)) {
        return HOLON_NPU_E_ARG;
    }

    clear_mask = mask & (HOLON_NPU_CLEAR_DONE | HOLON_NPU_CLEAR_ERROR | HOLON_NPU_CLEAR_PERF);
    if (clear_mask != 0U) {
        holon_npu_write32(dev, HOLON_NPU_REG_CLEAR, clear_mask);
    }

    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_read_perf(const holon_npu_dev_t* dev, holon_npu_perf_t* perf) {
    if (!holon_npu_dev_valid(dev) || (perf == NULL)) {
        return HOLON_NPU_E_ARG;
    }

    perf->cycles = holon_npu_read_counter64(
        dev,
        HOLON_NPU_REG_PERF_CYCLES_LO,
        HOLON_NPU_REG_PERF_CYCLES_HI
    );
    perf->busy_cycles = holon_npu_read_counter64(
        dev,
        HOLON_NPU_REG_PERF_BUSY_LO,
        HOLON_NPU_REG_PERF_BUSY_HI
    );
    perf->desc_count = holon_npu_read32(dev, HOLON_NPU_REG_PERF_DESC_COUNT);
    perf->error_count = holon_npu_read32(dev, HOLON_NPU_REG_PERF_ERROR_COUNT);
    return HOLON_NPU_OK;
}
