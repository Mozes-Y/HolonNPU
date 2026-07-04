`include "npu_assert.svh"

module npu_command_processor #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic                    soft_reset_i,

    input  logic                    start_i,
    input  logic [ADDR_W-1:0]       desc_addr_i,
    output logic                    busy_o,
    output logic                    done_o,
    output logic                    error_o,
    output logic [31:0]             error_code_o,
    output logic                    irq_on_done_o,
    output logic                    irq_on_error_o,
    output logic                    clear_perf_on_start_o,

    npu_axi4_if.read_master         m_axi,
    npu_vr_if.source                command_o
);

    import npu_pkg::*;

    typedef enum logic [2:0] {
        STATE_IDLE  = 3'd0,
        STATE_FETCH = 3'd1,
        STATE_CHECK = 3'd2,
        STATE_ISSUE = 3'd3,
        STATE_DONE  = 3'd4,
        STATE_ERR   = 3'd5
    } state_e;

    localparam int unsigned DESC_BEATS = 8;

    state_e state_q;
    logic [2:0] desc_beat_count_q;
    logic [127:0] desc_q [DESC_BEATS];
    logic [31:0] error_code_q;
    logic flags_valid_q;
    logic dma_rst_ni;

    logic dma_start_q;
    logic dma_busy;
    logic dma_done;
    logic dma_error;
    logic [31:0] dma_error_code;
    logic dma_out_valid;
    logic dma_out_ready;
    logic [DATA_W:0] dma_out_payload;
    logic [127:0] dma_out_data;
    logic dma_out_last;
    logic [NPU_GEMM_CMD_W-1:0] command_payload;

    logic [15:0] desc_size;
    logic [7:0]  desc_version;
    logic [7:0]  desc_opcode;
    logic [31:0] desc_flags;
    logic [31:0] desc_m;
    logic [31:0] desc_n;
    logic [31:0] desc_k;
    logic [31:0] desc_reserved_14;
    logic [63:0] desc_a_addr;
    logic [63:0] desc_b_addr;
    logic [63:0] desc_c_addr;
    logic [31:0] desc_a_stride;
    logic [31:0] desc_b_stride;
    logic [31:0] desc_c_stride;
    logic [31:0] desc_reserved_3c;
    logic        reserved_tail_nonzero;
    logic [31:0] c_stride_min;
    logic [31:0] validation_error_code;
    logic        validation_error;
    logic        dma_last_mismatch;

    assign busy_o = (state_q == STATE_FETCH) || (state_q == STATE_CHECK) ||
                    (state_q == STATE_ISSUE) || dma_busy;
    assign done_o = (state_q == STATE_DONE) && !start_i;
    assign error_o = (state_q == STATE_ERR) && !start_i;
    assign error_code_o = error_code_q;

    assign command_o.valid = (state_q == STATE_ISSUE);
    assign command_o.data = command_payload;
    assign irq_on_done_o = flags_valid_q && desc_flags[0];
    assign irq_on_error_o = flags_valid_q ? desc_flags[1] : 1'b1;
    assign clear_perf_on_start_o = flags_valid_q && desc_flags[2];
    assign dma_rst_ni = rst_ni && !soft_reset_i;

    assign dma_out_ready = (state_q == STATE_FETCH);
    assign dma_out_data = dma_out_payload[DATA_W-1:0];
    assign dma_out_last = dma_out_payload[DATA_W];
    assign dma_last_mismatch = dma_out_valid && dma_out_ready &&
                               (dma_out_last != (desc_beat_count_q == 3'd7));

    always_comb begin
        command_payload = '0;
        command_payload[NPU_GEMM_CMD_IRQ_ON_DONE_BIT] = irq_on_done_o;
        command_payload[NPU_GEMM_CMD_IRQ_ON_ERROR_BIT] = irq_on_error_o;
        command_payload[NPU_GEMM_CMD_CLEAR_PERF_BIT] = clear_perf_on_start_o;
        command_payload[NPU_GEMM_CMD_M_LSB +: 32] = desc_m;
        command_payload[NPU_GEMM_CMD_N_LSB +: 32] = desc_n;
        command_payload[NPU_GEMM_CMD_K_LSB +: 32] = desc_k;
        command_payload[NPU_GEMM_CMD_A_ADDR_LSB +: 64] = desc_a_addr;
        command_payload[NPU_GEMM_CMD_B_ADDR_LSB +: 64] = desc_b_addr;
        command_payload[NPU_GEMM_CMD_C_ADDR_LSB +: 64] = desc_c_addr;
        command_payload[NPU_GEMM_CMD_A_STRIDE_LSB +: 32] = desc_a_stride;
        command_payload[NPU_GEMM_CMD_B_STRIDE_LSB +: 32] = desc_b_stride;
        command_payload[NPU_GEMM_CMD_C_STRIDE_LSB +: 32] = desc_c_stride;
    end

    npu_vr_if #(
        .DATA_W(DATA_W + 1)
    ) dma_out_if (
        .clk_i(clk_i),
        .rst_ni(dma_rst_ni)
    );

    assign dma_out_valid = dma_out_if.valid;
    assign dma_out_payload = dma_out_if.data;
    assign dma_out_if.ready = dma_out_ready;

    npu_axi4_read_dma_core #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) u_desc_read_dma (
        .clk_i(clk_i),
        .rst_ni(dma_rst_ni),
        .start_i(dma_start_q),
        .addr_i(desc_addr_i),
        .bytes_i(32'(NPU_DESC_SIZE_BYTES)),
        .busy_o(dma_busy),
        .done_o(dma_done),
        .error_o(dma_error),
        .error_code_o(dma_error_code),
        .m_axi(m_axi),
        .out_o(dma_out_if)
    );

    function automatic logic [7:0] desc_byte(input int unsigned byte_offset);
        logic [2:0] beat;
        logic [3:0] byte_in_beat;
        begin
            beat = 3'(byte_offset >> 4);
            byte_in_beat = 4'(byte_offset & 15);
            desc_byte = desc_q[beat][(byte_in_beat * 8) +: 8];
        end
    endfunction

    function automatic logic [15:0] desc_u16(input int unsigned byte_offset);
        desc_u16 = {desc_byte(byte_offset + 1), desc_byte(byte_offset)};
    endfunction

    function automatic logic [31:0] desc_u32(input int unsigned byte_offset);
        desc_u32 = {
            desc_byte(byte_offset + 3),
            desc_byte(byte_offset + 2),
            desc_byte(byte_offset + 1),
            desc_byte(byte_offset)
        };
    endfunction

    function automatic logic [63:0] desc_u64(input int unsigned byte_offset);
        desc_u64 = {
            desc_byte(byte_offset + 7),
            desc_byte(byte_offset + 6),
            desc_byte(byte_offset + 5),
            desc_byte(byte_offset + 4),
            desc_byte(byte_offset + 3),
            desc_byte(byte_offset + 2),
            desc_byte(byte_offset + 1),
            desc_byte(byte_offset)
        };
    endfunction

    always_comb begin
        desc_size = desc_u16(NPU_DESC_OFF_SIZE_BYTES);
        desc_version = desc_byte(NPU_DESC_OFF_VERSION);
        desc_opcode = desc_byte(NPU_DESC_OFF_OPCODE);
        desc_flags = desc_u32(NPU_DESC_OFF_FLAGS);
        desc_m = desc_u32(NPU_DESC_OFF_M);
        desc_n = desc_u32(NPU_DESC_OFF_N);
        desc_k = desc_u32(NPU_DESC_OFF_K);
        desc_reserved_14 = desc_u32(NPU_DESC_OFF_RESERVED_14);
        desc_a_addr = desc_u64(NPU_DESC_OFF_A_ADDR);
        desc_b_addr = desc_u64(NPU_DESC_OFF_B_ADDR);
        desc_c_addr = desc_u64(NPU_DESC_OFF_C_ADDR);
        desc_a_stride = desc_u32(NPU_DESC_OFF_A_STRIDE);
        desc_b_stride = desc_u32(NPU_DESC_OFF_B_STRIDE);
        desc_c_stride = desc_u32(NPU_DESC_OFF_C_STRIDE);
        desc_reserved_3c = desc_u32(NPU_DESC_OFF_RESERVED_3C);
        c_stride_min = desc_n << 2;

        reserved_tail_nonzero = 1'b0;
        for (int unsigned offset = NPU_DESC_OFF_RESERVED_TAIL; offset < NPU_DESC_SIZE_BYTES; offset++) begin
            reserved_tail_nonzero |= (desc_byte(offset) != 8'h00);
        end

        validation_error = 1'b0;
        validation_error_code = 32'(NPU_ERR_NONE);

        if (desc_size != 16'(NPU_DESC_SIZE_BYTES)) begin
            validation_error = 1'b1;
            validation_error_code = 32'(NPU_ERR_INVALID_DESC_SIZE);
        end else if (desc_version != 8'(NPU_ABI_MAJOR)) begin
            validation_error = 1'b1;
            validation_error_code = 32'(NPU_ERR_INVALID_DESC_VERSION);
        end else if (desc_opcode != 8'(NPU_OPCODE_GEMM_I8I8I32)) begin
            validation_error = 1'b1;
            validation_error_code = 32'(NPU_ERR_INVALID_OPCODE);
        end else if ((desc_flags & ~NPU_DESC_FLAG_VALID_MASK) != 32'h0000_0000) begin
            validation_error = 1'b1;
            validation_error_code = 32'(NPU_ERR_INVALID_FLAGS);
        end else if ((desc_m == 32'd0) || (desc_n == 32'd0) || (desc_k == 32'd0)) begin
            validation_error = 1'b1;
            validation_error_code = 32'(NPU_ERR_DIMENSION_ZERO);
        end else if ((desc_m > 32'd65535) || (desc_n > 32'd65535) || (desc_k > 32'd65535)) begin
            validation_error = 1'b1;
            validation_error_code = 32'(NPU_ERR_DIMENSION_UNSUPPORTED);
        end else if ((desc_a_addr[3:0] != 4'h0) ||
                     (desc_b_addr[3:0] != 4'h0) ||
                     (desc_c_addr[3:0] != 4'h0) ||
                     (desc_a_stride[3:0] != 4'h0) ||
                     (desc_b_stride[3:0] != 4'h0) ||
                     (desc_c_stride[3:0] != 4'h0) ||
                     (desc_a_stride < desc_k) ||
                     (desc_b_stride < desc_n) ||
                     (desc_c_stride < c_stride_min)) begin
            validation_error = 1'b1;
            validation_error_code = 32'(NPU_ERR_UNSUPPORTED_ALIGNMENT);
        end else if ((desc_reserved_14 != 32'h0000_0000) ||
                     (desc_reserved_3c != 32'h0000_0000) ||
                     reserved_tail_nonzero) begin
            validation_error = 1'b1;
            validation_error_code = 32'(NPU_ERR_RESERVED_NONZERO);
        end
    end

    initial begin
        if (NPU_ABI_MAJOR != 2) $fatal("Unexpected NPU_ABI_MAJOR");
        if (NPU_ABI_MINOR != 0) $fatal("Unexpected NPU_ABI_MINOR");
        if (NPU_DESC_SIZE_BYTES != 128) $fatal("Unexpected NPU_DESC_SIZE_BYTES");
        if (NPU_DESC_ALIGN_BYTES != 16) $fatal("Unexpected NPU_DESC_ALIGN_BYTES");
        if (NPU_TENSOR_ALIGN_BYTES != 16) $fatal("Unexpected NPU_TENSOR_ALIGN_BYTES");
        if (NPU_ARRAY_K != 16) $fatal("Unexpected NPU_ARRAY_K");
        if (NPU_ARRAY_N != 16) $fatal("Unexpected NPU_ARRAY_N");
        if (NPU_INPUT_BITS != 8) $fatal("Unexpected NPU_INPUT_BITS");
        if (NPU_ACC_BITS != 32) $fatal("Unexpected NPU_ACC_BITS");
        if (NPU_DEVICE_ID_RESET != 32'h4E50_5501) $fatal("Unexpected NPU_DEVICE_ID_RESET");
        if (NPU_ABI_VERSION_RESET != 32'h0002_0000) $fatal("Unexpected NPU_ABI_VERSION_RESET");
        if (NPU_CAP0_RESET != 32'h0000_003F) $fatal("Unexpected NPU_CAP0_RESET");
        if (NPU_CAP1_RESET != 32'h0820_1010) $fatal("Unexpected NPU_CAP1_RESET");
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state_q <= STATE_IDLE;
            desc_beat_count_q <= '0;
            error_code_q <= 32'(NPU_ERR_NONE);
            dma_start_q <= 1'b0;
            flags_valid_q <= 1'b0;
            for (int unsigned beat = 0; beat < DESC_BEATS; beat++) begin
                desc_q[beat] <= '0;
            end
        end else begin
            dma_start_q <= 1'b0;

            if (soft_reset_i) begin
                state_q <= STATE_IDLE;
                desc_beat_count_q <= '0;
                error_code_q <= 32'(NPU_ERR_NONE);
                flags_valid_q <= 1'b0;
                for (int unsigned beat = 0; beat < DESC_BEATS; beat++) begin
                    desc_q[beat] <= '0;
                end
            end else unique case (state_q)
                STATE_IDLE: begin
                    error_code_q <= 32'(NPU_ERR_NONE);
                    desc_beat_count_q <= '0;
                    flags_valid_q <= 1'b0;
                    if (start_i) begin
                        dma_start_q <= 1'b1;
                        state_q <= STATE_FETCH;
                    end
                end

                STATE_FETCH: begin
                    if (dma_out_valid && dma_out_ready) begin
                        desc_q[desc_beat_count_q] <= dma_out_data;
                        desc_beat_count_q <= desc_beat_count_q + 3'd1;
                    end

                    if (dma_error) begin
                        error_code_q <= dma_error_code;
                        state_q <= STATE_ERR;
                    end else if (dma_last_mismatch) begin
                        error_code_q <= 32'(NPU_ERR_INTERNAL_PROTOCOL);
                        state_q <= STATE_ERR;
                    end else if (dma_done) begin
                        flags_valid_q <= 1'b1;
                        state_q <= STATE_CHECK;
                    end
                end

                STATE_CHECK: begin
                    if (validation_error) begin
                        error_code_q <= validation_error_code;
                        state_q <= STATE_ERR;
                    end else begin
                        state_q <= STATE_ISSUE;
                    end
                end

                STATE_ISSUE: begin
                    if (command_o.ready) begin
                        state_q <= STATE_DONE;
                    end
                end

                STATE_DONE: begin
                    if (start_i) begin
                        dma_start_q <= 1'b1;
                        desc_beat_count_q <= '0;
                        error_code_q <= 32'(NPU_ERR_NONE);
                        flags_valid_q <= 1'b0;
                        state_q <= STATE_FETCH;
                    end
                end

                STATE_ERR: begin
                    if (start_i) begin
                        dma_start_q <= 1'b1;
                        desc_beat_count_q <= '0;
                        error_code_q <= 32'(NPU_ERR_NONE);
                        flags_valid_q <= 1'b0;
                        state_q <= STATE_FETCH;
                    end
                end

                default: begin
                    error_code_q <= 32'(NPU_ERR_INTERNAL_PROTOCOL);
                    state_q <= STATE_ERR;
                end
            endcase
        end
    end

    `HOLON_NPU_ASSERT(command_terminal_states_exclusive,
        @(posedge clk_i) disable iff (!rst_ni)
            !(done_o && error_o))
    `HOLON_NPU_ASSERT(command_invalid_descriptor_never_issues,
        @(posedge clk_i) disable iff (!rst_ni)
            (state_q == STATE_CHECK) && validation_error |-> !command_o.valid)
    `HOLON_NPU_ASSERT(command_issue_only_after_validation,
        @(posedge clk_i) disable iff (!rst_ni)
            command_o.valid |-> (state_q == STATE_ISSUE) && !validation_error)
    `HOLON_NPU_ASSERT(command_dma_last_mismatch_is_terminal_error,
        @(posedge clk_i) disable iff (!rst_ni)
            dma_last_mismatch |=> (state_q == STATE_ERR))

endmodule
