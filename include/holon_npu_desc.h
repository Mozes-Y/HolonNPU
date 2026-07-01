#ifndef HOLON_NPU_DESC_H
#define HOLON_NPU_DESC_H

#include <stddef.h>
#include <stdint.h>

#include "holon_npu_regs.h"

#define HOLON_NPU_OPCODE_GEMM_I8I8I32 UINT32_C(1)

#define HOLON_NPU_DESC_FLAG_IRQ_ON_DONE        UINT32_C(0x00000001)
#define HOLON_NPU_DESC_FLAG_IRQ_ON_ERROR       UINT32_C(0x00000002)
#define HOLON_NPU_DESC_FLAG_CLEAR_PERF_ON_START UINT32_C(0x00000004)
#define HOLON_NPU_DESC_FLAG_VALID_MASK         UINT32_C(0x00000007)

#define HOLON_NPU_DESC_OFF_SIZE_BYTES          UINT32_C(0x00)
#define HOLON_NPU_DESC_OFF_VERSION             UINT32_C(0x02)
#define HOLON_NPU_DESC_OFF_OPCODE              UINT32_C(0x03)
#define HOLON_NPU_DESC_OFF_FLAGS               UINT32_C(0x04)
#define HOLON_NPU_DESC_OFF_M                   UINT32_C(0x08)
#define HOLON_NPU_DESC_OFF_N                   UINT32_C(0x0C)
#define HOLON_NPU_DESC_OFF_K                   UINT32_C(0x10)
#define HOLON_NPU_DESC_OFF_RESERVED_14         UINT32_C(0x14)
#define HOLON_NPU_DESC_OFF_A_ADDR              UINT32_C(0x18)
#define HOLON_NPU_DESC_OFF_B_ADDR              UINT32_C(0x20)
#define HOLON_NPU_DESC_OFF_C_ADDR              UINT32_C(0x28)
#define HOLON_NPU_DESC_OFF_A_STRIDE            UINT32_C(0x30)
#define HOLON_NPU_DESC_OFF_B_STRIDE            UINT32_C(0x34)
#define HOLON_NPU_DESC_OFF_C_STRIDE            UINT32_C(0x38)
#define HOLON_NPU_DESC_OFF_RESERVED_3C         UINT32_C(0x3C)
#define HOLON_NPU_DESC_OFF_RESERVED_TAIL       UINT32_C(0x40)

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

#if defined(__cplusplus)
#define HOLON_NPU_STATIC_ASSERT static_assert
#else
#define HOLON_NPU_STATIC_ASSERT _Static_assert
#endif

HOLON_NPU_STATIC_ASSERT(sizeof(holon_npu_gemm_desc_t) == HOLON_NPU_DESC_SIZE,
                     "holon_npu_gemm_desc_t must be 128 bytes");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, size_bytes) == HOLON_NPU_DESC_OFF_SIZE_BYTES,
                     "descriptor size offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, version) == HOLON_NPU_DESC_OFF_VERSION,
                     "descriptor version offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, opcode) == HOLON_NPU_DESC_OFF_OPCODE,
                     "descriptor opcode offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, flags) == HOLON_NPU_DESC_OFF_FLAGS,
                     "descriptor flags offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, m) == HOLON_NPU_DESC_OFF_M,
                     "descriptor m offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, n) == HOLON_NPU_DESC_OFF_N,
                     "descriptor n offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, k) == HOLON_NPU_DESC_OFF_K,
                     "descriptor k offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, reserved_14) == HOLON_NPU_DESC_OFF_RESERVED_14,
                     "descriptor reserved_14 offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, a_addr) == HOLON_NPU_DESC_OFF_A_ADDR,
                     "descriptor a_addr offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, b_addr) == HOLON_NPU_DESC_OFF_B_ADDR,
                     "descriptor b_addr offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, c_addr) == HOLON_NPU_DESC_OFF_C_ADDR,
                     "descriptor c_addr offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, a_row_stride_bytes) == HOLON_NPU_DESC_OFF_A_STRIDE,
                     "descriptor a stride offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, b_row_stride_bytes) == HOLON_NPU_DESC_OFF_B_STRIDE,
                     "descriptor b stride offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, c_row_stride_bytes) == HOLON_NPU_DESC_OFF_C_STRIDE,
                     "descriptor c stride offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, reserved_3c) == HOLON_NPU_DESC_OFF_RESERVED_3C,
                     "descriptor reserved_3c offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, reserved_40) == HOLON_NPU_DESC_OFF_RESERVED_TAIL,
                     "descriptor reserved_40 offset mismatch");
HOLON_NPU_STATIC_ASSERT(offsetof(holon_npu_gemm_desc_t, reserved_78) == 0x78,
                     "descriptor reserved_78 offset mismatch");

#undef HOLON_NPU_STATIC_ASSERT

#endif
