/* verilator lint_off DECLFILENAME */

module npu_v2_control_regs_core #(
    parameter int unsigned ADDR_W = 12,
    parameter int unsigned DATA_W = 32,
    localparam int unsigned STRB_W = DATA_W / 8
) (
    npu_axi_lite_if.slave         s_axil,

    input  logic                  loader_done_i,
    input  logic                  frontend_done_i,
    input  logic                  frontend_fault_i,
    input  logic [31:0]           frontend_fault_code_i,
    input  logic                  frontend_halted_i,
    input  logic [31:0]           frontend_debug_pc_i,
    input  logic [63:0]           frontend_instret_i,

    output logic                  program_start_o,
    output logic [63:0]           program_desc_addr_o,
    output logic                  soft_reset_o,
    output logic                  halt_request_o,
    output logic                  resume_o,
    output logic                  debug_step_o,
    output logic                  clear_perf_o,
    output logic                  irq_o
);

    import npu_v2_pkg::*;

    localparam logic [1:0] AXI_RESP_OKAY = 2'b00;
    localparam logic [1:0] AXI_RESP_SLVERR = 2'b10;
    localparam logic [STRB_W-1:0] FULL_STRB = {STRB_W{1'b1}};

    localparam logic [ADDR_W-1:0] REG_DEVICE_ID             = ADDR_W'(NPU_V2_REG_DEVICE_ID);
    localparam logic [ADDR_W-1:0] REG_ABI_VERSION           = ADDR_W'(NPU_V2_REG_ABI_VERSION);
    localparam logic [ADDR_W-1:0] REG_ISA_VERSION           = ADDR_W'(NPU_V2_REG_ISA_VERSION);
    localparam logic [ADDR_W-1:0] REG_CAP0_LO               = ADDR_W'(NPU_V2_REG_CAP0_LO);
    localparam logic [ADDR_W-1:0] REG_CAP0_HI               = ADDR_W'(NPU_V2_REG_CAP0_HI);
    localparam logic [ADDR_W-1:0] REG_OP_CLASS_LO           = ADDR_W'(NPU_V2_REG_OP_CLASS_LO);
    localparam logic [ADDR_W-1:0] REG_OP_CLASS_HI           = ADDR_W'(NPU_V2_REG_OP_CLASS_HI);
    localparam logic [ADDR_W-1:0] REG_PROGRAM_MEM_BYTES     = ADDR_W'(NPU_V2_REG_PROGRAM_MEM_BYTES);
    localparam logic [ADDR_W-1:0] REG_LOCAL_MEM_BYTES       = ADDR_W'(NPU_V2_REG_LOCAL_MEM_BYTES);
    localparam logic [ADDR_W-1:0] REG_VECTOR_CAP0           = ADDR_W'(NPU_V2_REG_VECTOR_CAP0);
    localparam logic [ADDR_W-1:0] REG_MATRIX_CAP0           = ADDR_W'(NPU_V2_REG_MATRIX_CAP0);
    localparam logic [ADDR_W-1:0] REG_CONTROL               = ADDR_W'(NPU_V2_REG_CONTROL);
    localparam logic [ADDR_W-1:0] REG_STATUS                = ADDR_W'(NPU_V2_REG_STATUS);
    localparam logic [ADDR_W-1:0] REG_FAULT_CODE            = ADDR_W'(NPU_V2_REG_FAULT_CODE);
    localparam logic [ADDR_W-1:0] REG_DEBUG_PC              = ADDR_W'(NPU_V2_REG_DEBUG_PC);
    localparam logic [ADDR_W-1:0] REG_PROGRAM_DESC_ADDR_LO  = ADDR_W'(NPU_V2_REG_PROGRAM_DESC_ADDR_LO);
    localparam logic [ADDR_W-1:0] REG_PROGRAM_DESC_ADDR_HI  = ADDR_W'(NPU_V2_REG_PROGRAM_DESC_ADDR_HI);
    localparam logic [ADDR_W-1:0] REG_DOORBELL              = ADDR_W'(NPU_V2_REG_DOORBELL);
    localparam logic [ADDR_W-1:0] REG_IRQ_ENABLE            = ADDR_W'(NPU_V2_REG_IRQ_ENABLE);
    localparam logic [ADDR_W-1:0] REG_IRQ_STATUS            = ADDR_W'(NPU_V2_REG_IRQ_STATUS);
    localparam logic [ADDR_W-1:0] REG_IRQ_CLEAR             = ADDR_W'(NPU_V2_REG_IRQ_CLEAR);
    localparam logic [ADDR_W-1:0] REG_PERF_CYCLE_LO         = ADDR_W'(NPU_V2_REG_PERF_CYCLE_LO);
    localparam logic [ADDR_W-1:0] REG_PERF_CYCLE_HI         = ADDR_W'(NPU_V2_REG_PERF_CYCLE_HI);
    localparam logic [ADDR_W-1:0] REG_PERF_INSTRET_LO       = ADDR_W'(NPU_V2_REG_PERF_INSTRET_LO);
    localparam logic [ADDR_W-1:0] REG_PERF_INSTRET_HI       = ADDR_W'(NPU_V2_REG_PERF_INSTRET_HI);

    logic [31:0] desc_addr_lo_q;
    logic [31:0] desc_addr_hi_q;
    logic [31:0] irq_enable_q;
    logic [31:0] irq_status_q;
    logic [31:0] lifecycle_q;
    logic [31:0] fault_code_q;
    logic [31:0] debug_pc_q;
    logic [63:0] perf_cycle_q;
    logic [63:0] perf_instret_q;

    logic        bvalid_q;
    logic [1:0]  bresp_q;
    logic        rvalid_q;
    logic [1:0]  rresp_q;
    logic [31:0] rdata_q;
    logic        aw_pending_q;
    logic [ADDR_W-1:0] awaddr_q;
    logic        w_pending_q;
    logic [31:0] wdata_q;
    logic [STRB_W-1:0] wstrb_q;

    logic        program_start_q;
    logic        soft_reset_q;
    logic        halt_request_q;
    logic        resume_q;
    logic        debug_step_q;
    logic        clear_perf_q;

    logic        aw_accept;
    logic        w_accept;
    logic        write_fire;
    logic        read_fire;
    logic [ADDR_W-1:0] write_addr;
    logic [31:0] write_data;
    logic [STRB_W-1:0] write_strb;
    logic [31:0] status_value;
    logic [31:0] irq_pending_masked;
    logic        lifecycle_active;
    logic        lifecycle_terminal;

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

    assign program_start_o = program_start_q;
    assign program_desc_addr_o = {desc_addr_hi_q, desc_addr_lo_q};
    assign soft_reset_o = soft_reset_q;
    assign halt_request_o = halt_request_q;
    assign resume_o = resume_q;
    assign debug_step_o = debug_step_q;
    assign clear_perf_o = clear_perf_q;

    assign irq_pending_masked = irq_enable_q & irq_status_q & NPU_V2_IRQ_VALID_MASK;
    assign irq_o = |irq_pending_masked;
    assign status_value = lifecycle_q | (irq_o ? NPU_V2_STATUS_IRQ_PENDING : 32'h0000_0000);
    assign lifecycle_active = (lifecycle_q == NPU_V2_STATUS_LOADING) ||
                              (lifecycle_q == NPU_V2_STATUS_RUNNING) ||
                              (lifecycle_q == NPU_V2_STATUS_HALTED);
    assign lifecycle_terminal = (lifecycle_q == NPU_V2_STATUS_DONE) ||
                                (lifecycle_q == NPU_V2_STATUS_FAULT);

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

    function automatic logic has_multiple_bits(input logic [31:0] value);
        has_multiple_bits = (value & (value - 32'd1)) != 32'h0000_0000;
    endfunction

    task automatic reset_regs;
        begin
            desc_addr_lo_q <= NPU_V2_RESET_PROGRAM_DESC_ADDR_LO;
            desc_addr_hi_q <= NPU_V2_RESET_PROGRAM_DESC_ADDR_HI;
            irq_enable_q <= NPU_V2_RESET_IRQ_ENABLE;
            irq_status_q <= NPU_V2_RESET_IRQ_STATUS;
            lifecycle_q <= NPU_V2_STATUS_IDLE;
            fault_code_q <= NPU_V2_RESET_FAULT_CODE;
            debug_pc_q <= NPU_V2_RESET_DEBUG_PC;
            perf_cycle_q <= 64'h0000_0000_0000_0000;
            perf_instret_q <= 64'h0000_0000_0000_0000;
        end
    endtask

    task automatic clear_perf;
        begin
            perf_cycle_q <= 64'h0000_0000_0000_0000;
            perf_instret_q <= 64'h0000_0000_0000_0000;
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
            aw_pending_q <= 1'b0;
            awaddr_q <= '0;
            w_pending_q <= 1'b0;
            wdata_q <= 32'h0000_0000;
            wstrb_q <= '0;
            program_start_q <= 1'b0;
            soft_reset_q <= 1'b0;
            halt_request_q <= 1'b0;
            resume_q <= 1'b0;
            debug_step_q <= 1'b0;
            clear_perf_q <= 1'b0;
        end else begin
            program_start_q <= 1'b0;
            soft_reset_q <= 1'b0;
            halt_request_q <= 1'b0;
            resume_q <= 1'b0;
            debug_step_q <= 1'b0;
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

            if (lifecycle_active) begin
                perf_cycle_q <= perf_cycle_q + 64'd1;
                perf_instret_q <= frontend_instret_i;
            end

            if (frontend_fault_i && lifecycle_active) begin
                lifecycle_q <= NPU_V2_STATUS_FAULT;
                fault_code_q <= frontend_fault_code_i;
                debug_pc_q <= frontend_debug_pc_i;
                irq_status_q <= irq_status_q | NPU_V2_IRQ_FAULT;
            end else if (frontend_done_i && lifecycle_active) begin
                lifecycle_q <= NPU_V2_STATUS_DONE;
                fault_code_q <= NPU_V2_FAULT_NONE;
                debug_pc_q <= frontend_debug_pc_i;
                irq_status_q <= irq_status_q | NPU_V2_IRQ_DONE;
            end else if (frontend_halted_i && (lifecycle_q == NPU_V2_STATUS_RUNNING)) begin
                lifecycle_q <= NPU_V2_STATUS_HALTED;
                debug_pc_q <= frontend_debug_pc_i;
                irq_status_q <= irq_status_q | NPU_V2_IRQ_HALTED;
            end else if (loader_done_i && (lifecycle_q == NPU_V2_STATUS_LOADING)) begin
                lifecycle_q <= NPU_V2_STATUS_RUNNING;
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
                        if ((write_strb != FULL_STRB) ||
                            ((write_data & ~NPU_V2_CONTROL_VALID_MASK) != 32'h0000_0000) ||
                            has_multiple_bits(write_data & NPU_V2_CONTROL_VALID_MASK)) begin
                            bresp_q <= AXI_RESP_SLVERR;
                        end else begin
                            if ((write_data & NPU_V2_CONTROL_SOFT_RESET) != 32'h0000_0000) begin
                                reset_regs();
                                soft_reset_q <= 1'b1;
                                clear_perf_q <= 1'b1;
                            end else begin
                                if ((write_data & NPU_V2_CONTROL_CLEAR_TERMINAL) != 32'h0000_0000) begin
                                    if (lifecycle_terminal) begin
                                        lifecycle_q <= NPU_V2_STATUS_IDLE;
                                        fault_code_q <= NPU_V2_FAULT_NONE;
                                        debug_pc_q <= 32'h0000_0000;
                                    end else begin
                                        bresp_q <= AXI_RESP_SLVERR;
                                    end
                                end
                                if ((write_data & NPU_V2_CONTROL_HALT) != 32'h0000_0000) begin
                                    if (lifecycle_q == NPU_V2_STATUS_RUNNING) begin
                                        halt_request_q <= 1'b1;
                                    end else begin
                                        bresp_q <= AXI_RESP_SLVERR;
                                    end
                                end
                                if ((write_data & NPU_V2_CONTROL_RESUME) != 32'h0000_0000) begin
                                    if (lifecycle_q == NPU_V2_STATUS_HALTED) begin
                                        lifecycle_q <= NPU_V2_STATUS_RUNNING;
                                        resume_q <= 1'b1;
                                    end else begin
                                        bresp_q <= AXI_RESP_SLVERR;
                                    end
                                end
                                if ((write_data & NPU_V2_CONTROL_DEBUG_STEP) != 32'h0000_0000) begin
                                    if (lifecycle_q == NPU_V2_STATUS_HALTED) begin
                                        debug_step_q <= 1'b1;
                                        irq_status_q <= irq_status_q | NPU_V2_IRQ_DEBUG_STEP;
                                    end else begin
                                        bresp_q <= AXI_RESP_SLVERR;
                                    end
                                end
                            end
                        end
                    end

                    REG_PROGRAM_DESC_ADDR_LO: begin
                        desc_addr_lo_q <= apply_wstrb(desc_addr_lo_q, write_data, write_strb);
                    end

                    REG_PROGRAM_DESC_ADDR_HI: begin
                        desc_addr_hi_q <= apply_wstrb(desc_addr_hi_q, write_data, write_strb);
                    end

                    REG_DOORBELL: begin
                        if ((write_strb != FULL_STRB) || (write_data[31:1] != 31'd0)) begin
                            bresp_q <= AXI_RESP_SLVERR;
                        end else if (write_data[0]) begin
                            if (lifecycle_q != NPU_V2_STATUS_IDLE) begin
                                bresp_q <= AXI_RESP_SLVERR;
                            end else if (desc_addr_lo_q[3:0] != 4'h0) begin
                                lifecycle_q <= NPU_V2_STATUS_FAULT;
                                fault_code_q <= NPU_V2_FAULT_ALIGNMENT;
                                debug_pc_q <= 32'h0000_0000;
                                irq_status_q <= irq_status_q | NPU_V2_IRQ_FAULT;
                            end else begin
                                lifecycle_q <= NPU_V2_STATUS_LOADING;
                                fault_code_q <= NPU_V2_FAULT_NONE;
                                debug_pc_q <= 32'h0000_0000;
                                program_start_q <= 1'b1;
                            end
                        end
                    end

                    REG_IRQ_ENABLE: begin
                        if ((apply_wstrb(irq_enable_q, write_data, write_strb) &
                             ~NPU_V2_IRQ_VALID_MASK) != 32'h0000_0000) begin
                            bresp_q <= AXI_RESP_SLVERR;
                        end else begin
                            irq_enable_q <= apply_wstrb(irq_enable_q, write_data, write_strb);
                        end
                    end

                    REG_IRQ_CLEAR: begin
                        if ((write_strb != FULL_STRB) ||
                            ((write_data & ~NPU_V2_IRQ_VALID_MASK) != 32'h0000_0000)) begin
                            bresp_q <= AXI_RESP_SLVERR;
                        end else begin
                            irq_status_q <= irq_status_q & ~write_data;
                        end
                    end

                    REG_DEVICE_ID,
                    REG_ABI_VERSION,
                    REG_ISA_VERSION,
                    REG_CAP0_LO,
                    REG_CAP0_HI,
                    REG_OP_CLASS_LO,
                    REG_OP_CLASS_HI,
                    REG_PROGRAM_MEM_BYTES,
                    REG_LOCAL_MEM_BYTES,
                    REG_VECTOR_CAP0,
                    REG_MATRIX_CAP0,
                    REG_STATUS,
                    REG_FAULT_CODE,
                    REG_DEBUG_PC,
                    REG_IRQ_STATUS,
                    REG_PERF_CYCLE_LO,
                    REG_PERF_CYCLE_HI,
                    REG_PERF_INSTRET_LO,
                    REG_PERF_INSTRET_HI: begin
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
                    REG_DEVICE_ID: rdata_q <= NPU_V2_RESET_DEVICE_ID;
                    REG_ABI_VERSION: rdata_q <= NPU_V2_RESET_ABI_VERSION;
                    REG_ISA_VERSION: rdata_q <= NPU_V2_RESET_ISA_VERSION;
                    REG_CAP0_LO: rdata_q <= NPU_V2_RESET_CAP0_LO;
                    REG_CAP0_HI: rdata_q <= NPU_V2_RESET_CAP0_HI;
                    REG_OP_CLASS_LO: rdata_q <= NPU_V2_RESET_OP_CLASS_LO;
                    REG_OP_CLASS_HI: rdata_q <= NPU_V2_RESET_OP_CLASS_HI;
                    REG_PROGRAM_MEM_BYTES: rdata_q <= NPU_V2_RESET_PROGRAM_MEM_BYTES;
                    REG_LOCAL_MEM_BYTES: rdata_q <= NPU_V2_RESET_LOCAL_MEM_BYTES;
                    REG_VECTOR_CAP0: rdata_q <= NPU_V2_RESET_VECTOR_CAP0;
                    REG_MATRIX_CAP0: rdata_q <= NPU_V2_RESET_MATRIX_CAP0;
                    REG_CONTROL: rdata_q <= NPU_V2_RESET_CONTROL;
                    REG_STATUS: rdata_q <= status_value;
                    REG_FAULT_CODE: rdata_q <= fault_code_q;
                    REG_DEBUG_PC: rdata_q <= debug_pc_q;
                    REG_PROGRAM_DESC_ADDR_LO: rdata_q <= desc_addr_lo_q;
                    REG_PROGRAM_DESC_ADDR_HI: rdata_q <= desc_addr_hi_q;
                    REG_DOORBELL: rdata_q <= NPU_V2_RESET_DOORBELL;
                    REG_IRQ_ENABLE: rdata_q <= irq_enable_q;
                    REG_IRQ_STATUS: rdata_q <= irq_status_q & NPU_V2_IRQ_VALID_MASK;
                    REG_IRQ_CLEAR: rdata_q <= NPU_V2_RESET_IRQ_CLEAR;
                    REG_PERF_CYCLE_LO: rdata_q <= perf_cycle_q[31:0];
                    REG_PERF_CYCLE_HI: rdata_q <= perf_cycle_q[63:32];
                    REG_PERF_INSTRET_LO: rdata_q <= perf_instret_q[31:0];
                    REG_PERF_INSTRET_HI: rdata_q <= perf_instret_q[63:32];
                    default: begin
                        rresp_q <= AXI_RESP_SLVERR;
                        rdata_q <= 32'h0000_0000;
                    end
                endcase
            end
        end
    end

    v2_control_lifecycle_onehot: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            lifecycle_q == NPU_V2_STATUS_IDLE ||
            lifecycle_q == NPU_V2_STATUS_LOADING ||
            lifecycle_q == NPU_V2_STATUS_RUNNING ||
            lifecycle_q == NPU_V2_STATUS_HALTED ||
            lifecycle_q == NPU_V2_STATUS_DONE ||
            lifecycle_q == NPU_V2_STATUS_FAULT
    );
    v2_control_irq_output_matches_state: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            irq_o == |(irq_enable_q & irq_status_q & NPU_V2_IRQ_VALID_MASK)
    );
    v2_control_doorbell_active_rejected: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            write_fire && (write_addr == REG_DOORBELL) &&
            (write_strb == FULL_STRB) && (write_data == 32'h0000_0001) &&
            (lifecycle_q != NPU_V2_STATUS_IDLE)
            |=> bvalid_q && (bresp_q == AXI_RESP_SLVERR) && !program_start_q
    );

endmodule
/* verilator lint_on DECLFILENAME */
