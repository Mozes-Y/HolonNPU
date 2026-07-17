#include "holon_npu_driver.h"

#include <stddef.h>
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

static int holon_npu_range_fits_u64(uint64_t start, uint64_t byte_count) {
    return byte_count <= UINT64_MAX - start;
}

static int holon_npu_dev_valid(const holon_npu_dev_t* dev) {
    return (dev != NULL) && (dev->regs != NULL);
}

static uint64_t holon_npu_read_counter64(
    const holon_npu_dev_t* dev,
    uint32_t lo_offset,
    uint32_t hi_offset
) {
    uint32_t hi_before;
    uint32_t lo;
    uint32_t hi_after;

    do {
        hi_before = holon_npu_read32(dev, hi_offset);
        lo = holon_npu_read32(dev, lo_offset);
        hi_after = holon_npu_read32(dev, hi_offset);
    } while (hi_before != hi_after);

    return ((uint64_t)hi_after << 32) | lo;
}

static holon_npu_result_t holon_npu_write_control(
    const holon_npu_dev_t* dev,
    uint32_t command,
    uint32_t required_status
) {
    uint32_t status;

    if (!holon_npu_dev_valid(dev)) {
        return HOLON_NPU_E_ARG;
    }
    status = holon_npu_read32(dev, HOLON_NPU_REG_STATUS);
    if ((status & required_status) == 0U) {
        return HOLON_NPU_E_STATE;
    }
    holon_npu_write32(dev, HOLON_NPU_REG_CONTROL, command);
    return HOLON_NPU_OK;
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
    caps->isa_version = holon_npu_read32(dev, HOLON_NPU_REG_ISA_VERSION);
    caps->capabilities = holon_npu_read_counter64(
        dev,
        HOLON_NPU_REG_CAP0_LO,
        HOLON_NPU_REG_CAP0_HI
    );
    caps->operation_classes = holon_npu_read_counter64(
        dev,
        HOLON_NPU_REG_OP_CLASS_LO,
        HOLON_NPU_REG_OP_CLASS_HI
    );
    caps->program_mem_bytes = holon_npu_read32(dev, HOLON_NPU_REG_PROGRAM_MEM_BYTES);
    caps->local_mem_bytes = holon_npu_read32(dev, HOLON_NPU_REG_LOCAL_MEM_BYTES);
    caps->vector_cap0 = holon_npu_read32(dev, HOLON_NPU_REG_VECTOR_CAP0);
    caps->matrix_cap0 = holon_npu_read32(dev, HOLON_NPU_REG_MATRIX_CAP0);
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_build_program_desc(
    holon_npu_program_desc_t* desc,
    const holon_npu_program_config_t* config
) {
    if ((desc == NULL) || (config == NULL)) {
        return HOLON_NPU_E_ARG;
    }
    if ((config->flags & ~HOLON_NPU_PROGRAM_FLAG_VALID_MASK) != 0U ||
        !holon_npu_is_aligned_u64(config->code_addr, HOLON_NPU_PROGRAM_IMAGE_ALIGN) ||
        !holon_npu_is_aligned_u32(config->code_size_bytes, HOLON_NPU_PROGRAM_IMAGE_ALIGN) ||
        !holon_npu_is_aligned_u32(config->entry_pc, HOLON_NPU_ISA_INSTRUCTION_BYTES) ||
        !holon_npu_is_aligned_u64(config->arg_addr, HOLON_NPU_PROGRAM_ARGUMENT_ALIGN) ||
        !holon_npu_is_aligned_u32(config->arg_size_bytes, HOLON_NPU_PROGRAM_ARGUMENT_ALIGN) ||
        ((config->completion_addr != 0U) &&
         !holon_npu_is_aligned_u64(config->completion_addr, HOLON_NPU_PROGRAM_COMPLETION_ALIGN)) ||
        !holon_npu_range_fits_u64(config->code_addr, config->code_size_bytes) ||
        !holon_npu_range_fits_u64(config->arg_addr, config->arg_size_bytes) ||
        ((config->completion_addr != 0U) &&
         !holon_npu_range_fits_u64(config->completion_addr, HOLON_NPU_COMPLETION_RECORD_SIZE)) ||
        (config->code_size_bytes == 0U) ||
        (config->entry_pc >= config->code_size_bytes) ||
        (config->program_mem_bytes < config->code_size_bytes) ||
        (config->program_mem_bytes > HOLON_NPU_PROGRAM_MEM_MAX_BYTES) ||
        (config->local_mem_bytes < config->arg_size_bytes) ||
        (config->local_mem_bytes > HOLON_NPU_LOCAL_MEM_MAX_BYTES) ||
        (config->stack_bytes > HOLON_NPU_PROGRAM_STACK_MAX_BYTES) ||
        ((uint64_t)config->arg_size_bytes + config->stack_bytes > config->local_mem_bytes)) {
        return HOLON_NPU_E_ARG;
    }

    memset(desc, 0, sizeof(*desc));
    desc->size_bytes = HOLON_NPU_PROGRAM_DESC_SIZE;
    desc->version = HOLON_NPU_ABI_MAJOR;
    desc->program_format = HOLON_NPU_PROGRAM_FORMAT_HOLON;
    desc->holon_isa_major = HOLON_NPU_ISA_MAJOR;
    desc->holon_isa_minor = HOLON_NPU_ISA_MINOR;
    desc->required_caps = config->required_caps;
    desc->required_op_classes = config->required_op_classes;
    desc->code_addr = config->code_addr;
    desc->code_size_bytes = config->code_size_bytes;
    desc->entry_pc = config->entry_pc;
    desc->arg_addr = config->arg_addr;
    desc->arg_size_bytes = config->arg_size_bytes;
    desc->local_mem_bytes = config->local_mem_bytes;
    desc->program_mem_bytes = config->program_mem_bytes;
    desc->stack_bytes = config->stack_bytes;
    desc->completion_addr = config->completion_addr;
    desc->flags = config->flags;
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_submit_program(const holon_npu_dev_t* dev, uint64_t desc_pa) {
    uint32_t status;

    if (!holon_npu_dev_valid(dev) ||
        !holon_npu_is_aligned_u64(desc_pa, HOLON_NPU_PROGRAM_DESC_ALIGN) ||
        !holon_npu_range_fits_u64(desc_pa, HOLON_NPU_PROGRAM_DESC_SIZE)) {
        return HOLON_NPU_E_ARG;
    }

    status = holon_npu_read32(dev, HOLON_NPU_REG_STATUS);
    if ((status & (HOLON_NPU_STATUS_LOADING |
                   HOLON_NPU_STATUS_RUNNING |
                   HOLON_NPU_STATUS_HALTED)) != 0U) {
        return HOLON_NPU_E_BUSY;
    }
    if ((status & HOLON_NPU_STATUS_IDLE) == 0U) {
        return HOLON_NPU_E_STATE;
    }

    holon_npu_write32(
        dev,
        HOLON_NPU_REG_PROGRAM_DESC_ADDR_LO,
        (uint32_t)(desc_pa & UINT64_C(0xFFFFFFFF))
    );
    holon_npu_write32(dev, HOLON_NPU_REG_PROGRAM_DESC_ADDR_HI, (uint32_t)(desc_pa >> 32));
    holon_npu_write32(dev, HOLON_NPU_REG_DOORBELL, HOLON_NPU_DOORBELL_START);
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_poll(const holon_npu_dev_t* dev, holon_npu_status_t* status) {
    uint32_t raw;

    if (!holon_npu_dev_valid(dev) || (status == NULL)) {
        return HOLON_NPU_E_ARG;
    }

    raw = holon_npu_read32(dev, HOLON_NPU_REG_STATUS);
    status->raw = raw;
    status->idle = (raw & HOLON_NPU_STATUS_IDLE) != 0U;
    status->loading = (raw & HOLON_NPU_STATUS_LOADING) != 0U;
    status->running = (raw & HOLON_NPU_STATUS_RUNNING) != 0U;
    status->halted = (raw & HOLON_NPU_STATUS_HALTED) != 0U;
    status->done = (raw & HOLON_NPU_STATUS_DONE) != 0U;
    status->fault = (raw & HOLON_NPU_STATUS_FAULT) != 0U;
    status->irq_pending = (raw & HOLON_NPU_STATUS_IRQ_PENDING) != 0U;
    status->resetting = (raw & HOLON_NPU_STATUS_RESETTING) != 0U;
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

    for (uint32_t poll = 0; poll <= timeout_polls; ++poll) {
        holon_npu_result_t result = holon_npu_poll(dev, &local_status);
        if (result != HOLON_NPU_OK) {
            return result;
        }
        if (final_status != NULL) {
            *final_status = local_status;
        }
        if (local_status.done || local_status.fault) {
            return HOLON_NPU_OK;
        }
        if (poll == UINT32_MAX) {
            break;
        }
    }
    return HOLON_NPU_E_TIMEOUT;
}

holon_npu_result_t holon_npu_halt(const holon_npu_dev_t* dev) {
    return holon_npu_write_control(dev, HOLON_NPU_CONTROL_HALT, HOLON_NPU_STATUS_RUNNING);
}

holon_npu_result_t holon_npu_resume(const holon_npu_dev_t* dev) {
    return holon_npu_write_control(dev, HOLON_NPU_CONTROL_RESUME, HOLON_NPU_STATUS_HALTED);
}

holon_npu_result_t holon_npu_debug_step(const holon_npu_dev_t* dev) {
    return holon_npu_write_control(dev, HOLON_NPU_CONTROL_DEBUG_STEP, HOLON_NPU_STATUS_HALTED);
}

holon_npu_result_t holon_npu_soft_reset(const holon_npu_dev_t* dev) {
    uint32_t status;

    if (!holon_npu_dev_valid(dev)) {
        return HOLON_NPU_E_ARG;
    }
    status = holon_npu_read32(dev, HOLON_NPU_REG_STATUS);
    if ((status & HOLON_NPU_STATUS_RESETTING) != 0U) {
        return HOLON_NPU_E_STATE;
    }
    holon_npu_write32(dev, HOLON_NPU_REG_CONTROL, HOLON_NPU_CONTROL_SOFT_RESET);
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_wait_idle(
    const holon_npu_dev_t* dev,
    uint32_t timeout_polls,
    holon_npu_status_t* final_status
) {
    holon_npu_status_t local_status;

    if (!holon_npu_dev_valid(dev)) {
        return HOLON_NPU_E_ARG;
    }
    for (uint32_t poll = 0; poll <= timeout_polls; ++poll) {
        holon_npu_result_t result = holon_npu_poll(dev, &local_status);
        if (result != HOLON_NPU_OK) {
            return result;
        }
        if (final_status != NULL) {
            *final_status = local_status;
        }
        if (local_status.idle) {
            return HOLON_NPU_OK;
        }
        if (poll == UINT32_MAX) {
            break;
        }
    }
    return HOLON_NPU_E_TIMEOUT;
}

holon_npu_result_t holon_npu_clear_terminal(const holon_npu_dev_t* dev) {
    return holon_npu_write_control(
        dev,
        HOLON_NPU_CONTROL_CLEAR_TERMINAL,
        HOLON_NPU_STATUS_DONE | HOLON_NPU_STATUS_FAULT
    );
}

holon_npu_result_t holon_npu_get_fault_snapshot(
    const holon_npu_dev_t* dev,
    holon_npu_fault_snapshot_t* snapshot
) {
    if (!holon_npu_dev_valid(dev) || (snapshot == NULL)) {
        return HOLON_NPU_E_ARG;
    }
    snapshot->code = holon_npu_read32(dev, HOLON_NPU_REG_FAULT_CODE);
    snapshot->pc = holon_npu_read32(dev, HOLON_NPU_REG_DEBUG_PC);
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_set_irq_enable(const holon_npu_dev_t* dev, uint32_t mask) {
    if (!holon_npu_dev_valid(dev) || ((mask & ~HOLON_NPU_IRQ_VALID_MASK) != 0U)) {
        return HOLON_NPU_E_ARG;
    }
    holon_npu_write32(dev, HOLON_NPU_REG_IRQ_ENABLE, mask);
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_get_irq_status(const holon_npu_dev_t* dev, uint32_t* status) {
    if (!holon_npu_dev_valid(dev) || (status == NULL)) {
        return HOLON_NPU_E_ARG;
    }
    *status = holon_npu_read32(dev, HOLON_NPU_REG_IRQ_STATUS) & HOLON_NPU_IRQ_VALID_MASK;
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_clear_irq(const holon_npu_dev_t* dev, uint32_t mask) {
    if (!holon_npu_dev_valid(dev) || ((mask & ~HOLON_NPU_IRQ_VALID_MASK) != 0U)) {
        return HOLON_NPU_E_ARG;
    }
    holon_npu_write32(dev, HOLON_NPU_REG_IRQ_CLEAR, mask);
    return HOLON_NPU_OK;
}

holon_npu_result_t holon_npu_read_perf(const holon_npu_dev_t* dev, holon_npu_perf_t* perf) {
    if (!holon_npu_dev_valid(dev) || (perf == NULL)) {
        return HOLON_NPU_E_ARG;
    }
    perf->cycles = holon_npu_read_counter64(
        dev,
        HOLON_NPU_REG_PERF_CYCLE_LO,
        HOLON_NPU_REG_PERF_CYCLE_HI
    );
    perf->instructions_retired = holon_npu_read_counter64(
        dev,
        HOLON_NPU_REG_PERF_INSTRET_LO,
        HOLON_NPU_REG_PERF_INSTRET_HI
    );
    return HOLON_NPU_OK;
}
