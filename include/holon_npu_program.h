/* Generated from spec/holon_npu_abi.json by tools/gen_abi.py. Do not edit. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "holon_npu_isa.h"

static constexpr uint8_t HOLON_NPU_ABI_MAJOR                       = 0x03u;
static constexpr uint8_t HOLON_NPU_ABI_MINOR                       = 0x00u;
static constexpr uint32_t HOLON_NPU_ABI_VERSION_RESET               = 0x00030000u;

static constexpr uint16_t HOLON_NPU_PROGRAM_DESC_SIZE               = 0x0080u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_ALIGN              = 0x00000010u;
static constexpr uint32_t HOLON_NPU_PROGRAM_IMAGE_ALIGN             = 0x00000004u;
static constexpr uint32_t HOLON_NPU_PROGRAM_ARGUMENT_ALIGN          = 0x00000010u;
static constexpr uint32_t HOLON_NPU_PROGRAM_COMPLETION_ALIGN        = 0x00000010u;
static constexpr uint16_t HOLON_NPU_COMPLETION_RECORD_SIZE          = 0x0020u;
static constexpr uint8_t HOLON_NPU_PROGRAM_FORMAT_HOLON            = 0x01u;
static constexpr uint32_t HOLON_NPU_PROGRAM_MEM_MAX_BYTES           = 0x00010000u;
static constexpr uint32_t HOLON_NPU_LOCAL_MEM_MAX_BYTES             = 0x00040000u;
static constexpr uint32_t HOLON_NPU_PROGRAM_STACK_MAX_BYTES         = 0x00004000u;

/* Register offsets. */
static constexpr uint32_t HOLON_NPU_REG_DEVICE_ID           = 0x000u;
static constexpr uint32_t HOLON_NPU_REG_ABI_VERSION         = 0x004u;
static constexpr uint32_t HOLON_NPU_REG_ISA_VERSION         = 0x008u;
static constexpr uint32_t HOLON_NPU_REG_CAP0_LO             = 0x00Cu;
static constexpr uint32_t HOLON_NPU_REG_CAP0_HI             = 0x010u;
static constexpr uint32_t HOLON_NPU_REG_OP_CLASS_LO         = 0x014u;
static constexpr uint32_t HOLON_NPU_REG_OP_CLASS_HI         = 0x018u;
static constexpr uint32_t HOLON_NPU_REG_PROGRAM_MEM_BYTES   = 0x01Cu;
static constexpr uint32_t HOLON_NPU_REG_LOCAL_MEM_BYTES     = 0x020u;
static constexpr uint32_t HOLON_NPU_REG_VECTOR_CAP0         = 0x024u;
static constexpr uint32_t HOLON_NPU_REG_MATRIX_CAP0         = 0x028u;
static constexpr uint32_t HOLON_NPU_REG_CONTROL             = 0x030u;
static constexpr uint32_t HOLON_NPU_REG_STATUS              = 0x034u;
static constexpr uint32_t HOLON_NPU_REG_FAULT_CODE          = 0x038u;
static constexpr uint32_t HOLON_NPU_REG_DEBUG_PC            = 0x03Cu;
static constexpr uint32_t HOLON_NPU_REG_PROGRAM_DESC_ADDR_LO = 0x040u;
static constexpr uint32_t HOLON_NPU_REG_PROGRAM_DESC_ADDR_HI = 0x044u;
static constexpr uint32_t HOLON_NPU_REG_DOORBELL            = 0x048u;
static constexpr uint32_t HOLON_NPU_REG_IRQ_ENABLE          = 0x04Cu;
static constexpr uint32_t HOLON_NPU_REG_IRQ_STATUS          = 0x050u;
static constexpr uint32_t HOLON_NPU_REG_IRQ_CLEAR           = 0x054u;
static constexpr uint32_t HOLON_NPU_REG_PERF_CYCLE_LO       = 0x060u;
static constexpr uint32_t HOLON_NPU_REG_PERF_CYCLE_HI       = 0x064u;
static constexpr uint32_t HOLON_NPU_REG_PERF_INSTRET_LO     = 0x068u;
static constexpr uint32_t HOLON_NPU_REG_PERF_INSTRET_HI     = 0x06Cu;

