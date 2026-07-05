/* Generated from spec/holon_npu_abi.json by tools/gen_abi.py. Do not edit. */
#pragma once

#include <stdint.h>

static constexpr uint32_t HOLON_NPU_DEVICE_ID_RESET           = 0x4E505501u;
static constexpr uint32_t HOLON_NPU_ABI_VERSION_RESET         = 0x00020000u;
static constexpr uint32_t HOLON_NPU_CAP0_RESET                = 0x0000003Fu;
static constexpr uint32_t HOLON_NPU_CAP1_RESET                = 0x08201010u;

static constexpr uint8_t HOLON_NPU_ABI_MAJOR                 = 0x02u;
static constexpr uint8_t HOLON_NPU_ABI_MINOR                 = 0x00u;
static constexpr uint16_t HOLON_NPU_DESC_SIZE                 = 0x0080u;
static constexpr uint32_t HOLON_NPU_DESC_ALIGN                = 0x00000010u;
static constexpr uint32_t HOLON_NPU_TENSOR_ALIGN              = 0x00000010u;
static constexpr uint16_t HOLON_NPU_ARRAY_K                   = 0x0010u;
static constexpr uint16_t HOLON_NPU_ARRAY_N                   = 0x0010u;
static constexpr uint16_t HOLON_NPU_INPUT_BITS                = 0x0008u;
static constexpr uint16_t HOLON_NPU_ACC_BITS                  = 0x0020u;

static constexpr uint32_t HOLON_NPU_REG_DEVICE_ID       = 0x00u;
static constexpr uint32_t HOLON_NPU_REG_ABI_VERSION     = 0x04u;
static constexpr uint32_t HOLON_NPU_REG_CAP0            = 0x08u;
static constexpr uint32_t HOLON_NPU_REG_CAP1            = 0x0Cu;
static constexpr uint32_t HOLON_NPU_REG_CONTROL         = 0x10u;
static constexpr uint32_t HOLON_NPU_REG_STATUS          = 0x14u;
static constexpr uint32_t HOLON_NPU_REG_ERROR_CODE      = 0x18u;
static constexpr uint32_t HOLON_NPU_REG_IRQ_ENABLE      = 0x1Cu;
static constexpr uint32_t HOLON_NPU_REG_IRQ_STATUS      = 0x20u;
static constexpr uint32_t HOLON_NPU_REG_DOORBELL        = 0x24u;
static constexpr uint32_t HOLON_NPU_REG_DESC_ADDR_LO    = 0x28u;
static constexpr uint32_t HOLON_NPU_REG_DESC_ADDR_HI    = 0x2Cu;
static constexpr uint32_t HOLON_NPU_REG_CLEAR           = 0x30u;
static constexpr uint32_t HOLON_NPU_REG_PERF_CYCLES_LO  = 0x40u;
static constexpr uint32_t HOLON_NPU_REG_PERF_CYCLES_HI  = 0x44u;
static constexpr uint32_t HOLON_NPU_REG_PERF_BUSY_LO    = 0x48u;
static constexpr uint32_t HOLON_NPU_REG_PERF_BUSY_HI    = 0x4Cu;
static constexpr uint32_t HOLON_NPU_REG_PERF_DESC_COUNT = 0x50u;
static constexpr uint32_t HOLON_NPU_REG_PERF_ERROR_COUNT = 0x54u;

static constexpr uint32_t HOLON_NPU_CONTROL_SOFT_RESET = 0x00000001u;
static constexpr uint32_t HOLON_NPU_STATUS_IDLE       = 0x00000001u;
static constexpr uint32_t HOLON_NPU_STATUS_BUSY       = 0x00000002u;
static constexpr uint32_t HOLON_NPU_STATUS_DONE       = 0x00000004u;
static constexpr uint32_t HOLON_NPU_STATUS_ERROR      = 0x00000008u;
static constexpr uint32_t HOLON_NPU_STATUS_IRQ_PENDING = 0x00000010u;
static constexpr uint32_t HOLON_NPU_IRQ_DONE          = 0x00000001u;
static constexpr uint32_t HOLON_NPU_IRQ_ERROR         = 0x00000002u;
static constexpr uint32_t HOLON_NPU_IRQ_VALID_MASK    = 0x00000003u;
static constexpr uint32_t HOLON_NPU_DOORBELL_START    = 0x00000001u;
static constexpr uint32_t HOLON_NPU_CLEAR_DONE        = 0x00000001u;
static constexpr uint32_t HOLON_NPU_CLEAR_ERROR       = 0x00000002u;
static constexpr uint32_t HOLON_NPU_CLEAR_PERF        = 0x00000004u;
static constexpr uint32_t HOLON_NPU_CLEAR_VALID_MASK  = 0x00000007u;

static constexpr uint32_t HOLON_NPU_ERR_NONE                 = 0x00u;
static constexpr uint32_t HOLON_NPU_ERR_INVALID_DESC_VERSION = 0x01u;
static constexpr uint32_t HOLON_NPU_ERR_INVALID_OPCODE       = 0x02u;
static constexpr uint32_t HOLON_NPU_ERR_INVALID_DESC_SIZE    = 0x03u;
static constexpr uint32_t HOLON_NPU_ERR_INVALID_FLAGS        = 0x04u;
static constexpr uint32_t HOLON_NPU_ERR_UNSUPPORTED_ALIGNMENT = 0x05u;
static constexpr uint32_t HOLON_NPU_ERR_AXI_READ             = 0x06u;
static constexpr uint32_t HOLON_NPU_ERR_AXI_WRITE            = 0x07u;
static constexpr uint32_t HOLON_NPU_ERR_INTERNAL_PROTOCOL    = 0x08u;
static constexpr uint32_t HOLON_NPU_ERR_DOORBELL_BUSY        = 0x09u;
static constexpr uint32_t HOLON_NPU_ERR_RESERVED_NONZERO     = 0x0Au;
static constexpr uint32_t HOLON_NPU_ERR_DIMENSION_ZERO       = 0x0Bu;
static constexpr uint32_t HOLON_NPU_ERR_DIMENSION_UNSUPPORTED = 0x0Cu;
