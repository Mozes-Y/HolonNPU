/* Generated from spec/holon_npu_abi.json by tools/gen_abi.py. Do not edit. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "holon_npu_regs.h"

static constexpr uint8_t HOLON_NPU_OPCODE_GEMM_I8I8I32 = 0x01u;

static constexpr uint32_t HOLON_NPU_DESC_FLAG_IRQ_ON_DONE        = 0x00000001u;
static constexpr uint32_t HOLON_NPU_DESC_FLAG_IRQ_ON_ERROR       = 0x00000002u;
static constexpr uint32_t HOLON_NPU_DESC_FLAG_CLEAR_PERF_ON_START = 0x00000004u;
static constexpr uint32_t HOLON_NPU_DESC_FLAG_VALID_MASK         = 0x00000007u;

static constexpr uint32_t HOLON_NPU_DESC_OFF_SIZE_BYTES   = 0x00u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_VERSION      = 0x02u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_OPCODE       = 0x03u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_FLAGS        = 0x04u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_M            = 0x08u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_N            = 0x0Cu;
static constexpr uint32_t HOLON_NPU_DESC_OFF_K            = 0x10u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_RESERVED_14  = 0x14u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_A_ADDR       = 0x18u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_B_ADDR       = 0x20u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_C_ADDR       = 0x28u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_A_STRIDE     = 0x30u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_B_STRIDE     = 0x34u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_C_STRIDE     = 0x38u;
static constexpr uint32_t HOLON_NPU_DESC_OFF_RESERVED_3C  = 0x3Cu;
static constexpr uint32_t HOLON_NPU_DESC_OFF_RESERVED_TAIL = 0x40u;

typedef struct holon_npu_gemm_desc {
    uint16_t size_bytes;
    uint8_t version;
    uint8_t opcode;
    uint32_t flags;
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t reserved_14;
    uint64_t a_addr;
    uint64_t b_addr;
    uint64_t c_addr;
    uint32_t a_row_stride_bytes;
    uint32_t b_row_stride_bytes;
    uint32_t c_row_stride_bytes;
    uint32_t reserved_3c;
    uint64_t reserved_40;
    uint64_t reserved_48;
    uint64_t reserved_50;
    uint64_t reserved_58;
    uint64_t reserved_60;
    uint64_t reserved_68;
    uint64_t reserved_70;
    uint64_t reserved_78;
} holon_npu_gemm_desc_t;

typedef struct holon_npu_gemm_config {
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t flags;
    uint64_t a_addr;
    uint64_t b_addr;
    uint64_t c_addr;
    uint32_t a_row_stride_bytes;
    uint32_t b_row_stride_bytes;
    uint32_t c_row_stride_bytes;
} holon_npu_gemm_config_t;

static_assert(sizeof(holon_npu_gemm_desc_t) == HOLON_NPU_DESC_SIZE,
              "holon_npu_gemm_desc_t must be 128 bytes");
static_assert(offsetof(holon_npu_gemm_desc_t, size_bytes) == HOLON_NPU_DESC_OFF_SIZE_BYTES,
              "descriptor size_bytes offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, version) == HOLON_NPU_DESC_OFF_VERSION,
              "descriptor version offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, opcode) == HOLON_NPU_DESC_OFF_OPCODE,
              "descriptor opcode offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, flags) == HOLON_NPU_DESC_OFF_FLAGS,
              "descriptor flags offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, m) == HOLON_NPU_DESC_OFF_M,
              "descriptor m offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, n) == HOLON_NPU_DESC_OFF_N,
              "descriptor n offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, k) == HOLON_NPU_DESC_OFF_K,
              "descriptor k offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_14) == HOLON_NPU_DESC_OFF_RESERVED_14,
              "descriptor reserved_14 offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, a_addr) == HOLON_NPU_DESC_OFF_A_ADDR,
              "descriptor a_addr offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, b_addr) == HOLON_NPU_DESC_OFF_B_ADDR,
              "descriptor b_addr offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, c_addr) == HOLON_NPU_DESC_OFF_C_ADDR,
              "descriptor c_addr offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, a_row_stride_bytes) == HOLON_NPU_DESC_OFF_A_STRIDE,
              "descriptor a_row_stride_bytes offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, b_row_stride_bytes) == HOLON_NPU_DESC_OFF_B_STRIDE,
              "descriptor b_row_stride_bytes offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, c_row_stride_bytes) == HOLON_NPU_DESC_OFF_C_STRIDE,
              "descriptor c_row_stride_bytes offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_3c) == HOLON_NPU_DESC_OFF_RESERVED_3C,
              "descriptor reserved_3c offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_40) == HOLON_NPU_DESC_OFF_RESERVED_TAIL,
              "descriptor reserved_40 offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_48) == 0x48,
              "descriptor reserved_48 offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_50) == 0x50,
              "descriptor reserved_50 offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_58) == 0x58,
              "descriptor reserved_58 offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_60) == 0x60,
              "descriptor reserved_60 offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_68) == 0x68,
              "descriptor reserved_68 offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_70) == 0x70,
              "descriptor reserved_70 offset mismatch");
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_78) == 0x78,
              "descriptor reserved_78 offset mismatch");