/* Register reset values. */
static constexpr uint32_t HOLON_NPU_RESET_DEVICE_ID           = 0x4E505502u;
static constexpr uint32_t HOLON_NPU_RESET_ABI_VERSION         = 0x00030000u;
static constexpr uint32_t HOLON_NPU_RESET_ISA_VERSION         = 0x00010000u;
static constexpr uint32_t HOLON_NPU_RESET_CAP0_LO             = 0x0000007Fu;
static constexpr uint32_t HOLON_NPU_RESET_CAP0_HI             = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_OP_CLASS_LO         = 0x000001FFu;
static constexpr uint32_t HOLON_NPU_RESET_OP_CLASS_HI         = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_PROGRAM_MEM_BYTES   = 0x00010000u;
static constexpr uint32_t HOLON_NPU_RESET_LOCAL_MEM_BYTES     = 0x00040000u;
static constexpr uint32_t HOLON_NPU_RESET_VECTOR_CAP0         = 0x01010010u;
static constexpr uint32_t HOLON_NPU_RESET_MATRIX_CAP0         = 0x08201010u;
static constexpr uint32_t HOLON_NPU_RESET_CONTROL             = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_STATUS              = 0x00000001u;
static constexpr uint32_t HOLON_NPU_RESET_FAULT_CODE          = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_DEBUG_PC            = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_PROGRAM_DESC_ADDR_LO = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_PROGRAM_DESC_ADDR_HI = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_DOORBELL            = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_IRQ_ENABLE          = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_IRQ_STATUS          = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_IRQ_CLEAR           = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_PERF_CYCLE_LO       = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_PERF_CYCLE_HI       = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_PERF_INSTRET_LO     = 0x00000000u;
static constexpr uint32_t HOLON_NPU_RESET_PERF_INSTRET_HI     = 0x00000000u;

/* Lifecycle status bits. */
static constexpr uint32_t HOLON_NPU_STATUS_IDLE       = 0x00000001u;
static constexpr uint32_t HOLON_NPU_STATUS_LOADING    = 0x00000002u;
static constexpr uint32_t HOLON_NPU_STATUS_RUNNING    = 0x00000004u;
static constexpr uint32_t HOLON_NPU_STATUS_HALTED     = 0x00000008u;
static constexpr uint32_t HOLON_NPU_STATUS_DONE       = 0x00000010u;
static constexpr uint32_t HOLON_NPU_STATUS_FAULT      = 0x00000020u;
static constexpr uint32_t HOLON_NPU_STATUS_IRQ_PENDING = 0x00000040u;
static constexpr uint32_t HOLON_NPU_STATUS_RESETTING  = 0x00000080u;

/* Program descriptor flags. */
static constexpr uint32_t HOLON_NPU_PROGRAM_FLAG_IRQ_ON_DONE            = 0x00000001u;
static constexpr uint32_t HOLON_NPU_PROGRAM_FLAG_IRQ_ON_FAULT           = 0x00000002u;
static constexpr uint32_t HOLON_NPU_PROGRAM_FLAG_CLEAR_PERF_ON_START    = 0x00000004u;
static constexpr uint32_t HOLON_NPU_PROGRAM_FLAG_DEBUG_SNAPSHOT_ON_FAULT = 0x00000008u;
static constexpr uint32_t HOLON_NPU_PROGRAM_FLAG_VALID_MASK             = 0x0000000Fu;

/* Control bits. */
static constexpr uint32_t HOLON_NPU_CONTROL_SOFT_RESET    = 0x00000001u;
static constexpr uint32_t HOLON_NPU_CONTROL_CLEAR_TERMINAL = 0x00000002u;
static constexpr uint32_t HOLON_NPU_CONTROL_HALT          = 0x00000004u;
static constexpr uint32_t HOLON_NPU_CONTROL_RESUME        = 0x00000008u;
static constexpr uint32_t HOLON_NPU_CONTROL_DEBUG_STEP    = 0x00000010u;
static constexpr uint32_t HOLON_NPU_CONTROL_VALID_MASK    = 0x0000001Fu;

