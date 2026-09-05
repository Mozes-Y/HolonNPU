#include "holon_npu_isa.h"
#include "holon_npu_program.h"

#include <stdint.h>

static volatile uint32_t *const npu = (volatile uint32_t *)0x10010000u;

static uint32_t program[1] __attribute__((section(".holon_program"), aligned(16)));
static holon_npu_completion_record_t completion
    __attribute__((section(".holon_completion"), aligned(16)));
static holon_npu_program_desc_t descriptor
    __attribute__((section(".holon_descriptor"), aligned(16)));

static void host_memory_fence(void) {
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static void request_checkpoint(void) {
    register uint64_t delay __asm__("a0") = 0;
    register uint64_t period __asm__("a1") = 0;
    __asm__ volatile(".word 0x8600007b"
                     : "+r"(delay), "+r"(period)
                     :
                     : "memory");
}

static uint32_t system_exit(void) {
    return HOLON_NPU_ISA_CLASS_SYSTEM |
        (HOLON_NPU_ISA_OPCODE_SYSTEM_EXIT << HOLON_NPU_ISA_OPCODE_SHIFT);
}

static uint32_t wait_for_terminal(void) {
    uint32_t status = 0;
    for (uint32_t timeout = 0; timeout < 1000000u; ++timeout) {
        status = npu[HOLON_NPU_REG_STATUS / 4];
        if ((status & (HOLON_NPU_STATUS_DONE | HOLON_NPU_STATUS_FAULT)) != 0)
            break;
    }
    return status;
}

static void write_descriptor_address(void) {
    const uintptr_t address = (uintptr_t)&descriptor;
    npu[HOLON_NPU_REG_PROGRAM_DESC_ADDR_LO / 4] = (uint32_t)address;
    npu[HOLON_NPU_REG_PROGRAM_DESC_ADDR_HI / 4] = (uint32_t)(address >> 32u);
}

static int run_program(uint64_t minimum_cycles) {
    completion = (holon_npu_completion_record_t){0};
    host_memory_fence();
    npu[HOLON_NPU_REG_DOORBELL / 4] = HOLON_NPU_DOORBELL_START;
    const uint32_t status = wait_for_terminal();
    host_memory_fence();
    if ((status & HOLON_NPU_STATUS_DONE) == 0 ||
        npu[HOLON_NPU_REG_FAULT_CODE / 4] != HOLON_NPU_FAULT_NONE ||
        completion.status != HOLON_NPU_COMPLETION_STATUS_DONE ||
        completion.instret != 1 || completion.debug_pc != 4 ||
        completion.cycle_count <= minimum_cycles) {
        return 1;
    }
    return 0;
}

int main(void) {
    program[0] = system_exit();
    descriptor = (holon_npu_program_desc_t){
        .size_bytes = HOLON_NPU_PROGRAM_DESC_SIZE,
        .version = HOLON_NPU_ABI_MAJOR,
        .program_format = HOLON_NPU_PROGRAM_FORMAT_HOLON,
        .holon_isa_major = HOLON_NPU_ISA_MAJOR,
        .holon_isa_minor = HOLON_NPU_ISA_MINOR,
        .required_caps = HOLON_NPU_CAP_PROGRAM_DESCRIPTOR |
                         HOLON_NPU_CAP_LOCAL_PROGRAM_MEMORY,
        .required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        .code_addr = (uintptr_t)program,
        .code_size_bytes = sizeof(program),
        .entry_pc = 0,
        .arg_addr = 0,
        .arg_size_bytes = 0,
        .local_mem_bytes = 16,
        .program_mem_bytes = sizeof(program),
        .stack_bytes = 0,
        .completion_addr = (uintptr_t)&completion,
        .flags = HOLON_NPU_PROGRAM_FLAG_IRQ_ON_DONE,
    };

    write_descriptor_address();
    npu[HOLON_NPU_REG_IRQ_ENABLE / 4] = HOLON_NPU_IRQ_DONE | HOLON_NPU_IRQ_FAULT;
    if (run_program(0) != 0) return 1;

    const uint32_t cycles_before_checkpoint = npu[HOLON_NPU_REG_PERF_CYCLE_LO / 4];
    npu[HOLON_NPU_REG_CONTROL / 4] = HOLON_NPU_CONTROL_CLEAR_TERMINAL;
    const uintptr_t descriptor_address = (uintptr_t)&descriptor;
    const uint32_t idle_status = npu[HOLON_NPU_REG_STATUS / 4];
    if (cycles_before_checkpoint == 0 ||
        (idle_status & (HOLON_NPU_STATUS_IDLE | HOLON_NPU_STATUS_IRQ_PENDING)) !=
            (HOLON_NPU_STATUS_IDLE | HOLON_NPU_STATUS_IRQ_PENDING)) {
        return 2;
    }

    request_checkpoint();

    if (npu[HOLON_NPU_REG_PROGRAM_DESC_ADDR_LO / 4] !=
            (uint32_t)descriptor_address ||
        npu[HOLON_NPU_REG_PROGRAM_DESC_ADDR_HI / 4] !=
            (uint32_t)(descriptor_address >> 32u) ||
        npu[HOLON_NPU_REG_IRQ_ENABLE / 4] !=
            (HOLON_NPU_IRQ_DONE | HOLON_NPU_IRQ_FAULT) ||
        npu[HOLON_NPU_REG_IRQ_STATUS / 4] != HOLON_NPU_IRQ_DONE ||
        npu[HOLON_NPU_REG_PERF_CYCLE_LO / 4] != cycles_before_checkpoint) {
        return 3;
    }

    npu[HOLON_NPU_REG_IRQ_CLEAR / 4] = HOLON_NPU_IRQ_DONE;
    if ((npu[HOLON_NPU_REG_STATUS / 4] & HOLON_NPU_STATUS_IRQ_PENDING) != 0)
        return 4;
    if (run_program(cycles_before_checkpoint) != 0) return 5;

    npu[HOLON_NPU_REG_IRQ_CLEAR / 4] = HOLON_NPU_IRQ_DONE;
    npu[HOLON_NPU_REG_CONTROL / 4] = HOLON_NPU_CONTROL_CLEAR_TERMINAL;
    return (npu[HOLON_NPU_REG_STATUS / 4] & HOLON_NPU_STATUS_IDLE) != 0 ? 0 : 6;
}
