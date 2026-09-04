#include "holon_npu_isa.h"
#include "holon_npu_program.h"

#include <stdint.h>

static volatile uint32_t *const npu = (volatile uint32_t *)0x10010000u;

static void host_memory_fence(void) {
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static uint32_t encode(
    uint32_t instruction_class,
    uint32_t opcode,
    uint32_t rd,
    uint32_t rs1,
    uint32_t rs2,
    uint32_t immediate
) {
    return instruction_class |
        ((opcode & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_OPCODE_SHIFT) |
        ((rd & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_RD_SHIFT) |
        ((rs1 & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_RS1_SHIFT) |
        ((rs2 & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_RS2_SHIFT) |
        (immediate & HOLON_NPU_ISA_IMM_MASK);
}

static uint32_t configure_i32x4(void) {
    const uint32_t immediate = 3u |
        (HOLON_NPU_ISA_VTYPE_SEW_32 << HOLON_NPU_ISA_VTYPE_SEW_SHIFT) |
        HOLON_NPU_ISA_VTYPE_SIGNED;
    return encode(
        HOLON_NPU_ISA_CLASS_VECTOR_CONFIG,
        HOLON_NPU_ISA_OPCODE_VECTOR_CONFIG_SET,
        0,
        0,
        0,
        immediate
    );
}

static uint32_t vector_memory(uint32_t opcode, uint32_t reg, uint32_t offset) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_MEMORY, opcode, reg, 0, 0, offset);
}

static uint32_t scalar_load(uint32_t reg, uint32_t offset) {
    return encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL,
                  HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_LOAD, reg, 0, 0, offset);
}

static uint32_t dma_store(void) {
    return encode(HOLON_NPU_ISA_CLASS_DMA, HOLON_NPU_ISA_OPCODE_DMA_STORE,
                  1, 2, 3, 3);
}

static uint32_t system_exit(void) {
    return encode(HOLON_NPU_ISA_CLASS_SYSTEM, HOLON_NPU_ISA_OPCODE_SYSTEM_EXIT,
                  0, 0, 0, 0);
}

static uint32_t system_fault(void) {
    return encode(HOLON_NPU_ISA_CLASS_SYSTEM, HOLON_NPU_ISA_OPCODE_SYSTEM_FAULT,
                  0, 0, 0, 0);
}

static uint32_t program[10] __attribute__((section(".holon_program"), aligned(16)));
static uint32_t arguments[40] __attribute__((section(".holon_arguments"), aligned(16))) = {
    1, 2, 3, 4,
    10, 20, 30, 40,
};
static uint32_t result[4] __attribute__((aligned(16)));
static holon_npu_completion_record_t completion
    __attribute__((section(".holon_completion"), aligned(16)));
static holon_npu_program_desc_t descriptor
    __attribute__((section(".holon_descriptor"), aligned(16)));

static void build_program(void) {
    program[0] = configure_i32x4();
    program[1] = vector_memory(HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_LOAD, 1, 0);
    program[2] = vector_memory(HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_LOAD, 2, 16);
    program[3] = encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU,
                        HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD, 3, 1, 2, 0);
    program[4] = vector_memory(HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_STORE, 3, 32);
    program[5] = scalar_load(1, 48);
    program[6] = scalar_load(2, 52);
    program[7] = scalar_load(3, 56);
    program[8] = dma_store();
    program[9] = system_exit();

    const uintptr_t result_address = (uintptr_t)result;
    arguments[12] = (uint32_t)result_address;
    arguments[13] = (uint32_t)(result_address >> 32u);
    arguments[14] = 32;

    descriptor = (holon_npu_program_desc_t){
        .size_bytes = HOLON_NPU_PROGRAM_DESC_SIZE,
        .version = HOLON_NPU_ABI_MAJOR,
        .program_format = HOLON_NPU_PROGRAM_FORMAT_HOLON,
        .holon_isa_major = HOLON_NPU_ISA_MAJOR,
        .holon_isa_minor = HOLON_NPU_ISA_MINOR,
        .required_caps = HOLON_NPU_CAP_PROGRAM_DESCRIPTOR |
                         HOLON_NPU_CAP_LOCAL_PROGRAM_MEMORY |
                         HOLON_NPU_CAP_ARGUMENT_SCRATCHPAD_COPY |
                         HOLON_NPU_CAP_IN_ORDER_DMA_QUEUE |
                         HOLON_NPU_CAP_INTEGER_VECTOR_BASE,
        .required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
                               HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
                               HOLON_NPU_PROGRAM_OP_CLASS_DMA |
                               HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        .code_addr = (uintptr_t)program,
        .code_size_bytes = sizeof(program),
        .entry_pc = 0,
        .arg_addr = (uintptr_t)arguments,
        .arg_size_bytes = 16 * sizeof(arguments[0]),
        .local_mem_bytes = 128,
        .program_mem_bytes = sizeof(program),
        .stack_bytes = 0,
        .completion_addr = (uintptr_t)&completion,
        .flags = HOLON_NPU_PROGRAM_FLAG_IRQ_ON_DONE,
    };
}

static void build_matrix_program(void) {
    const uintptr_t result_address = (uintptr_t)result;
    const uint32_t shape = 2u |
        (2u << HOLON_NPU_ISA_MATRIX_SHAPE_N_SHIFT) |
        (2u << HOLON_NPU_ISA_MATRIX_SHAPE_K_SHIFT) |
        ((HOLON_NPU_ISA_MATRIX_FLAG_CLEAR | HOLON_NPU_ISA_MATRIX_FLAG_STORE)
         << HOLON_NPU_ISA_MATRIX_SHAPE_FLAGS_SHIFT);

    for (uint32_t index = 0; index < 40; ++index) arguments[index] = 0;
    arguments[0] = 0x04030201u;
    arguments[8] = 0x08070605u;
    arguments[24] = 0;
    arguments[25] = 32;
    arguments[26] = 64;
    arguments[27] = 2;
    arguments[28] = 2;
    arguments[29] = 8;
    arguments[30] = shape;
    arguments[31] = 0;
    arguments[32] = (uint32_t)result_address;
    arguments[33] = (uint32_t)(result_address >> 32u);
    arguments[34] = 64;

    program[0] = encode(HOLON_NPU_ISA_CLASS_MATRIX,
                        HOLON_NPU_ISA_OPCODE_MATRIX_GEMM, 0, 0, 0, 96);
    program[1] = scalar_load(1, 128);
    program[2] = scalar_load(2, 132);
    program[3] = scalar_load(3, 136);
    program[4] = dma_store();
    program[5] = system_exit();

    descriptor.code_size_bytes = 6 * sizeof(program[0]);
    descriptor.arg_size_bytes = sizeof(arguments);
    descriptor.local_mem_bytes = sizeof(arguments);
    descriptor.program_mem_bytes = descriptor.code_size_bytes;
    descriptor.required_caps = HOLON_NPU_CAP_PROGRAM_DESCRIPTOR |
                               HOLON_NPU_CAP_LOCAL_PROGRAM_MEMORY |
                               HOLON_NPU_CAP_ARGUMENT_SCRATCHPAD_COPY |
                               HOLON_NPU_CAP_IN_ORDER_DMA_QUEUE |
                               HOLON_NPU_CAP_MATRIX_MICRO_OP;
    descriptor.required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
                                     HOLON_NPU_PROGRAM_OP_CLASS_MATRIX |
                                     HOLON_NPU_PROGRAM_OP_CLASS_DMA |
                                     HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM;
    descriptor.flags = HOLON_NPU_PROGRAM_FLAG_IRQ_ON_DONE;
}

static void write_descriptor_address(void) {
    const uintptr_t descriptor_address = (uintptr_t)&descriptor;
    npu[HOLON_NPU_REG_PROGRAM_DESC_ADDR_LO / 4] = (uint32_t)descriptor_address;
    npu[HOLON_NPU_REG_PROGRAM_DESC_ADDR_HI / 4] = (uint32_t)(descriptor_address >> 32u);
}

static uint32_t wait_for_status(uint32_t mask) {
    uint32_t status = 0;
    for (uint32_t timeout = 0; timeout < 1000000u; ++timeout) {
        status = npu[HOLON_NPU_REG_STATUS / 4];
        if ((status & mask) != 0) break;
    }
    return status;
}

int main(void) {
    build_program();
    if (npu[HOLON_NPU_REG_DEVICE_ID / 4] != HOLON_NPU_RESET_DEVICE_ID ||
        npu[HOLON_NPU_REG_ABI_VERSION / 4] != HOLON_NPU_ABI_VERSION_RESET) {
        return 1;
    }

    write_descriptor_address();
    npu[HOLON_NPU_REG_IRQ_ENABLE / 4] = HOLON_NPU_IRQ_DONE | HOLON_NPU_IRQ_FAULT;
    host_memory_fence();
    npu[HOLON_NPU_REG_DOORBELL / 4] = HOLON_NPU_DOORBELL_START;

    uint32_t status = wait_for_status(HOLON_NPU_STATUS_DONE | HOLON_NPU_STATUS_FAULT);
    host_memory_fence();
    if ((status & HOLON_NPU_STATUS_DONE) == 0 ||
        npu[HOLON_NPU_REG_FAULT_CODE / 4] != HOLON_NPU_FAULT_NONE ||
        (npu[HOLON_NPU_REG_IRQ_STATUS / 4] & HOLON_NPU_IRQ_DONE) == 0 ||
        completion.status != HOLON_NPU_COMPLETION_STATUS_DONE ||
        completion.instret != 10 || completion.debug_pc != 40 ||
        completion.cycle_count == 0 ||
        npu[HOLON_NPU_REG_PERF_INSTRET_LO / 4] != 10 ||
        npu[HOLON_NPU_REG_PERF_CYCLE_LO / 4] == 0) {
        return 2;
    }
    if (result[0] != 11 || result[1] != 22 || result[2] != 33 || result[3] != 44) {
        return 3;
    }

    npu[HOLON_NPU_REG_IRQ_CLEAR / 4] = HOLON_NPU_IRQ_DONE;
    npu[HOLON_NPU_REG_CONTROL / 4] = HOLON_NPU_CONTROL_CLEAR_TERMINAL;
    if ((npu[HOLON_NPU_REG_STATUS / 4] & HOLON_NPU_STATUS_IDLE) == 0) return 4;

    build_matrix_program();
    completion = (holon_npu_completion_record_t){0};
    for (uint32_t index = 0; index < 4; ++index) result[index] = 0;
    write_descriptor_address();
    host_memory_fence();
    npu[HOLON_NPU_REG_DOORBELL / 4] = HOLON_NPU_DOORBELL_START;
    status = wait_for_status(HOLON_NPU_STATUS_DONE | HOLON_NPU_STATUS_FAULT);
    host_memory_fence();
    if ((status & HOLON_NPU_STATUS_DONE) == 0)
        return 64 + npu[HOLON_NPU_REG_FAULT_CODE / 4];
    if (completion.status != HOLON_NPU_COMPLETION_STATUS_DONE) return 51;
    if (result[0] != 19 || result[1] != 22 || result[2] != 43 || result[3] != 50)
        return 52;
    npu[HOLON_NPU_REG_IRQ_CLEAR / 4] = HOLON_NPU_IRQ_DONE;
    npu[HOLON_NPU_REG_CONTROL / 4] = HOLON_NPU_CONTROL_CLEAR_TERMINAL;

    build_program();
    completion = (holon_npu_completion_record_t){0};
    write_descriptor_address();
    host_memory_fence();
    npu[HOLON_NPU_REG_DOORBELL / 4] = HOLON_NPU_DOORBELL_START;
    npu[HOLON_NPU_REG_CONTROL / 4] = HOLON_NPU_CONTROL_SOFT_RESET;
    status = npu[HOLON_NPU_REG_STATUS / 4];
    if ((status & (HOLON_NPU_STATUS_RESETTING | HOLON_NPU_STATUS_IDLE)) == 0) return 6;
    status = wait_for_status(HOLON_NPU_STATUS_IDLE);
    if ((status & HOLON_NPU_STATUS_IDLE) == 0 ||
        npu[HOLON_NPU_REG_FAULT_CODE / 4] != HOLON_NPU_FAULT_NONE ||
        npu[HOLON_NPU_REG_IRQ_STATUS / 4] != 0 ||
        npu[HOLON_NPU_REG_PERF_CYCLE_LO / 4] != 0 ||
        npu[HOLON_NPU_REG_PERF_INSTRET_LO / 4] != 0) {
        return 7;
    }

    program[0] = system_fault();
    completion = (holon_npu_completion_record_t){0};
    descriptor.code_size_bytes = sizeof(program[0]);
    descriptor.arg_size_bytes = 0;
    descriptor.local_mem_bytes = 16;
    descriptor.program_mem_bytes = sizeof(program[0]);
    descriptor.required_caps = HOLON_NPU_CAP_PROGRAM_DESCRIPTOR |
                               HOLON_NPU_CAP_LOCAL_PROGRAM_MEMORY;
    descriptor.required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM;
    descriptor.flags = HOLON_NPU_PROGRAM_FLAG_IRQ_ON_FAULT;
    write_descriptor_address();
    host_memory_fence();
    npu[HOLON_NPU_REG_DOORBELL / 4] = HOLON_NPU_DOORBELL_START;
    status = wait_for_status(HOLON_NPU_STATUS_DONE | HOLON_NPU_STATUS_FAULT);
    host_memory_fence();
    if ((status & HOLON_NPU_STATUS_FAULT) == 0 ||
        npu[HOLON_NPU_REG_FAULT_CODE / 4] != HOLON_NPU_FAULT_EXPLICIT_PROGRAM_FAULT ||
        (npu[HOLON_NPU_REG_IRQ_STATUS / 4] & HOLON_NPU_IRQ_FAULT) == 0 ||
        completion.status != HOLON_NPU_COMPLETION_STATUS_FAULT ||
        completion.fault_code != HOLON_NPU_FAULT_EXPLICIT_PROGRAM_FAULT ||
        completion.debug_pc != 0 || completion.instret != 0) {
        return 8;
    }
    return 0;
}