/* Doorbell bits. */
static constexpr uint32_t HOLON_NPU_DOORBELL_START     = 0x00000001u;
static constexpr uint32_t HOLON_NPU_DOORBELL_VALID_MASK = 0x00000001u;

/* IRQ bits. */
static constexpr uint32_t HOLON_NPU_IRQ_DONE      = 0x00000001u;
static constexpr uint32_t HOLON_NPU_IRQ_FAULT     = 0x00000002u;
static constexpr uint32_t HOLON_NPU_IRQ_HALTED    = 0x00000004u;
static constexpr uint32_t HOLON_NPU_IRQ_DEBUG_STEP = 0x00000008u;
static constexpr uint32_t HOLON_NPU_IRQ_VALID_MASK = 0x0000000Fu;

/* Required operation class bits. */
static constexpr uint64_t HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL = 0x0000000000000001u;
static constexpr uint64_t HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE       = 0x0000000000000002u;
static constexpr uint64_t HOLON_NPU_PROGRAM_OP_CLASS_VECTOR          = 0x0000000000000004u;
static constexpr uint64_t HOLON_NPU_PROGRAM_OP_CLASS_QUANTIZATION    = 0x0000000000000008u;
static constexpr uint64_t HOLON_NPU_PROGRAM_OP_CLASS_MATRIX          = 0x0000000000000010u;
static constexpr uint64_t HOLON_NPU_PROGRAM_OP_CLASS_DMA             = 0x0000000000000020u;
static constexpr uint64_t HOLON_NPU_PROGRAM_OP_CLASS_CSR_DEBUG       = 0x0000000000000040u;
static constexpr uint64_t HOLON_NPU_PROGRAM_OP_CLASS_SYNC            = 0x0000000000000080u;
static constexpr uint64_t HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM          = 0x0000000000000100u;

/* Capability bits. */
static constexpr uint64_t HOLON_NPU_CAP_PROGRAM_DESCRIPTOR      = 0x0000000000000001u;
static constexpr uint64_t HOLON_NPU_CAP_LOCAL_PROGRAM_MEMORY    = 0x0000000000000002u;
static constexpr uint64_t HOLON_NPU_CAP_ARGUMENT_SCRATCHPAD_COPY = 0x0000000000000004u;
static constexpr uint64_t HOLON_NPU_CAP_IN_ORDER_DMA_QUEUE      = 0x0000000000000008u;
static constexpr uint64_t HOLON_NPU_CAP_MATRIX_MICRO_OP         = 0x0000000000000010u;
static constexpr uint64_t HOLON_NPU_CAP_INTEGER_VECTOR_BASE     = 0x0000000000000020u;
static constexpr uint64_t HOLON_NPU_CAP_QUANT_VECTOR            = 0x0000000000000040u;

/* Fault codes. */
static constexpr uint32_t HOLON_NPU_FAULT_NONE                       = 0x00u;
static constexpr uint32_t HOLON_NPU_FAULT_INVALID_PROGRAM_DESCRIPTOR = 0x01u;
static constexpr uint32_t HOLON_NPU_FAULT_UNSUPPORTED_ABI_OR_ISA     = 0x02u;
static constexpr uint32_t HOLON_NPU_FAULT_UNSUPPORTED_PROGRAM_FORMAT = 0x03u;
static constexpr uint32_t HOLON_NPU_FAULT_UNSUPPORTED_CAPABILITY     = 0x04u;
static constexpr uint32_t HOLON_NPU_FAULT_UNSUPPORTED_OPERATION_CLASS = 0x05u;
static constexpr uint32_t HOLON_NPU_FAULT_ALIGNMENT                  = 0x06u;
static constexpr uint32_t HOLON_NPU_FAULT_LOCAL_MEMORY_BOUNDS        = 0x07u;
static constexpr uint32_t HOLON_NPU_FAULT_ILLEGAL_INSTRUCTION        = 0x08u;
static constexpr uint32_t HOLON_NPU_FAULT_VECTOR_CONFIG              = 0x09u;
static constexpr uint32_t HOLON_NPU_FAULT_MATRIX_ISSUE               = 0x0Au;
static constexpr uint32_t HOLON_NPU_FAULT_DMA_REQUEST                = 0x0Bu;
static constexpr uint32_t HOLON_NPU_FAULT_AXI_READ                   = 0x0Cu;
static constexpr uint32_t HOLON_NPU_FAULT_AXI_WRITE                  = 0x0Du;
static constexpr uint32_t HOLON_NPU_FAULT_EXPLICIT_PROGRAM_FAULT     = 0x0Eu;

