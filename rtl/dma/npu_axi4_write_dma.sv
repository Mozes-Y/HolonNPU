module npu_axi4_write_dma #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128,
    parameter int unsigned LEN_W = 32,
    localparam int unsigned BEAT_BYTES = DATA_W / 8,
    localparam int unsigned BEAT_SHIFT = $clog2(BEAT_BYTES),
    localparam int unsigned STRB_W = DATA_W / 8
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,

    input  logic                    start_i,
    input  logic [ADDR_W-1:0]       addr_i,
    input  logic [LEN_W-1:0]        bytes_i,
    output logic                    busy_o,
    output logic                    done_o,
    output logic                    error_o,
    output logic [31:0]             error_code_o,

    output logic [ADDR_W-1:0]       m_axi_awaddr_o,
    output logic [7:0]              m_axi_awlen_o,
    output logic [2:0]              m_axi_awsize_o,
    output logic [1:0]              m_axi_awburst_o,
    output logic                    m_axi_awvalid_o,
    input  logic                    m_axi_awready_i,
    output logic [DATA_W-1:0]       m_axi_wdata_o,
    output logic [STRB_W-1:0]       m_axi_wstrb_o,
    output logic                    m_axi_wlast_o,
    output logic                    m_axi_wvalid_o,
    input  logic                    m_axi_wready_i,
    input  logic [1:0]              m_axi_bresp_i,
    input  logic                    m_axi_bvalid_i,
    output logic                    m_axi_bready_o,

    input  logic                    in_valid_i,
    output logic                    in_ready_o,
    input  logic [DATA_W-1:0]       in_data_i
);

    import npu_pkg::*;

    typedef enum logic [2:0] {
        STATE_IDLE = 3'd0,
        STATE_AW   = 3'd1,
        STATE_W    = 3'd2,
        STATE_B    = 3'd3,
        STATE_DONE = 3'd4,
        STATE_ERR  = 3'd5
    } state_e;

    localparam logic [1:0] AXI_RESP_OKAY = 2'b00;
    localparam logic [1:0] AXI_RESP_EXOKAY = 2'b01;
    localparam logic [1:0] AXI_BURST_INCR = 2'b01;
    localparam int unsigned MAX_BURST_BEATS = 16;
    localparam int unsigned BEAT_COUNT_W = LEN_W - BEAT_SHIFT;
    localparam logic [BEAT_COUNT_W-1:0] MAX_BURST_COUNT = BEAT_COUNT_W'(MAX_BURST_BEATS);

    state_e state_q;
    logic [ADDR_W-1:0] addr_q;
    logic [BEAT_COUNT_W-1:0] beats_remaining_q;
    logic [4:0] burst_beats_q;
    logic [4:0] burst_beats_sent_q;
    logic [31:0] error_code_q;

    logic start_aligned;
    logic start_nonzero;
    logic [BEAT_COUNT_W-1:0] start_beats;
    logic [4:0] next_burst_beats;
    logic w_fire;
    logic b_resp_ok;
    logic final_w_beat;

    assign start_aligned = (addr_i[BEAT_SHIFT-1:0] == '0) &&
                           (bytes_i[BEAT_SHIFT-1:0] == '0);
    assign start_nonzero = (bytes_i != '0);
    assign start_beats = bytes_i[LEN_W-1:BEAT_SHIFT];
    assign next_burst_beats = (beats_remaining_q > MAX_BURST_COUNT)
        ? 5'd16
        : 5'(beats_remaining_q);

    assign w_fire = m_axi_wvalid_o && m_axi_wready_i;
    assign b_resp_ok = (m_axi_bresp_i == AXI_RESP_OKAY) ||
                       (m_axi_bresp_i == AXI_RESP_EXOKAY);
    assign final_w_beat = (burst_beats_sent_q == (burst_beats_q - 5'd1));

    assign busy_o = (state_q == STATE_AW) || (state_q == STATE_W) || (state_q == STATE_B);
    assign done_o = (state_q == STATE_DONE) && !start_i;
    assign error_o = (state_q == STATE_ERR) && !start_i;
    assign error_code_o = error_code_q;

    assign m_axi_awaddr_o = addr_q;
    assign m_axi_awlen_o = 8'(next_burst_beats - 5'd1);
    assign m_axi_awsize_o = 3'(BEAT_SHIFT);
    assign m_axi_awburst_o = AXI_BURST_INCR;
    assign m_axi_awvalid_o = (state_q == STATE_AW);
    assign m_axi_wdata_o = in_data_i;
    assign m_axi_wstrb_o = {STRB_W{1'b1}};
    assign m_axi_wlast_o = final_w_beat;
    assign m_axi_wvalid_o = (state_q == STATE_W) && in_valid_i;
    assign m_axi_bready_o = (state_q == STATE_B);
    assign in_ready_o = (state_q == STATE_W) && m_axi_wready_i;

    initial begin
        if (NPU_ABI_MAJOR != 1) $fatal("Unexpected NPU_ABI_MAJOR");
        if (NPU_ABI_MINOR != 0) $fatal("Unexpected NPU_ABI_MINOR");
        if (NPU_DESC_SIZE_BYTES != 128) $fatal("Unexpected NPU_DESC_SIZE_BYTES");
        if (NPU_DESC_ALIGN_BYTES != 16) $fatal("Unexpected NPU_DESC_ALIGN_BYTES");
        if (NPU_TENSOR_ALIGN_BYTES != 16) $fatal("Unexpected NPU_TENSOR_ALIGN_BYTES");
        if (NPU_ARRAY_M != 16) $fatal("Unexpected NPU_ARRAY_M");
        if (NPU_ARRAY_N != 16) $fatal("Unexpected NPU_ARRAY_N");
        if (NPU_INPUT_BITS != 8) $fatal("Unexpected NPU_INPUT_BITS");
        if (NPU_ACC_BITS != 32) $fatal("Unexpected NPU_ACC_BITS");
        if (NPU_DEVICE_ID_RESET != 32'h4E50_5501) $fatal("Unexpected NPU_DEVICE_ID_RESET");
        if (NPU_ABI_VERSION_RESET != 32'h0001_0000) $fatal("Unexpected NPU_ABI_VERSION_RESET");
        if (NPU_CAP0_RESET != 32'h0000_003F) $fatal("Unexpected NPU_CAP0_RESET");
        if (NPU_CAP1_RESET != 32'h0820_1010) $fatal("Unexpected NPU_CAP1_RESET");
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state_q <= STATE_IDLE;
            addr_q <= '0;
            beats_remaining_q <= '0;
            burst_beats_q <= '0;
            burst_beats_sent_q <= '0;
            error_code_q <= 32'(NPU_ERR_NONE);
        end else begin
            unique case (state_q)
                STATE_IDLE: begin
                    error_code_q <= 32'(NPU_ERR_NONE);
                    if (start_i) begin
                        if (!start_aligned || !start_nonzero) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_UNSUPPORTED_ALIGNMENT);
                        end else begin
                            state_q <= STATE_AW;
                            addr_q <= addr_i;
                            beats_remaining_q <= start_beats;
                        end
                    end
                end

                STATE_AW: begin
                    if (m_axi_awready_i) begin
                        burst_beats_q <= next_burst_beats;
                        burst_beats_sent_q <= '0;
                        state_q <= STATE_W;
                    end
                end

                STATE_W: begin
                    if (w_fire) begin
                        if (final_w_beat) begin
                            state_q <= STATE_B;
                        end else begin
                            burst_beats_sent_q <= burst_beats_sent_q + 5'd1;
                        end
                    end
                end

                STATE_B: begin
                    if (m_axi_bvalid_i) begin
                        if (!b_resp_ok) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_AXI_WRITE);
                        end else begin
                            beats_remaining_q <= beats_remaining_q - BEAT_COUNT_W'(burst_beats_q);
                            addr_q <= addr_q + ADDR_W'(burst_beats_q * BEAT_BYTES);
                            if (beats_remaining_q == BEAT_COUNT_W'(burst_beats_q)) begin
                                state_q <= STATE_DONE;
                            end else begin
                                state_q <= STATE_AW;
                            end
                        end
                    end
                end

                STATE_DONE: begin
                    if (start_i) begin
                        if (!start_aligned || !start_nonzero) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_UNSUPPORTED_ALIGNMENT);
                        end else begin
                            state_q <= STATE_AW;
                            addr_q <= addr_i;
                            beats_remaining_q <= start_beats;
                            error_code_q <= 32'(NPU_ERR_NONE);
                        end
                    end
                end

                STATE_ERR: begin
                    if (start_i) begin
                        if (!start_aligned || !start_nonzero) begin
                            error_code_q <= 32'(NPU_ERR_UNSUPPORTED_ALIGNMENT);
                        end else begin
                            state_q <= STATE_AW;
                            addr_q <= addr_i;
                            beats_remaining_q <= start_beats;
                            error_code_q <= 32'(NPU_ERR_NONE);
                        end
                    end
                end

                default: begin
                    state_q <= STATE_IDLE;
                    error_code_q <= 32'(NPU_ERR_INTERNAL_PROTOCOL);
                end
            endcase
        end
    end

endmodule
