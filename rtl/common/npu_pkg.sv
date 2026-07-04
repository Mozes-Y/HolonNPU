// Generated from spec/holon_npu_abi.json by tools/gen_abi.py. Do not edit.
/* verilator lint_off UNUSEDPARAM */
package npu_pkg;
    localparam int unsigned NPU_ABI_MAJOR = 2;
    localparam int unsigned NPU_ABI_MINOR = 0;

    localparam int unsigned NPU_DESC_SIZE_BYTES = 128;
    localparam int unsigned NPU_DESC_ALIGN_BYTES = 16;
    localparam int unsigned NPU_TENSOR_ALIGN_BYTES = 16;

    localparam int unsigned NPU_ARRAY_K = 16;
    localparam int unsigned NPU_ARRAY_N = 16;
    localparam int unsigned NPU_INPUT_BITS = 8;
    localparam int unsigned NPU_ACC_BITS = 32;

    localparam int unsigned NPU_OPCODE_GEMM_I8I8I32 = 1;

    localparam logic [31:0] NPU_DEVICE_ID_RESET = 32'h4E50_5501;
    localparam logic [31:0] NPU_ABI_VERSION_RESET = 32'h0002_0000;
    localparam logic [31:0] NPU_CAP0_RESET = 32'h0000_003F;
    localparam logic [31:0] NPU_CAP1_RESET = 32'h0820_1010;

    localparam logic [11:0] NPU_REG_DEVICE_ID        = 12'h000;
    localparam logic [11:0] NPU_REG_ABI_VERSION      = 12'h004;
    localparam logic [11:0] NPU_REG_CAP0             = 12'h008;
    localparam logic [11:0] NPU_REG_CAP1             = 12'h00C;
    localparam logic [11:0] NPU_REG_CONTROL          = 12'h010;
    localparam logic [11:0] NPU_REG_STATUS           = 12'h014;
    localparam logic [11:0] NPU_REG_ERROR_CODE       = 12'h018;
    localparam logic [11:0] NPU_REG_IRQ_ENABLE       = 12'h01C;
    localparam logic [11:0] NPU_REG_IRQ_STATUS       = 12'h020;
    localparam logic [11:0] NPU_REG_DOORBELL         = 12'h024;
    localparam logic [11:0] NPU_REG_DESC_ADDR_LO     = 12'h028;
    localparam logic [11:0] NPU_REG_DESC_ADDR_HI     = 12'h02C;
    localparam logic [11:0] NPU_REG_CLEAR            = 12'h030;
    localparam logic [11:0] NPU_REG_RESERVED_034     = 12'h034;
    localparam logic [11:0] NPU_REG_RESERVED_038     = 12'h038;
    localparam logic [11:0] NPU_REG_RESERVED_03C     = 12'h03C;
    localparam logic [11:0] NPU_REG_PERF_CYCLES_LO   = 12'h040;
    localparam logic [11:0] NPU_REG_PERF_CYCLES_HI   = 12'h044;
    localparam logic [11:0] NPU_REG_PERF_BUSY_LO     = 12'h048;
    localparam logic [11:0] NPU_REG_PERF_BUSY_HI     = 12'h04C;
    localparam logic [11:0] NPU_REG_PERF_DESC_COUNT  = 12'h050;
    localparam logic [11:0] NPU_REG_PERF_ERROR_COUNT = 12'h054;

    localparam int unsigned NPU_DESC_OFF_SIZE_BYTES = 0;
    localparam int unsigned NPU_DESC_OFF_VERSION = 2;
    localparam int unsigned NPU_DESC_OFF_OPCODE = 3;
    localparam int unsigned NPU_DESC_OFF_FLAGS = 4;
    localparam int unsigned NPU_DESC_OFF_M = 8;
    localparam int unsigned NPU_DESC_OFF_N = 12;
    localparam int unsigned NPU_DESC_OFF_K = 16;
    localparam int unsigned NPU_DESC_OFF_RESERVED_14 = 20;
    localparam int unsigned NPU_DESC_OFF_A_ADDR = 24;
    localparam int unsigned NPU_DESC_OFF_B_ADDR = 32;
    localparam int unsigned NPU_DESC_OFF_C_ADDR = 40;
    localparam int unsigned NPU_DESC_OFF_A_STRIDE = 48;
    localparam int unsigned NPU_DESC_OFF_B_STRIDE = 52;
    localparam int unsigned NPU_DESC_OFF_C_STRIDE = 56;
    localparam int unsigned NPU_DESC_OFF_RESERVED_3C = 60;
    localparam int unsigned NPU_DESC_OFF_RESERVED_TAIL = 64;
    localparam logic [31:0] NPU_DESC_FLAG_IRQ_ON_DONE = 32'h0000_0001;
    localparam logic [31:0] NPU_DESC_FLAG_IRQ_ON_ERROR = 32'h0000_0002;
    localparam logic [31:0] NPU_DESC_FLAG_CLEAR_PERF_ON_START = 32'h0000_0004;
    localparam logic [31:0] NPU_DESC_FLAG_VALID_MASK = 32'h0000_0007;

    localparam int unsigned NPU_GEMM_CMD_IRQ_ON_DONE_BIT = 0;
    localparam int unsigned NPU_GEMM_CMD_IRQ_ON_ERROR_BIT = 1;
    localparam int unsigned NPU_GEMM_CMD_CLEAR_PERF_BIT = 2;
    localparam int unsigned NPU_GEMM_CMD_M_LSB = 3;
    localparam int unsigned NPU_GEMM_CMD_N_LSB = 35;
    localparam int unsigned NPU_GEMM_CMD_K_LSB = 67;
    localparam int unsigned NPU_GEMM_CMD_A_ADDR_LSB = 99;
    localparam int unsigned NPU_GEMM_CMD_B_ADDR_LSB = 163;
    localparam int unsigned NPU_GEMM_CMD_C_ADDR_LSB = 227;
    localparam int unsigned NPU_GEMM_CMD_A_STRIDE_LSB = 291;
    localparam int unsigned NPU_GEMM_CMD_B_STRIDE_LSB = 323;
    localparam int unsigned NPU_GEMM_CMD_C_STRIDE_LSB = 355;
    localparam int unsigned NPU_GEMM_CMD_W = 387;

    typedef enum logic [3:0] {
        NPU_ERR_NONE                  = 4'd0,
        NPU_ERR_INVALID_DESC_VERSION  = 4'd1,
        NPU_ERR_INVALID_OPCODE        = 4'd2,
        NPU_ERR_INVALID_DESC_SIZE     = 4'd3,
        NPU_ERR_INVALID_FLAGS         = 4'd4,
        NPU_ERR_UNSUPPORTED_ALIGNMENT = 4'd5,
        NPU_ERR_AXI_READ              = 4'd6,
        NPU_ERR_AXI_WRITE             = 4'd7,
        NPU_ERR_INTERNAL_PROTOCOL     = 4'd8,
        NPU_ERR_DOORBELL_BUSY         = 4'd9,
        NPU_ERR_RESERVED_NONZERO      = 4'd10,
        NPU_ERR_DIMENSION_ZERO        = 4'd11,
        NPU_ERR_DIMENSION_UNSUPPORTED = 4'd12
    } npu_error_e;

endpackage
/* verilator lint_on UNUSEDPARAM */
