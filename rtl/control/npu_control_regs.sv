/* verilator lint_off DECLFILENAME */
`include "npu_assert.svh"

module npu_control_regs_core #(
    parameter int unsigned ADDR_W = 12,
    parameter int unsigned DATA_W = 32,
    localparam int unsigned STRB_W = DATA_W / 8
) (
    npu_axi_lite_if.slave         s_axil,

    input  logic                  backend_done_i,
    input  logic                  backend_error_i,
    input  logic [31:0]           backend_error_code_i,
    input  logic                  backend_busy_i,
    input  logic                  backend_clear_perf_i,
    input  logic                  backend_irq_on_done_i,
    input  logic                  backend_irq_on_error_i,

    output logic                  command_start_o,
    output logic [63:0]           command_desc_addr_o,
    output logic                  soft_reset_o,
    output logic                  clear_perf_o,
    output logic                  irq_o
);

    import npu_pkg::*;

    localparam logic [1:0] AXI_RESP_OKAY = 2'b00;
    localparam logic [1:0] AXI_RESP_SLVERR = 2'b10;
    localparam logic [STRB_W-1:0] FULL_STRB = {STRB_W{1'b1}};

    localparam logic [ADDR_W-1:0] REG_DEVICE_ID          = ADDR_W'(NPU_REG_DEVICE_ID);
    localparam logic [ADDR_W-1:0] REG_ABI_VERSION        = ADDR_W'(NPU_REG_ABI_VERSION);
    localparam logic [ADDR_W-1:0] REG_CAP0               = ADDR_W'(NPU_REG_CAP0);
    localparam logic [ADDR_W-1:0] REG_CAP1               = ADDR_W'(NPU_REG_CAP1);
    localparam logic [ADDR_W-1:0] REG_CONTROL            = ADDR_W'(NPU_REG_CONTROL);
    localparam logic [ADDR_W-1:0] REG_STATUS             = ADDR_W'(NPU_REG_STATUS);
    localparam logic [ADDR_W-1:0] REG_ERROR_CODE         = ADDR_W'(NPU_REG_ERROR_CODE);
    localparam logic [ADDR_W-1:0] REG_IRQ_ENABLE         = ADDR_W'(NPU_REG_IRQ_ENABLE);
    localparam logic [ADDR_W-1:0] REG_IRQ_STATUS         = ADDR_W'(NPU_REG_IRQ_STATUS);
    localparam logic [ADDR_W-1:0] REG_DOORBELL           = ADDR_W'(NPU_REG_DOORBELL);
    localparam logic [ADDR_W-1:0] REG_DESC_ADDR_LO       = ADDR_W'(NPU_REG_DESC_ADDR_LO);
    localparam logic [ADDR_W-1:0] REG_DESC_ADDR_HI       = ADDR_W'(NPU_REG_DESC_ADDR_HI);
    localparam logic [ADDR_W-1:0] REG_CLEAR              = ADDR_W'(NPU_REG_CLEAR);
    localparam logic [ADDR_W-1:0] REG_RESERVED_034       = ADDR_W'(NPU_REG_RESERVED_034);
    localparam logic [ADDR_W-1:0] REG_RESERVED_038       = ADDR_W'(NPU_REG_RESERVED_038);
    localparam logic [ADDR_W-1:0] REG_RESERVED_03C       = ADDR_W'(NPU_REG_RESERVED_03C);
    localparam logic [ADDR_W-1:0] REG_PERF_CYCLES_LO     = ADDR_W'(NPU_REG_PERF_CYCLES_LO);
    localparam logic [ADDR_W-1:0] REG_PERF_CYCLES_HI     = ADDR_W'(NPU_REG_PERF_CYCLES_HI);
    localparam logic [ADDR_W-1:0] REG_PERF_BUSY_LO       = ADDR_W'(NPU_REG_PERF_BUSY_LO);
    localparam logic [ADDR_W-1:0] REG_PERF_BUSY_HI       = ADDR_W'(NPU_REG_PERF_BUSY_HI);
    localparam logic [ADDR_W-1:0] REG_PERF_DESC_COUNT    = ADDR_W'(NPU_REG_PERF_DESC_COUNT);
    localparam logic [ADDR_W-1:0] REG_PERF_ERROR_COUNT   = ADDR_W'(NPU_REG_PERF_ERROR_COUNT);

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
    end

    logic [31:0] desc_addr_lo_q;
    logic [31:0] desc_addr_hi_q;
    logic [1:0]  irq_enable_q;
    logic [1:0]  irq_status_q;
    logic        busy_q;
    logic        done_q;
    logic        error_q;
    logic [31:0] error_code_q;
    logic [63:0] perf_cycles_q;
    logic [63:0] perf_busy_cycles_q;
    logic [31:0] perf_desc_count_q;
    logic [31:0] perf_error_count_q;

    logic        bvalid_q;
    logic [1:0]  bresp_q;
    logic        rvalid_q;
    logic [1:0]  rresp_q;
    logic [31:0] rdata_q;
    logic        command_start_q;
    logic        soft_reset_q;
    logic        clear_perf_q;
    logic        aw_pending_q;
    logic [ADDR_W-1:0] awaddr_q;
    logic        w_pending_q;
    logic [31:0] wdata_q;
    logic [STRB_W-1:0] wstrb_q;

    logic        aw_accept;
    logic        w_accept;
    logic        write_fire;
    logic        read_fire;
    logic [ADDR_W-1:0] write_addr;
    logic [31:0] write_data;
    logic [STRB_W-1:0] write_strb;
    logic [31:0] status_value;

    assign s_axil.awready = !aw_pending_q && !bvalid_q;
    assign s_axil.wready  = !w_pending_q && !bvalid_q;
    assign s_axil.bvalid  = bvalid_q;
    assign s_axil.bresp   = bresp_q;

    assign s_axil.arready = !rvalid_q;
    assign s_axil.rvalid  = rvalid_q;
    assign s_axil.rresp   = rresp_q;
    assign s_axil.rdata   = rdata_q;

    assign aw_accept = s_axil.awvalid && s_axil.awready;
    assign w_accept = s_axil.wvalid && s_axil.wready;
    assign write_fire = !bvalid_q && (aw_pending_q || aw_accept) &&
                        (w_pending_q || w_accept);
    assign read_fire = s_axil.arvalid && s_axil.arready;
    assign write_addr = aw_pending_q ? awaddr_q : s_axil.awaddr;
    assign write_data = w_pending_q ? wdata_q : s_axil.wdata;
    assign write_strb = w_pending_q ? wstrb_q : s_axil.wstrb;

    assign command_start_o = command_start_q;
    assign command_desc_addr_o = {desc_addr_hi_q, desc_addr_lo_q};
    assign soft_reset_o = soft_reset_q;
    assign clear_perf_o = clear_perf_q;
    assign irq_o = |(irq_enable_q & irq_status_q);

    assign status_value = {
        27'd0,
        irq_o,
        error_q,
        done_q,
        busy_q,
        !busy_q
    };

    function automatic logic [31:0] apply_wstrb(
        input logic [31:0] old_value,
        input logic [31:0] new_value,
        input logic [3:0]  strb
    );
        logic [31:0] merged;

        merged = old_value;
        for (int unsigned i = 0; i < 4; i++) begin
            if (strb[i]) begin
                merged[(i * 8) +: 8] = new_value[(i * 8) +: 8];
            end
        end

        apply_wstrb = merged;
    endfunction

    task automatic reset_regs;
        begin
            desc_addr_lo_q     <= 32'h0000_0000;
            desc_addr_hi_q     <= 32'h0000_0000;
            irq_enable_q       <= 2'b00;
            irq_status_q       <= 2'b00;
            busy_q             <= 1'b0;
            done_q             <= 1'b0;
            error_q            <= 1'b0;
            error_code_q       <= 32'h0000_0000;
            perf_cycles_q      <= 64'h0000_0000_0000_0000;
            perf_busy_cycles_q <= 64'h0000_0000_0000_0000;
            perf_desc_count_q  <= 32'h0000_0000;
            perf_error_count_q <= 32'h0000_0000;
        end
    endtask

    task automatic clear_perf;
        begin
            perf_cycles_q      <= 64'h0000_0000_0000_0000;
            perf_busy_cycles_q <= 64'h0000_0000_0000_0000;
            perf_desc_count_q  <= 32'h0000_0000;
            perf_error_count_q <= 32'h0000_0000;
        end
    endtask

    always_ff @(posedge s_axil.aclk_i or negedge s_axil.aresetn_i) begin
        if (!s_axil.aresetn_i) begin
            reset_regs();
            bvalid_q <= 1'b0;
            bresp_q <= AXI_RESP_OKAY;
            rvalid_q <= 1'b0;
            rresp_q <= AXI_RESP_OKAY;
            rdata_q <= 32'h0000_0000;
            command_start_q <= 1'b0;
            soft_reset_q <= 1'b0;
            clear_perf_q <= 1'b0;
            aw_pending_q <= 1'b0;
            awaddr_q <= '0;
            w_pending_q <= 1'b0;
            wdata_q <= 32'h0000_0000;
            wstrb_q <= '0;
        end else begin
            command_start_q <= 1'b0;
            soft_reset_q <= 1'b0;
            clear_perf_q <= 1'b0;

            if (aw_accept && !write_fire) begin
                aw_pending_q <= 1'b1;
                awaddr_q <= s_axil.awaddr;
            end else if (write_fire) begin
                aw_pending_q <= 1'b0;
            end

            if (w_accept && !write_fire) begin
                w_pending_q <= 1'b1;
                wdata_q <= s_axil.wdata;
                wstrb_q <= s_axil.wstrb;
            end else if (write_fire) begin
                w_pending_q <= 1'b0;
            end

            if (backend_clear_perf_i) begin
                clear_perf();
            end else if (busy_q) begin
                perf_cycles_q <= perf_cycles_q + 64'd1;
                if (backend_busy_i) begin
                    perf_busy_cycles_q <= perf_busy_cycles_q + 64'd1;
                end
            end

            if (backend_error_i && busy_q) begin
                busy_q <= 1'b0;
                done_q <= 1'b0;
                error_q <= 1'b1;
                error_code_q <= backend_error_code_i;
                perf_error_count_q <= perf_error_count_q + 32'd1;
                if (backend_irq_on_error_i) begin
                    irq_status_q[1] <= 1'b1;
                end
            end else if (backend_done_i && busy_q) begin
                busy_q <= 1'b0;
                done_q <= 1'b1;
                error_q <= 1'b0;
                error_code_q <= 32'h0000_0000;
                perf_desc_count_q <= perf_desc_count_q + 32'd1;
                if (backend_irq_on_done_i) begin
                    irq_status_q[0] <= 1'b1;
                end
            end

            if (bvalid_q && s_axil.bready) begin
                bvalid_q <= 1'b0;
            end

            if (rvalid_q && s_axil.rready) begin
                rvalid_q <= 1'b0;
            end

            if (write_fire) begin
                bvalid_q <= 1'b1;
                bresp_q <= AXI_RESP_OKAY;

                unique case (write_addr)
                    REG_CONTROL: begin
                        if ((write_strb != FULL_STRB) || (write_data[31:1] != 31'd0)) begin
                            bresp_q <= AXI_RESP_SLVERR;
                        end else if (write_data[0]) begin
                            reset_regs();
                            soft_reset_q <= 1'b1;
                            clear_perf_q <= 1'b1;
                        end
                    end

                    REG_IRQ_ENABLE: begin
                        if (apply_wstrb({30'd0, irq_enable_q}, write_data, write_strb)[31:2] != 30'd0) begin
                            bresp_q <= AXI_RESP_SLVERR;
                        end else begin
                            irq_enable_q <= apply_wstrb({30'd0, irq_enable_q}, write_data, write_strb)[1:0];
                        end
                    end

                    REG_IRQ_STATUS: begin
                        if (apply_wstrb({30'd0, irq_status_q}, write_data, write_strb)[31:2] != 30'd0) begin
                            bresp_q <= AXI_RESP_SLVERR;
                        end else begin
                            irq_status_q <= irq_status_q &
                                ~apply_wstrb(32'h0000_0000, write_data, write_strb)[1:0];
                        end
                    end

                    REG_DOORBELL: begin
                        if ((write_strb != FULL_STRB) || (write_data[31:1] != 31'd0)) begin
                            bresp_q <= AXI_RESP_SLVERR;
                        end else if (write_data[0]) begin
                            if (busy_q) begin
                                bresp_q <= AXI_RESP_SLVERR;
                            end else begin
                                busy_q <= 1'b1;
                                done_q <= 1'b0;
                                error_q <= 1'b0;
                                error_code_q <= 32'h0000_0000;
                                command_start_q <= 1'b1;
                            end
                        end
                    end

                    REG_DESC_ADDR_LO: begin
                        desc_addr_lo_q <= apply_wstrb(desc_addr_lo_q, write_data, write_strb);
                    end

                    REG_DESC_ADDR_HI: begin
                        desc_addr_hi_q <= apply_wstrb(desc_addr_hi_q, write_data, write_strb);
                    end

                    REG_CLEAR: begin
                        if ((write_strb != FULL_STRB) || (write_data[31:3] != 29'd0)) begin
                            bresp_q <= AXI_RESP_SLVERR;
                        end else begin
                            if (write_data[0]) begin
                                done_q <= 1'b0;
                                irq_status_q[0] <= 1'b0;
                            end
                            if (write_data[1]) begin
                                error_q <= 1'b0;
                                error_code_q <= 32'h0000_0000;
                                irq_status_q[1] <= 1'b0;
                            end
                            if (write_data[2]) begin
                                clear_perf();
                                clear_perf_q <= 1'b1;
                            end
                        end
                    end

                    REG_DEVICE_ID,
                    REG_ABI_VERSION,
                    REG_CAP0,
                    REG_CAP1,
                    REG_STATUS,
                    REG_ERROR_CODE,
                    REG_RESERVED_034,
                    REG_RESERVED_038,
                    REG_RESERVED_03C,
                    REG_PERF_CYCLES_LO,
                    REG_PERF_CYCLES_HI,
                    REG_PERF_BUSY_LO,
                    REG_PERF_BUSY_HI,
                    REG_PERF_DESC_COUNT,
                    REG_PERF_ERROR_COUNT: begin
                        bresp_q <= AXI_RESP_SLVERR;
                    end

                    default: begin
                        bresp_q <= AXI_RESP_SLVERR;
                    end
                endcase
            end

            if (read_fire) begin
                rvalid_q <= 1'b1;
                rresp_q <= AXI_RESP_OKAY;
                rdata_q <= 32'h0000_0000;

                unique case (s_axil.araddr)
                    REG_DEVICE_ID: rdata_q <= NPU_DEVICE_ID_RESET;
                    REG_ABI_VERSION: rdata_q <= NPU_ABI_VERSION_RESET;
                    REG_CAP0: rdata_q <= NPU_CAP0_RESET;
                    REG_CAP1: rdata_q <= NPU_CAP1_RESET;
                    REG_CONTROL: rdata_q <= 32'h0000_0000;
                    REG_STATUS: rdata_q <= status_value;
                    REG_ERROR_CODE: rdata_q <= error_code_q;
                    REG_IRQ_ENABLE: rdata_q <= {30'd0, irq_enable_q};
                    REG_IRQ_STATUS: rdata_q <= {30'd0, irq_status_q};
                    REG_DOORBELL: rdata_q <= 32'h0000_0000;
                    REG_DESC_ADDR_LO: rdata_q <= desc_addr_lo_q;
                    REG_DESC_ADDR_HI: rdata_q <= desc_addr_hi_q;
                    REG_CLEAR: rdata_q <= 32'h0000_0000;
                    REG_RESERVED_034,
                    REG_RESERVED_038,
                    REG_RESERVED_03C: rdata_q <= 32'h0000_0000;
                    REG_PERF_CYCLES_LO: rdata_q <= perf_cycles_q[31:0];
                    REG_PERF_CYCLES_HI: rdata_q <= perf_cycles_q[63:32];
                    REG_PERF_BUSY_LO: rdata_q <= perf_busy_cycles_q[31:0];
                    REG_PERF_BUSY_HI: rdata_q <= perf_busy_cycles_q[63:32];
                    REG_PERF_DESC_COUNT: rdata_q <= perf_desc_count_q;
                    REG_PERF_ERROR_COUNT: rdata_q <= perf_error_count_q;
                    default: begin
                        rresp_q <= AXI_RESP_SLVERR;
                        rdata_q <= 32'h0000_0000;
                    end
                endcase
            end
        end
    end

    `HOLON_NPU_ASSERT(control_status_state_is_legal,
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            !(busy_q && (done_q || error_q)) && !(done_q && error_q))
    `HOLON_NPU_ASSERT(control_irq_output_matches_state,
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            irq_o == |(irq_enable_q & irq_status_q))
    `HOLON_NPU_ASSERT(control_doorbell_busy_is_rejected,
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            write_fire && (write_addr == REG_DOORBELL) &&
            (write_strb == FULL_STRB) && (write_data == 32'h0000_0001) && busy_q
            |=> bvalid_q && (bresp_q == AXI_RESP_SLVERR) && !command_start_q)

endmodule
/* verilator lint_on DECLFILENAME */
