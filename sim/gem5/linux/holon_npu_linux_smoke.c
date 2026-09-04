#include "holon_npu_isa.h"
#include "holon_npu_program.h"
#include "holon_npu_sim_uapi.h"

#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum buffer_offset {
    descriptor_offset = 0,
    program_offset = 128,
    argument_offset = 256,
    result_offset = 512,
    completion_offset = 576,
};

static uint32_t encode(
    uint32_t instruction_class,
    uint32_t opcode,
    uint32_t rd,
    uint32_t rs1,
    uint32_t rs2,
    uint32_t immediate
)
{
    return instruction_class |
        ((opcode & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_OPCODE_SHIFT) |
        ((rd & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_RD_SHIFT) |
        ((rs1 & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_RS1_SHIFT) |
        ((rs2 & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_RS2_SHIFT) |
        (immediate & HOLON_NPU_ISA_IMM_MASK);
}

static void build_program(
    void *buffer,
    const struct holon_npu_sim_snapshot *device
)
{
    uint8_t *bytes = buffer;
    uint32_t *program = (uint32_t *)(bytes + program_offset);
    uint32_t *arguments = (uint32_t *)(bytes + argument_offset);
    holon_npu_program_desc_t *descriptor =
        (holon_npu_program_desc_t *)(bytes + descriptor_offset);
    uint32_t vtype = 3u |
        (HOLON_NPU_ISA_VTYPE_SEW_32 << HOLON_NPU_ISA_VTYPE_SEW_SHIFT) |
        HOLON_NPU_ISA_VTYPE_SIGNED;
    uint64_t result_address = device->dma_address + result_offset;

    arguments[0] = 1;
    arguments[1] = 2;
    arguments[2] = 3;
    arguments[3] = 4;
    arguments[4] = 10;
    arguments[5] = 20;
    arguments[6] = 30;
    arguments[7] = 40;
    arguments[12] = (uint32_t)result_address;
    arguments[13] = (uint32_t)(result_address >> 32);
    arguments[14] = 32;

    program[0] = encode(HOLON_NPU_ISA_CLASS_VECTOR_CONFIG,
                        HOLON_NPU_ISA_OPCODE_VECTOR_CONFIG_SET, 0, 0, 0, vtype);
    program[1] = encode(HOLON_NPU_ISA_CLASS_VECTOR_MEMORY,
                        HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_LOAD, 1, 0, 0, 0);
    program[2] = encode(HOLON_NPU_ISA_CLASS_VECTOR_MEMORY,
                        HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_LOAD, 2, 0, 0, 16);
    program[3] = encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU,
                        HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD, 3, 1, 2, 0);
    program[4] = encode(HOLON_NPU_ISA_CLASS_VECTOR_MEMORY,
                        HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_STORE, 3, 0, 0, 32);
    program[5] = encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL,
                        HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_LOAD, 1, 0, 0, 48);
    program[6] = encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL,
                        HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_LOAD, 2, 0, 0, 52);
    program[7] = encode(HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL,
                        HOLON_NPU_ISA_OPCODE_FRONTEND_CONTROL_LOAD, 3, 0, 0, 56);
    program[8] = encode(HOLON_NPU_ISA_CLASS_DMA,
                        HOLON_NPU_ISA_OPCODE_DMA_STORE, 1, 2, 3, 3);
    program[9] = encode(HOLON_NPU_ISA_CLASS_SYSTEM,
                        HOLON_NPU_ISA_OPCODE_SYSTEM_EXIT, 0, 0, 0, 0);

    *descriptor = (holon_npu_program_desc_t){
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
        .code_addr = program_offset,
        .code_size_bytes = 10 * sizeof(uint32_t),
        .entry_pc = 0,
        .arg_addr = argument_offset,
        .arg_size_bytes = 64,
        .local_mem_bytes = 128,
        .program_mem_bytes = 10 * sizeof(uint32_t),
        .stack_bytes = 0,
        .completion_addr = completion_offset,
        .flags = HOLON_NPU_PROGRAM_FLAG_IRQ_ON_DONE,
    };
}

int main(void)
{
    struct holon_npu_sim_snapshot snapshot;
    struct pollfd poll_descriptor;
    uint64_t submit_offset = descriptor_offset;
    uint32_t *result;
    holon_npu_completion_record_t *completion;
    void *buffer;
    int device;

    device = open("/dev/holonnpu-sim", O_RDWR);
    if (device < 0) {
        perror("open");
        return 1;
    }
    if (read(device, &snapshot, sizeof(snapshot)) != sizeof(snapshot) ||
        snapshot.dma_bytes != HOLON_NPU_SIM_DMA_BUFFER_BYTES) {
        perror("read capabilities");
        return 2;
    }
    buffer = mmap(NULL, snapshot.dma_bytes, PROT_READ | PROT_WRITE,
                  MAP_SHARED, device, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap");
        return 3;
    }
    memset(buffer, 0, snapshot.dma_bytes);
    build_program(buffer, &snapshot);
    if (write(device, &submit_offset, sizeof(submit_offset)) != sizeof(submit_offset)) {
        perror("submit");
        return 4;
    }

    poll_descriptor = (struct pollfd){.fd = device, .events = POLLIN};
    if (poll(&poll_descriptor, 1, 30000) != 1) {
        perror("poll");
        return 5;
    }
    if (read(device, &snapshot, sizeof(snapshot)) != sizeof(snapshot) ||
        !(snapshot.status & HOLON_NPU_STATUS_DONE)) {
        fprintf(stderr, "HolonNPU did not complete: status=%#x fault=%#x\n",
                snapshot.status, snapshot.fault_code);
        return 6;
    }
    result = (uint32_t *)((uint8_t *)buffer + result_offset);
    completion = (holon_npu_completion_record_t *)((uint8_t *)buffer + completion_offset);
    if (result[0] != 11 || result[1] != 22 || result[2] != 33 || result[3] != 44 ||
        completion->status != HOLON_NPU_COMPLETION_STATUS_DONE) {
        fprintf(stderr, "HolonNPU result mismatch\n");
        return 7;
    }
    printf("HolonNPU Linux smoke passed: cycles=%llu instret=%llu\n",
           (unsigned long long)snapshot.cycles,
           (unsigned long long)snapshot.instructions_retired);
    return 0;
}