/* Completion status values. */
static constexpr uint32_t HOLON_NPU_COMPLETION_STATUS_DONE = 0x00000001u;
static constexpr uint32_t HOLON_NPU_COMPLETION_STATUS_FAULT = 0x00000002u;

/* Program descriptor offsets. */
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_SIZE_BYTES         = 0x00u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_VERSION            = 0x02u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_PROGRAM_FORMAT     = 0x03u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_HOLON_ISA_MAJOR    = 0x04u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_HOLON_ISA_MINOR    = 0x06u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_REQUIRED_CAPS      = 0x08u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_REQUIRED_OP_CLASSES = 0x10u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_CODE_ADDR          = 0x18u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_CODE_SIZE          = 0x20u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_ENTRY_PC           = 0x24u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_ARG_ADDR           = 0x28u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_ARG_SIZE           = 0x30u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_LOCAL_MEM_BYTES    = 0x34u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_PROGRAM_MEM_BYTES  = 0x38u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_STACK_BYTES        = 0x3Cu;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_COMPLETION_ADDR    = 0x40u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_FLAGS              = 0x48u;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_RESERVED_4C        = 0x4Cu;
static constexpr uint32_t HOLON_NPU_PROGRAM_DESC_OFF_RESERVED_TAIL      = 0x50u;

/* Completion record offsets. */
static constexpr uint32_t HOLON_NPU_COMPLETION_OFF_ABI_VERSION = 0x00u;
static constexpr uint32_t HOLON_NPU_COMPLETION_OFF_STATUS     = 0x04u;
static constexpr uint32_t HOLON_NPU_COMPLETION_OFF_FAULT_CODE = 0x08u;
static constexpr uint32_t HOLON_NPU_COMPLETION_OFF_DEBUG_PC   = 0x0Cu;
static constexpr uint32_t HOLON_NPU_COMPLETION_OFF_CYCLE_COUNT = 0x10u;
static constexpr uint32_t HOLON_NPU_COMPLETION_OFF_INSTRET    = 0x18u;

typedef struct holon_npu_program_desc {
    uint16_t size_bytes;
    uint8_t version;
    uint8_t program_format;
    uint16_t holon_isa_major;
    uint16_t holon_isa_minor;
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
    uint32_t reserved_4c;
    uint64_t reserved_50;
    uint64_t reserved_58;
    uint64_t reserved_60;
    uint64_t reserved_68;
    uint64_t reserved_70;
    uint64_t reserved_78;
} holon_npu_program_desc_t;

typedef struct holon_npu_completion_record {
    uint32_t abi_version;
    uint32_t status;
    uint32_t fault_code;
    uint32_t debug_pc;
    uint64_t cycle_count;
    uint64_t instret;
} holon_npu_completion_record_t;

