/* Generated from spec/holon_npu_isa.json by tools/gen_isa.py. Do not edit. */
#pragma once

#include <stdint.h>

static constexpr uint8_t HOLON_NPU_ISA_MAJOR               = 0x01u;
static constexpr uint8_t HOLON_NPU_ISA_MINOR               = 0x00u;
static constexpr uint8_t HOLON_NPU_ISA_INSTRUCTION_BYTES   = 0x04u;
static constexpr uint8_t HOLON_NPU_ISA_INSTRUCTION_BITS    = 0x20u;

static constexpr uint32_t HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL     = 0x00000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_PREDICATE            = 0x10000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_CONFIG        = 0x20000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_ALU           = 0x30000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_MEMORY        = 0x40000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE       = 0x50000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_REDUCTION     = 0x60000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_QUANTIZATION         = 0x70000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_MATRIX               = 0x80000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_DMA                  = 0x90000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_CSR_DEBUG            = 0xA0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_SYNC                 = 0xB0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_SYSTEM               = 0xC0000000u;

static constexpr uint32_t HOLON_NPU_ISA_CLASS_FRONTEND_CONTROL_MASK = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_PREDICATE_MASK       = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_CONFIG_MASK   = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_ALU_MASK      = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_MEMORY_MASK   = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE_MASK  = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_VECTOR_REDUCTION_MASK = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_QUANTIZATION_MASK    = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_MATRIX_MASK          = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_DMA_MASK             = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_CSR_DEBUG_MASK       = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_SYNC_MASK            = 0xF0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_SYSTEM_MASK          = 0xF0000000u;

static constexpr uint32_t HOLON_NPU_ISA_CLASS_RESERVED_D           = 0xD0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_RESERVED_E           = 0xE0000000u;
static constexpr uint32_t HOLON_NPU_ISA_CLASS_RESERVED_F           = 0xF0000000u;

typedef enum holon_npu_isa_class {
    HOLON_NPU_ISA_ENUM_FRONTEND_CONTROL = 0x00000000u,
    HOLON_NPU_ISA_ENUM_PREDICATE = 0x10000000u,
    HOLON_NPU_ISA_ENUM_VECTOR_CONFIG = 0x20000000u,
    HOLON_NPU_ISA_ENUM_VECTOR_ALU = 0x30000000u,
    HOLON_NPU_ISA_ENUM_VECTOR_MEMORY = 0x40000000u,
    HOLON_NPU_ISA_ENUM_VECTOR_PERMUTE = 0x50000000u,
    HOLON_NPU_ISA_ENUM_VECTOR_REDUCTION = 0x60000000u,
    HOLON_NPU_ISA_ENUM_QUANTIZATION = 0x70000000u,
    HOLON_NPU_ISA_ENUM_MATRIX = 0x80000000u,
    HOLON_NPU_ISA_ENUM_DMA = 0x90000000u,
    HOLON_NPU_ISA_ENUM_CSR_DEBUG = 0xA0000000u,
    HOLON_NPU_ISA_ENUM_SYNC = 0xB0000000u,
    HOLON_NPU_ISA_ENUM_SYSTEM = 0xC0000000u,
    HOLON_NPU_ISA_ENUM_RESERVED_D = 0xD0000000u,
    HOLON_NPU_ISA_ENUM_RESERVED_E = 0xE0000000u,
    HOLON_NPU_ISA_ENUM_RESERVED_F = 0xF0000000u
} holon_npu_isa_class_t;
