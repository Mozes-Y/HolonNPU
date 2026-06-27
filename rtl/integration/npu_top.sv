module npu_top #(
    parameter int unsigned AXIL_ADDR_W = 12,
    parameter int unsigned AXIL_DATA_W = 32,
    parameter int unsigned AXI_ADDR_W = 64,
    parameter int unsigned AXI_DATA_W = 128,
    localparam int unsigned AXIL_STRB_W = AXIL_DATA_W / 8,
    localparam int unsigned AXI_STRB_W = AXI_DATA_W / 8
) (
    input  logic                       clk_i,
    input  logic                       rst_ni,

    input  logic [AXIL_ADDR_W-1:0]     s_axil_awaddr_i,
    input  logic                       s_axil_awvalid_i,
    output logic                       s_axil_awready_o,
    input  logic [AXIL_DATA_W-1:0]     s_axil_wdata_i,
    input  logic [AXIL_STRB_W-1:0]     s_axil_wstrb_i,
    input  logic                       s_axil_wvalid_i,
    output logic                       s_axil_wready_o,
    output logic [1:0]                 s_axil_bresp_o,
    output logic                       s_axil_bvalid_o,
    input  logic                       s_axil_bready_i,

    input  logic [AXIL_ADDR_W-1:0]     s_axil_araddr_i,
    input  logic                       s_axil_arvalid_i,
    output logic                       s_axil_arready_o,
    output logic [AXIL_DATA_W-1:0]     s_axil_rdata_o,
    output logic [1:0]                 s_axil_rresp_o,
    output logic                       s_axil_rvalid_o,
    input  logic                       s_axil_rready_i,

    output logic                       irq_o,
    output logic [3:0]                 stage_o,

    output logic [AXI_ADDR_W-1:0]      m_axi_araddr_o,
    output logic [7:0]                 m_axi_arlen_o,
    output logic [2:0]                 m_axi_arsize_o,
    output logic [1:0]                 m_axi_arburst_o,
    output logic                       m_axi_arvalid_o,
    input  logic                       m_axi_arready_i,
    input  logic [63:0]                m_axi_rdata_lo_i,
    input  logic [63:0]                m_axi_rdata_hi_i,
    input  logic [1:0]                 m_axi_rresp_i,
    input  logic                       m_axi_rlast_i,
    input  logic                       m_axi_rvalid_i,
    output logic                       m_axi_rready_o,

    output logic [AXI_ADDR_W-1:0]      m_axi_awaddr_o,
    output logic [7:0]                 m_axi_awlen_o,
    output logic [2:0]                 m_axi_awsize_o,
    output logic [1:0]                 m_axi_awburst_o,
    output logic                       m_axi_awvalid_o,
    input  logic                       m_axi_awready_i,
    output logic [63:0]                m_axi_wdata_lo_o,
    output logic [63:0]                m_axi_wdata_hi_o,
    output logic [AXI_STRB_W-1:0]      m_axi_wstrb_o,
    output logic                       m_axi_wlast_o,
    output logic                       m_axi_wvalid_o,
    input  logic                       m_axi_wready_i,
    input  logic [1:0]                 m_axi_bresp_i,
    input  logic                       m_axi_bvalid_i,
    output logic                       m_axi_bready_o
);

    typedef enum logic [1:0] {
        READ_OWNER_NONE = 2'd0,
        READ_OWNER_CMD  = 2'd1,
        READ_OWNER_GEMM = 2'd2
    } read_owner_e;

    read_owner_e read_owner_q;

    logic control_start;
    logic [63:0] control_desc_addr;
    logic control_soft_reset;
    logic control_clear_perf;

    /* verilator lint_off UNUSEDSIGNAL */
    logic cmd_done_unused;
    /* verilator lint_on UNUSEDSIGNAL */
    logic cmd_busy;
    logic cmd_error;
    logic [31:0] cmd_error_code;
    logic cmd_irq_on_done;
    logic cmd_irq_on_error;
    logic cmd_clear_perf_on_start;
    logic cmd_valid;
    logic cmd_ready;
    logic [31:0] cmd_m;
    logic [31:0] cmd_n;
    logic [31:0] cmd_k;
    logic [63:0] cmd_a_addr;
    logic [63:0] cmd_b_addr;
    logic [63:0] cmd_c_addr;
    logic [31:0] cmd_a_stride;
    logic [31:0] cmd_b_stride;
    logic [31:0] cmd_c_stride;

    logic [AXI_ADDR_W-1:0] cmd_araddr;
    logic [7:0] cmd_arlen;
    logic [2:0] cmd_arsize;
    logic [1:0] cmd_arburst;
    logic cmd_arvalid;
    logic cmd_arready;
    logic cmd_rvalid;
    logic cmd_rready;

    logic gemm_busy;
    logic gemm_done;
    logic gemm_error;
    logic [31:0] gemm_error_code;
    logic gemm_irq_unused;
    logic [63:0] gemm_perf_cycles_unused;
    logic [63:0] gemm_perf_busy_unused;
    logic [31:0] gemm_perf_desc_unused;
    logic [31:0] gemm_perf_error_unused;
    logic [AXI_ADDR_W-1:0] gemm_araddr;
    logic [7:0] gemm_arlen;
    logic [2:0] gemm_arsize;
    logic [1:0] gemm_arburst;
    logic gemm_arvalid;
    logic gemm_arready;
    logic gemm_rvalid;
    logic gemm_rready;
    logic active_irq_on_done_q;
    logic active_irq_on_error_q;
    logic backend_error;
    logic [31:0] backend_error_code;
    logic backend_clear_perf;
    logic cmd_error_active;
    logic gemm_terminal_mask;
    logic gemm_done_active;
    logic gemm_error_active;

    logic read_select_cmd;
    logic read_select_gemm;
    logic read_fire;
    logic read_last_fire;
    logic [AXI_DATA_W-1:0] shared_rdata;

    assign shared_rdata = {m_axi_rdata_hi_i, m_axi_rdata_lo_i};
    assign read_select_cmd = (read_owner_q == READ_OWNER_NONE) && cmd_arvalid;
    assign read_select_gemm = (read_owner_q == READ_OWNER_NONE) && !cmd_arvalid && gemm_arvalid;

    assign m_axi_arvalid_o = read_select_cmd ? cmd_arvalid :
                             read_select_gemm ? gemm_arvalid : 1'b0;
    assign m_axi_araddr_o = read_select_cmd ? cmd_araddr : gemm_araddr;
    assign m_axi_arlen_o = read_select_cmd ? cmd_arlen : gemm_arlen;
    assign m_axi_arsize_o = read_select_cmd ? cmd_arsize : gemm_arsize;
    assign m_axi_arburst_o = read_select_cmd ? cmd_arburst : gemm_arburst;
    assign cmd_arready = read_select_cmd && m_axi_arready_i;
    assign gemm_arready = read_select_gemm && m_axi_arready_i;

    assign cmd_rvalid = (read_owner_q == READ_OWNER_CMD) && m_axi_rvalid_i;
    assign gemm_rvalid = (read_owner_q == READ_OWNER_GEMM) && m_axi_rvalid_i;
    assign m_axi_rready_o = (read_owner_q == READ_OWNER_CMD) ? cmd_rready :
                            (read_owner_q == READ_OWNER_GEMM) ? gemm_rready : 1'b0;
    assign read_fire = m_axi_arvalid_o && m_axi_arready_i;
    assign read_last_fire = m_axi_rvalid_i && m_axi_rready_o && m_axi_rlast_i;

    assign cmd_error_active = cmd_error && !control_start;
    assign gemm_terminal_mask = control_start || cmd_busy || cmd_valid;
    assign gemm_done_active = gemm_done && !gemm_terminal_mask;
    assign gemm_error_active = gemm_error && !gemm_terminal_mask;
    assign backend_error = cmd_error_active || gemm_error_active;
    assign backend_error_code = cmd_error_active ? cmd_error_code : gemm_error_code;
    assign backend_clear_perf = cmd_valid && cmd_ready && cmd_clear_perf_on_start;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            read_owner_q <= READ_OWNER_NONE;
            active_irq_on_done_q <= 1'b0;
            active_irq_on_error_q <= 1'b0;
        end else begin
            if (control_soft_reset) begin
                read_owner_q <= READ_OWNER_NONE;
                active_irq_on_done_q <= 1'b0;
                active_irq_on_error_q <= 1'b0;
            end else begin
                if ((read_owner_q == READ_OWNER_NONE) && read_fire) begin
                    read_owner_q <= read_select_cmd ? READ_OWNER_CMD : READ_OWNER_GEMM;
                end else if (read_last_fire) begin
                    read_owner_q <= READ_OWNER_NONE;
                end

                if (cmd_valid && cmd_ready) begin
                    active_irq_on_done_q <= cmd_irq_on_done;
                    active_irq_on_error_q <= cmd_irq_on_error;
                end
            end
        end
    end

    npu_control_regs #(
        .ADDR_W(AXIL_ADDR_W),
        .DATA_W(AXIL_DATA_W)
    ) u_control_regs (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .s_axil_awaddr_i(s_axil_awaddr_i),
        .s_axil_awvalid_i(s_axil_awvalid_i),
        .s_axil_awready_o(s_axil_awready_o),
        .s_axil_wdata_i(s_axil_wdata_i),
        .s_axil_wstrb_i(s_axil_wstrb_i),
        .s_axil_wvalid_i(s_axil_wvalid_i),
        .s_axil_wready_o(s_axil_wready_o),
        .s_axil_bresp_o(s_axil_bresp_o),
        .s_axil_bvalid_o(s_axil_bvalid_o),
        .s_axil_bready_i(s_axil_bready_i),
        .s_axil_araddr_i(s_axil_araddr_i),
        .s_axil_arvalid_i(s_axil_arvalid_i),
        .s_axil_arready_o(s_axil_arready_o),
        .s_axil_rdata_o(s_axil_rdata_o),
        .s_axil_rresp_o(s_axil_rresp_o),
        .s_axil_rvalid_o(s_axil_rvalid_o),
        .s_axil_rready_i(s_axil_rready_i),
        .backend_done_i(gemm_done_active),
        .backend_error_i(backend_error),
        .backend_error_code_i(backend_error_code),
        .backend_busy_i(cmd_busy || gemm_busy),
        .backend_clear_perf_i(backend_clear_perf),
        .backend_irq_on_done_i(active_irq_on_done_q),
        .backend_irq_on_error_i(cmd_error_active ? cmd_irq_on_error : active_irq_on_error_q),
        .command_start_o(control_start),
        .command_desc_addr_o(control_desc_addr),
        .soft_reset_o(control_soft_reset),
        .clear_perf_o(control_clear_perf),
        .irq_o(irq_o)
    );

    npu_command_processor #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W)
    ) u_command_processor (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(control_soft_reset),
        .start_i(control_start),
        .desc_addr_i(control_desc_addr),
        .busy_o(cmd_busy),
        .done_o(cmd_done_unused),
        .error_o(cmd_error),
        .error_code_o(cmd_error_code),
        .irq_on_done_o(cmd_irq_on_done),
        .irq_on_error_o(cmd_irq_on_error),
        .clear_perf_on_start_o(cmd_clear_perf_on_start),
        .m_axi_araddr_o(cmd_araddr),
        .m_axi_arlen_o(cmd_arlen),
        .m_axi_arsize_o(cmd_arsize),
        .m_axi_arburst_o(cmd_arburst),
        .m_axi_arvalid_o(cmd_arvalid),
        .m_axi_arready_i(cmd_arready),
        .m_axi_rdata_i(shared_rdata),
        .m_axi_rresp_i(m_axi_rresp_i),
        .m_axi_rlast_i(m_axi_rlast_i),
        .m_axi_rvalid_i(cmd_rvalid),
        .m_axi_rready_o(cmd_rready),
        .command_valid_o(cmd_valid),
        .command_ready_i(cmd_ready),
        .command_m_o(cmd_m),
        .command_n_o(cmd_n),
        .command_k_o(cmd_k),
        .command_a_addr_o(cmd_a_addr),
        .command_b_addr_o(cmd_b_addr),
        .command_c_addr_o(cmd_c_addr),
        .command_a_stride_o(cmd_a_stride),
        .command_b_stride_o(cmd_b_stride),
        .command_c_stride_o(cmd_c_stride)
    );

    npu_gemm_accelerator #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W)
    ) u_gemm_accelerator (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(control_soft_reset),
        .clear_perf_i(control_clear_perf),
        .command_valid_i(cmd_valid),
        .command_ready_o(cmd_ready),
        .command_m_i(cmd_m),
        .command_n_i(cmd_n),
        .command_k_i(cmd_k),
        .command_a_addr_i(cmd_a_addr),
        .command_b_addr_i(cmd_b_addr),
        .command_c_addr_i(cmd_c_addr),
        .command_a_stride_i(cmd_a_stride),
        .command_b_stride_i(cmd_b_stride),
        .command_c_stride_i(cmd_c_stride),
        .command_irq_on_done_i(cmd_irq_on_done),
        .command_irq_on_error_i(cmd_irq_on_error),
        .command_clear_perf_on_start_i(cmd_clear_perf_on_start),
        .busy_o(gemm_busy),
        .done_o(gemm_done),
        .error_o(gemm_error),
        .error_code_o(gemm_error_code),
        .irq_o(gemm_irq_unused),
        .stage_o(stage_o),
        .perf_cycles_o(gemm_perf_cycles_unused),
        .perf_busy_cycles_o(gemm_perf_busy_unused),
        .perf_desc_count_o(gemm_perf_desc_unused),
        .perf_error_count_o(gemm_perf_error_unused),
        .m_axi_araddr_o(gemm_araddr),
        .m_axi_arlen_o(gemm_arlen),
        .m_axi_arsize_o(gemm_arsize),
        .m_axi_arburst_o(gemm_arburst),
        .m_axi_arvalid_o(gemm_arvalid),
        .m_axi_arready_i(gemm_arready),
        .m_axi_rdata_lo_i(m_axi_rdata_lo_i),
        .m_axi_rdata_hi_i(m_axi_rdata_hi_i),
        .m_axi_rresp_i(m_axi_rresp_i),
        .m_axi_rlast_i(m_axi_rlast_i),
        .m_axi_rvalid_i(gemm_rvalid),
        .m_axi_rready_o(gemm_rready),
        .m_axi_awaddr_o(m_axi_awaddr_o),
        .m_axi_awlen_o(m_axi_awlen_o),
        .m_axi_awsize_o(m_axi_awsize_o),
        .m_axi_awburst_o(m_axi_awburst_o),
        .m_axi_awvalid_o(m_axi_awvalid_o),
        .m_axi_awready_i(m_axi_awready_i),
        .m_axi_wdata_lo_o(m_axi_wdata_lo_o),
        .m_axi_wdata_hi_o(m_axi_wdata_hi_o),
        .m_axi_wstrb_o(m_axi_wstrb_o),
        .m_axi_wlast_o(m_axi_wlast_o),
        .m_axi_wvalid_o(m_axi_wvalid_o),
        .m_axi_wready_i(m_axi_wready_i),
        .m_axi_bresp_i(m_axi_bresp_i),
        .m_axi_bvalid_i(m_axi_bvalid_i),
        .m_axi_bready_o(m_axi_bready_o)
    );

endmodule