static_assert(sizeof(holon_npu_program_desc_t) == HOLON_NPU_PROGRAM_DESC_SIZE);
static_assert(sizeof(holon_npu_completion_record_t) == HOLON_NPU_COMPLETION_RECORD_SIZE);
static_assert(HOLON_NPU_ABI_VERSION_RESET == 0x00030000u);
static_assert(HOLON_NPU_PROGRAM_FORMAT_HOLON == 0x01u);
static_assert(HOLON_NPU_ISA_MAJOR == 0x01u);
static_assert(offsetof(holon_npu_program_desc_t, size_bytes) == HOLON_NPU_PROGRAM_DESC_OFF_SIZE_BYTES);
static_assert(offsetof(holon_npu_program_desc_t, version) == HOLON_NPU_PROGRAM_DESC_OFF_VERSION);
static_assert(offsetof(holon_npu_program_desc_t, program_format) == HOLON_NPU_PROGRAM_DESC_OFF_PROGRAM_FORMAT);
static_assert(offsetof(holon_npu_program_desc_t, holon_isa_major) == HOLON_NPU_PROGRAM_DESC_OFF_HOLON_ISA_MAJOR);
static_assert(offsetof(holon_npu_program_desc_t, holon_isa_minor) == HOLON_NPU_PROGRAM_DESC_OFF_HOLON_ISA_MINOR);
static_assert(offsetof(holon_npu_program_desc_t, required_caps) == HOLON_NPU_PROGRAM_DESC_OFF_REQUIRED_CAPS);
static_assert(offsetof(holon_npu_program_desc_t, required_op_classes) == HOLON_NPU_PROGRAM_DESC_OFF_REQUIRED_OP_CLASSES);
static_assert(offsetof(holon_npu_program_desc_t, code_addr) == HOLON_NPU_PROGRAM_DESC_OFF_CODE_ADDR);
static_assert(offsetof(holon_npu_program_desc_t, code_size_bytes) == HOLON_NPU_PROGRAM_DESC_OFF_CODE_SIZE);
static_assert(offsetof(holon_npu_program_desc_t, entry_pc) == HOLON_NPU_PROGRAM_DESC_OFF_ENTRY_PC);
static_assert(offsetof(holon_npu_program_desc_t, arg_addr) == HOLON_NPU_PROGRAM_DESC_OFF_ARG_ADDR);
static_assert(offsetof(holon_npu_program_desc_t, arg_size_bytes) == HOLON_NPU_PROGRAM_DESC_OFF_ARG_SIZE);
static_assert(offsetof(holon_npu_program_desc_t, local_mem_bytes) == HOLON_NPU_PROGRAM_DESC_OFF_LOCAL_MEM_BYTES);
static_assert(offsetof(holon_npu_program_desc_t, program_mem_bytes) == HOLON_NPU_PROGRAM_DESC_OFF_PROGRAM_MEM_BYTES);
static_assert(offsetof(holon_npu_program_desc_t, stack_bytes) == HOLON_NPU_PROGRAM_DESC_OFF_STACK_BYTES);
static_assert(offsetof(holon_npu_program_desc_t, completion_addr) == HOLON_NPU_PROGRAM_DESC_OFF_COMPLETION_ADDR);
static_assert(offsetof(holon_npu_program_desc_t, flags) == HOLON_NPU_PROGRAM_DESC_OFF_FLAGS);
static_assert(offsetof(holon_npu_program_desc_t, reserved_4c) == HOLON_NPU_PROGRAM_DESC_OFF_RESERVED_4C);
static_assert(offsetof(holon_npu_program_desc_t, reserved_50) == HOLON_NPU_PROGRAM_DESC_OFF_RESERVED_TAIL);
static_assert(offsetof(holon_npu_completion_record_t, abi_version) == HOLON_NPU_COMPLETION_OFF_ABI_VERSION);
static_assert(offsetof(holon_npu_completion_record_t, status) == HOLON_NPU_COMPLETION_OFF_STATUS);
static_assert(offsetof(holon_npu_completion_record_t, fault_code) == HOLON_NPU_COMPLETION_OFF_FAULT_CODE);
static_assert(offsetof(holon_npu_completion_record_t, debug_pc) == HOLON_NPU_COMPLETION_OFF_DEBUG_PC);
static_assert(offsetof(holon_npu_completion_record_t, cycle_count) == HOLON_NPU_COMPLETION_OFF_CYCLE_COUNT);
static_assert(offsetof(holon_npu_completion_record_t, instret) == HOLON_NPU_COMPLETION_OFF_INSTRET);
