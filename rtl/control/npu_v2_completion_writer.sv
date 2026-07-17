/* verilator lint_off DECLFILENAME */

module npu_v2_completion_writer_core #(
    parameter int unsigned AXI_ADDR_W = 64,
    parameter int unsigned AXI_DATA_W = 128,
    localparam int unsigned AXI_STRB_W = AXI_DATA_W / 8
) (
    input logic                  clk_i,
    input logic                  rst_ni,
    input logic                  soft_reset_i,
    input logic                  start_i,
    input logic [AXI_ADDR_W-1:0] completion_addr_i,
    input logic                  terminal_fault_i,
    input logic [31:0]           fault_code_i,
    input logic [31:0]           debug_pc_i,
    input logic [63:0]           cycle_count_i,
    input logic [63:0]           instret_i,
    output logic                 busy_o,
    output logic                 done_o,
    output logic                 fault_o,
    output logic [31:0]          fault_code_o,
    npu_axi4_if.write_master     m_axi
);

    import npu_v2_pkg::*;

    typedef enum logic [2:0] {
        STATE_IDLE,
        STATE_AW,
        STATE_W0,
        STATE_W1,
        STATE_B,
        STATE_DONE,
        STATE_FAULT
    } state_e;

    localparam logic [1:0] AXI_RESP_OKAY = 2'b00;
    localparam logic [1:0] AXI_RESP_EXOKAY = 2'b01;
    localparam logic [1:0] AXI_BURST_INCR = 2'b01;
    localparam logic [2:0] AXI_BEAT_SIZE = 3'd4;

    state_e state_q;
    logic [AXI_ADDR_W-1:0] completion_addr_q;
    logic [AXI_DATA_W-1:0] beat0_q;
    logic [AXI_DATA_W-1:0] beat1_q;

    logic bresp_ok;

    assign bresp_ok = (m_axi.bresp == AXI_RESP_OKAY) ||
                      (m_axi.bresp == AXI_RESP_EXOKAY);
    assign busy_o = (state_q == STATE_AW) || (state_q == STATE_W0) ||
                    (state_q == STATE_W1) || (state_q == STATE_B);
    assign done_o = state_q == STATE_DONE;
    assign fault_o = state_q == STATE_FAULT;
    assign fault_code_o = fault_o ? NPU_V2_FAULT_AXI_WRITE : NPU_V2_FAULT_NONE;

    assign m_axi.awid = '0;
    assign m_axi.awaddr = completion_addr_q;
    assign m_axi.awlen = 8'd1;
    assign m_axi.awsize = AXI_BEAT_SIZE;
    assign m_axi.awburst = AXI_BURST_INCR;
    assign m_axi.awvalid = state_q == STATE_AW;
    assign m_axi.wdata = state_q == STATE_W0 ? beat0_q : beat1_q;
    assign m_axi.wstrb = {AXI_STRB_W{1'b1}};
    assign m_axi.wlast = state_q == STATE_W1;
    assign m_axi.wvalid = (state_q == STATE_W0) || (state_q == STATE_W1);
    assign m_axi.bready = state_q == STATE_B;

    initial begin
        if (AXI_ADDR_W != 64) $fatal("V2 completion writer requires 64-bit AXI addresses");
        if (AXI_DATA_W != 128) $fatal("V2 completion writer requires 128-bit AXI data");
        if (NPU_V2_COMPLETION_RECORD_SIZE != 32) $fatal("Unexpected completion record size");
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state_q <= STATE_IDLE;
            completion_addr_q <= '0;
            beat0_q <= '0;
            beat1_q <= '0;
        end else if (soft_reset_i) begin
            state_q <= STATE_IDLE;
            completion_addr_q <= '0;
            beat0_q <= '0;
            beat1_q <= '0;
        end else begin
            unique case (state_q)
                STATE_IDLE: begin
                    if (start_i) begin
                        if ((completion_addr_i == '0) ||
                            (completion_addr_i[3:0] != 4'h0)) begin
                            state_q <= STATE_FAULT;
                        end else begin
                            completion_addr_q <= completion_addr_i;
                            beat0_q <= {
                                debug_pc_i,
                                terminal_fault_i ? fault_code_i : NPU_V2_FAULT_NONE,
                                terminal_fault_i ? NPU_V2_COMPLETION_STATUS_FAULT :
                                                   NPU_V2_COMPLETION_STATUS_DONE,
                                NPU_V2_ABI_VERSION_RESET
                            };
                            beat1_q <= {instret_i, cycle_count_i};
                            state_q <= STATE_AW;
                        end
                    end
                end

                STATE_AW: begin
                    if (m_axi.awready) begin
                        state_q <= STATE_W0;
                    end
                end

                STATE_W0: begin
                    if (m_axi.wready) begin
                        state_q <= STATE_W1;
                    end
                end

                STATE_W1: begin
                    if (m_axi.wready) begin
                        state_q <= STATE_B;
                    end
                end

                STATE_B: begin
                    if (m_axi.bvalid) begin
                        state_q <= bresp_ok ? STATE_DONE : STATE_FAULT;
                    end
                end

                STATE_DONE,
                STATE_FAULT: begin
                    state_q <= STATE_IDLE;
                end

                default: begin
                    state_q <= STATE_FAULT;
                end
            endcase
        end
    end

    v2_completion_writer_profile: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            m_axi.awvalid |->
                (m_axi.awaddr[3:0] == 4'h0) &&
                (m_axi.awlen == 8'd1) &&
                (m_axi.awsize == AXI_BEAT_SIZE) &&
                (m_axi.awburst == AXI_BURST_INCR)
    );
    v2_completion_writer_full_strobe: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            m_axi.wvalid |-> (m_axi.wstrb == {AXI_STRB_W{1'b1}})
    );
    v2_completion_writer_last_second_beat: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            m_axi.wvalid |-> (m_axi.wlast == (state_q == STATE_W1))
    );
    v2_completion_writer_success_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i) done_o
    );
    v2_completion_writer_fault_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i) fault_o
    );

endmodule

/* verilator lint_on DECLFILENAME */
