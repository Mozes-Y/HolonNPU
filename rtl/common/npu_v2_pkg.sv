// Generated from spec/holon_npu_v2_abi.json by tools/gen_v2_abi.py. Do not edit.
/* verilator lint_off UNUSEDPARAM */
package npu_v2_pkg;
    localparam int unsigned NPU_V2_ABI_MAJOR                  = 3;
    localparam int unsigned NPU_V2_ABI_MINOR                  = 0;
    localparam logic [31:0] NPU_V2_ABI_VERSION_RESET          = 32'h00030000;

    localparam int unsigned NPU_V2_PROGRAM_DESC_SIZE              = 128;
    localparam int unsigned NPU_V2_PROGRAM_DESC_ALIGN             = 16;
    localparam int unsigned NPU_V2_PROGRAM_IMAGE_ALIGN            = 4;
    localparam int unsigned NPU_V2_PROGRAM_ARGUMENT_ALIGN         = 16;
    localparam int unsigned NPU_V2_PROGRAM_COMPLETION_ALIGN       = 16;
    localparam int unsigned NPU_V2_COMPLETION_RECORD_SIZE         = 32;
    localparam int unsigned NPU_V2_PROGRAM_FORMAT_HOLON_V2        = 1;
    localparam int unsigned NPU_V2_PROGRAM_MEM_MAX_BYTES          = 65536;
    localparam int unsigned NPU_V2_LOCAL_MEM_MAX_BYTES            = 262144;
    localparam int unsigned NPU_V2_PROGRAM_STACK_MAX_BYTES        = 16384;

    // V2 register offsets.
    localparam logic [11:0] NPU_V2_REG_DEVICE_ID           = 12'h000;
    localparam logic [11:0] NPU_V2_REG_ABI_VERSION         = 12'h004;
    localparam logic [11:0] NPU_V2_REG_ISA_VERSION         = 12'h008;
    localparam logic [11:0] NPU_V2_REG_CAP0_LO             = 12'h00C;
    localparam logic [11:0] NPU_V2_REG_CAP0_HI             = 12'h010;
    localparam logic [11:0] NPU_V2_REG_OP_CLASS_LO         = 12'h014;
    localparam logic [11:0] NPU_V2_REG_OP_CLASS_HI         = 12'h018;
    localparam logic [11:0] NPU_V2_REG_PROGRAM_MEM_BYTES   = 12'h01C;
    localparam logic [11:0] NPU_V2_REG_LOCAL_MEM_BYTES     = 12'h020;
    localparam logic [11:0] NPU_V2_REG_VECTOR_CAP0         = 12'h024;
    localparam logic [11:0] NPU_V2_REG_MATRIX_CAP0         = 12'h028;
    localparam logic [11:0] NPU_V2_REG_CONTROL             = 12'h030;
    localparam logic [11:0] NPU_V2_REG_STATUS              = 12'h034;
    localparam logic [11:0] NPU_V2_REG_FAULT_CODE          = 12'h038;
    localparam logic [11:0] NPU_V2_REG_DEBUG_PC            = 12'h03C;
    localparam logic [11:0] NPU_V2_REG_PROGRAM_DESC_ADDR_LO = 12'h040;
    localparam logic [11:0] NPU_V2_REG_PROGRAM_DESC_ADDR_HI = 12'h044;
    localparam logic [11:0] NPU_V2_REG_DOORBELL            = 12'h048;
    localparam logic [11:0] NPU_V2_REG_IRQ_ENABLE          = 12'h04C;
    localparam logic [11:0] NPU_V2_REG_IRQ_STATUS          = 12'h050;
    localparam logic [11:0] NPU_V2_REG_IRQ_CLEAR           = 12'h054;
    localparam logic [11:0] NPU_V2_REG_PERF_CYCLE_LO       = 12'h060;
    localparam logic [11:0] NPU_V2_REG_PERF_CYCLE_HI       = 12'h064;
    localparam logic [11:0] NPU_V2_REG_PERF_INSTRET_LO     = 12'h068;
    localparam logic [11:0] NPU_V2_REG_PERF_INSTRET_HI     = 12'h06C;

    // V2 register reset values.
    localparam logic [31:0] NPU_V2_RESET_DEVICE_ID           = 32'h4E505502;
    localparam logic [31:0] NPU_V2_RESET_ABI_VERSION         = 32'h00030000;
    localparam logic [31:0] NPU_V2_RESET_ISA_VERSION         = 32'h00010000;
    localparam logic [31:0] NPU_V2_RESET_CAP0_LO             = 32'h0000007F;
    localparam logic [31:0] NPU_V2_RESET_CAP0_HI             = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_OP_CLASS_LO         = 32'h000001FF;
    localparam logic [31:0] NPU_V2_RESET_OP_CLASS_HI         = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_PROGRAM_MEM_BYTES   = 32'h00010000;
    localparam logic [31:0] NPU_V2_RESET_LOCAL_MEM_BYTES     = 32'h00040000;
    localparam logic [31:0] NPU_V2_RESET_VECTOR_CAP0         = 32'h01010010;
    localparam logic [31:0] NPU_V2_RESET_MATRIX_CAP0         = 32'h08201010;
    localparam logic [31:0] NPU_V2_RESET_CONTROL             = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_STATUS              = 32'h00000001;
    localparam logic [31:0] NPU_V2_RESET_FAULT_CODE          = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_DEBUG_PC            = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_PROGRAM_DESC_ADDR_LO = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_PROGRAM_DESC_ADDR_HI = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_DOORBELL            = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_IRQ_ENABLE          = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_IRQ_STATUS          = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_IRQ_CLEAR           = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_PERF_CYCLE_LO       = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_PERF_CYCLE_HI       = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_PERF_INSTRET_LO     = 32'h00000000;
    localparam logic [31:0] NPU_V2_RESET_PERF_INSTRET_HI     = 32'h00000000;

    // V2 lifecycle status bits.
    localparam logic [31:0] NPU_V2_STATUS_IDLE       = 32'h00000001;
    localparam logic [31:0] NPU_V2_STATUS_LOADING    = 32'h00000002;
    localparam logic [31:0] NPU_V2_STATUS_RUNNING    = 32'h00000004;
    localparam logic [31:0] NPU_V2_STATUS_HALTED     = 32'h00000008;
    localparam logic [31:0] NPU_V2_STATUS_DONE       = 32'h00000010;
    localparam logic [31:0] NPU_V2_STATUS_FAULT      = 32'h00000020;
    localparam logic [31:0] NPU_V2_STATUS_IRQ_PENDING = 32'h00000040;

    // V2 program flags.
    localparam logic [31:0] NPU_V2_PROGRAM_FLAG_IRQ_ON_DONE            = 32'h00000001;
    localparam logic [31:0] NPU_V2_PROGRAM_FLAG_IRQ_ON_FAULT           = 32'h00000002;
    localparam logic [31:0] NPU_V2_PROGRAM_FLAG_CLEAR_PERF_ON_START    = 32'h00000004;
    localparam logic [31:0] NPU_V2_PROGRAM_FLAG_DEBUG_SNAPSHOT_ON_FAULT = 32'h00000008;
    localparam logic [31:0] NPU_V2_PROGRAM_FLAG_VALID_MASK             = 32'h0000000F;

    // V2 control bits.
    localparam logic [31:0] NPU_V2_CONTROL_SOFT_RESET    = 32'h00000001;
    localparam logic [31:0] NPU_V2_CONTROL_CLEAR_TERMINAL = 32'h00000002;
    localparam logic [31:0] NPU_V2_CONTROL_HALT          = 32'h00000004;
    localparam logic [31:0] NPU_V2_CONTROL_RESUME        = 32'h00000008;
    localparam logic [31:0] NPU_V2_CONTROL_DEBUG_STEP    = 32'h00000010;
    localparam logic [31:0] NPU_V2_CONTROL_VALID_MASK    = 32'h0000001F;

    // V2 doorbell bits.
    localparam logic [31:0] NPU_V2_DOORBELL_START     = 32'h00000001;
    localparam logic [31:0] NPU_V2_DOORBELL_VALID_MASK = 32'h00000001;

    // V2 IRQ bits.
    localparam logic [31:0] NPU_V2_IRQ_DONE      = 32'h00000001;
    localparam logic [31:0] NPU_V2_IRQ_FAULT     = 32'h00000002;
    localparam logic [31:0] NPU_V2_IRQ_HALTED    = 32'h00000004;
    localparam logic [31:0] NPU_V2_IRQ_DEBUG_STEP = 32'h00000008;
    localparam logic [31:0] NPU_V2_IRQ_VALID_MASK = 32'h0000000F;

    // V2 operation classes.
    localparam logic [63:0] NPU_V2_OP_CLASS_FRONTEND_CONTROL = 64'h0000000000000001;
    localparam logic [63:0] NPU_V2_OP_CLASS_PREDICATE       = 64'h0000000000000002;
    localparam logic [63:0] NPU_V2_OP_CLASS_VECTOR          = 64'h0000000000000004;
    localparam logic [63:0] NPU_V2_OP_CLASS_QUANTIZATION    = 64'h0000000000000008;
    localparam logic [63:0] NPU_V2_OP_CLASS_MATRIX          = 64'h0000000000000010;
    localparam logic [63:0] NPU_V2_OP_CLASS_DMA             = 64'h0000000000000020;
    localparam logic [63:0] NPU_V2_OP_CLASS_CSR_DEBUG       = 64'h0000000000000040;
    localparam logic [63:0] NPU_V2_OP_CLASS_SYNC            = 64'h0000000000000080;
    localparam logic [63:0] NPU_V2_OP_CLASS_SYSTEM          = 64'h0000000000000100;

    // V2 capabilities.
    localparam logic [63:0] NPU_V2_CAP_PROGRAM_DESCRIPTOR      = 64'h0000000000000001;
    localparam logic [63:0] NPU_V2_CAP_LOCAL_PROGRAM_MEMORY    = 64'h0000000000000002;
    localparam logic [63:0] NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY = 64'h0000000000000004;
    localparam logic [63:0] NPU_V2_CAP_IN_ORDER_DMA_QUEUE      = 64'h0000000000000008;
    localparam logic [63:0] NPU_V2_CAP_MATRIX_MICRO_OP         = 64'h0000000000000010;
    localparam logic [63:0] NPU_V2_CAP_INTEGER_VECTOR_BASE     = 64'h0000000000000020;
    localparam logic [63:0] NPU_V2_CAP_QUANT_VECTOR            = 64'h0000000000000040;

    // V2 fault codes.
    localparam logic [31:0] NPU_V2_FAULT_NONE                       = 32'h00000000;
    localparam logic [31:0] NPU_V2_FAULT_INVALID_PROGRAM_DESCRIPTOR = 32'h00000001;
    localparam logic [31:0] NPU_V2_FAULT_UNSUPPORTED_ABI_OR_ISA     = 32'h00000002;
    localparam logic [31:0] NPU_V2_FAULT_UNSUPPORTED_PROGRAM_FORMAT = 32'h00000003;
    localparam logic [31:0] NPU_V2_FAULT_UNSUPPORTED_CAPABILITY     = 32'h00000004;
    localparam logic [31:0] NPU_V2_FAULT_UNSUPPORTED_OPERATION_CLASS = 32'h00000005;
    localparam logic [31:0] NPU_V2_FAULT_ALIGNMENT                  = 32'h00000006;
    localparam logic [31:0] NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS        = 32'h00000007;
    localparam logic [31:0] NPU_V2_FAULT_ILLEGAL_INSTRUCTION        = 32'h00000008;
    localparam logic [31:0] NPU_V2_FAULT_VECTOR_CONFIG              = 32'h00000009;
    localparam logic [31:0] NPU_V2_FAULT_MATRIX_ISSUE               = 32'h0000000A;
    localparam logic [31:0] NPU_V2_FAULT_DMA_REQUEST                = 32'h0000000B;
    localparam logic [31:0] NPU_V2_FAULT_AXI_READ                   = 32'h0000000C;
    localparam logic [31:0] NPU_V2_FAULT_AXI_WRITE                  = 32'h0000000D;
    localparam logic [31:0] NPU_V2_FAULT_EXPLICIT_PROGRAM_FAULT     = 32'h0000000E;

    // V2 completion status values.
    localparam logic [31:0] NPU_V2_COMPLETION_STATUS_DONE = 32'h00000001;
    localparam logic [31:0] NPU_V2_COMPLETION_STATUS_FAULT = 32'h00000002;

    // V2 program descriptor offsets.
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_SIZE_BYTES         = 0;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_VERSION            = 2;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_PROGRAM_FORMAT     = 3;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_HOLON_ISA_MAJOR    = 4;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_HOLON_ISA_MINOR    = 6;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_REQUIRED_CAPS      = 8;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_REQUIRED_OP_CLASSES = 16;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_CODE_ADDR          = 24;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_CODE_SIZE          = 32;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_ENTRY_PC           = 36;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_ARG_ADDR           = 40;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_ARG_SIZE           = 48;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_LOCAL_MEM_BYTES    = 52;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_PROGRAM_MEM_BYTES  = 56;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_STACK_BYTES        = 60;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_COMPLETION_ADDR    = 64;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_FLAGS              = 72;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_RESERVED_4C        = 76;
    localparam int unsigned NPU_V2_PROGRAM_DESC_OFF_RESERVED_TAIL      = 80;

    // V2 completion record offsets.
    localparam int unsigned NPU_V2_COMPLETION_OFF_ABI_VERSION = 0;
    localparam int unsigned NPU_V2_COMPLETION_OFF_STATUS     = 4;
    localparam int unsigned NPU_V2_COMPLETION_OFF_FAULT_CODE = 8;
    localparam int unsigned NPU_V2_COMPLETION_OFF_DEBUG_PC   = 12;
    localparam int unsigned NPU_V2_COMPLETION_OFF_CYCLE_COUNT = 16;
    localparam int unsigned NPU_V2_COMPLETION_OFF_INSTRET    = 24;

endpackage
/* verilator lint_on UNUSEDPARAM */
