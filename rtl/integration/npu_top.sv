/* verilator lint_off DECLFILENAME */

module npu_top_core #(
    parameter int unsigned AXIL_ADDR_W = 12,
    parameter int unsigned AXIL_DATA_W = 32,
    parameter int unsigned AXI_ADDR_W = 64,
    parameter int unsigned AXI_DATA_W = 128
) (
    input  logic                  clk_i,
    input  logic                  rst_ni,

    npu_axi_lite_if.slave         s_axil,
    npu_axi4_if.master            m_axi,

    output logic                  irq_o,
    output logic [3:0]            stage_o
);

    import npu_pkg::*;

    typedef enum logic [1:0] {
        READ_OWNER_NONE = 2'd0,
        READ_OWNER_CMD  = 2'd1,
        READ_OWNER_GEMM = 2'd2
    } read_owner_e;

    read_owner_e read_owner_q;

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W)
    ) cmd_axi (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W)
    ) gemm_axi (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    npu_vr_if #(
        .DATA_W(NPU_GEMM_CMD_W)
    ) command_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    logic control_start;
    logic [63:0] control_desc_addr;
    logic control_soft_reset;
    logic control_clear_perf;

    /* verilator lint_off UNUSEDSIGNAL */
    logic cmd_done_unused;
    logic cmd_irq_on_done_unused;
    logic cmd_clear_perf_on_start_unused;
    logic gemm_irq_unused;
    logic [63:0] gemm_perf_cycles_unused;
    logic [63:0] gemm_perf_busy_unused;
    logic [31:0] gemm_perf_desc_unused;
    logic [31:0] gemm_perf_error_unused;
    /* verilator lint_on UNUSEDSIGNAL */

    logic cmd_busy;
    logic cmd_error;
    logic [31:0] cmd_error_code;
    logic cmd_irq_on_error;

    logic gemm_busy;
    logic gemm_done;
    logic gemm_error;
    logic [31:0] gemm_error_code;

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

    initial begin
        if (AXI_ADDR_W != 64) $fatal("HolonNPU top requires 64-bit AXI addresses");
        if (AXI_DATA_W != 128) $fatal("HolonNPU top requires 128-bit AXI data");
        if (AXIL_DATA_W != 32) $fatal("HolonNPU control plane requires 32-bit AXI-Lite data");
    end

    assign read_select_cmd = (read_owner_q == READ_OWNER_NONE) && cmd_axi.arvalid;
    assign read_select_gemm = (read_owner_q == READ_OWNER_NONE) &&
                              !cmd_axi.arvalid && gemm_axi.arvalid;

    assign m_axi.arid = read_select_cmd ? cmd_axi.arid : gemm_axi.arid;
    assign m_axi.arvalid = read_select_cmd ? cmd_axi.arvalid :
                           read_select_gemm ? gemm_axi.arvalid : 1'b0;
    assign m_axi.araddr = read_select_cmd ? cmd_axi.araddr : gemm_axi.araddr;
    assign m_axi.arlen = read_select_cmd ? cmd_axi.arlen : gemm_axi.arlen;
    assign m_axi.arsize = read_select_cmd ? cmd_axi.arsize : gemm_axi.arsize;
    assign m_axi.arburst = read_select_cmd ? cmd_axi.arburst : gemm_axi.arburst;
    assign cmd_axi.arready = read_select_cmd && m_axi.arready;
    assign gemm_axi.arready = read_select_gemm && m_axi.arready;

    assign cmd_axi.rid = m_axi.rid;
    assign cmd_axi.rdata = m_axi.rdata;
    assign cmd_axi.rresp = m_axi.rresp;
    assign cmd_axi.rlast = m_axi.rlast;
    assign cmd_axi.rvalid = (read_owner_q == READ_OWNER_CMD) && m_axi.rvalid;

    assign gemm_axi.rid = m_axi.rid;
    assign gemm_axi.rdata = m_axi.rdata;
    assign gemm_axi.rresp = m_axi.rresp;
    assign gemm_axi.rlast = m_axi.rlast;
    assign gemm_axi.rvalid = (read_owner_q == READ_OWNER_GEMM) && m_axi.rvalid;

    assign m_axi.rready = (read_owner_q == READ_OWNER_CMD) ? cmd_axi.rready :
                          (read_owner_q == READ_OWNER_GEMM) ? gemm_axi.rready : 1'b0;
    assign read_fire = m_axi.arvalid && m_axi.arready;
    assign read_last_fire = m_axi.rvalid && m_axi.rready && m_axi.rlast;

    assign m_axi.awid = gemm_axi.awid;
    assign m_axi.awaddr = gemm_axi.awaddr;
    assign m_axi.awlen = gemm_axi.awlen;
    assign m_axi.awsize = gemm_axi.awsize;
    assign m_axi.awburst = gemm_axi.awburst;
    assign m_axi.awvalid = gemm_axi.awvalid;
    assign gemm_axi.awready = m_axi.awready;

    assign m_axi.wdata = gemm_axi.wdata;
    assign m_axi.wstrb = gemm_axi.wstrb;
    assign m_axi.wlast = gemm_axi.wlast;
    assign m_axi.wvalid = gemm_axi.wvalid;
    assign gemm_axi.wready = m_axi.wready;

    assign gemm_axi.bid = m_axi.bid;
    assign gemm_axi.bresp = m_axi.bresp;
    assign gemm_axi.bvalid = m_axi.bvalid;
    assign m_axi.bready = gemm_axi.bready;

    assign cmd_error_active = cmd_error && !control_start;
    assign gemm_terminal_mask = control_start || cmd_busy || command_if.valid;
    assign gemm_done_active = gemm_done && !gemm_terminal_mask;
    assign gemm_error_active = gemm_error && !gemm_terminal_mask;
    assign backend_error = cmd_error_active || gemm_error_active;
    assign backend_error_code = cmd_error_active ? cmd_error_code : gemm_error_code;
    assign backend_clear_perf = command_if.valid && command_if.ready &&
                                command_if.data[NPU_GEMM_CMD_CLEAR_PERF_BIT];

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

                if (command_if.valid && command_if.ready) begin
                    active_irq_on_done_q <= command_if.data[NPU_GEMM_CMD_IRQ_ON_DONE_BIT];
                    active_irq_on_error_q <= command_if.data[NPU_GEMM_CMD_IRQ_ON_ERROR_BIT];
                end
            end
        end
    end

    npu_control_regs_core #(
        .ADDR_W(AXIL_ADDR_W),
        .DATA_W(AXIL_DATA_W)
    ) u_control_regs (
        .s_axil(s_axil),
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
        .irq_on_done_o(cmd_irq_on_done_unused),
        .irq_on_error_o(cmd_irq_on_error),
        .clear_perf_on_start_o(cmd_clear_perf_on_start_unused),
        .m_axi(cmd_axi),
        .command_o(command_if)
    );

    npu_gemm_accelerator_core #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W)
    ) u_gemm_accelerator (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(control_soft_reset),
        .clear_perf_i(control_clear_perf),
        .command_i(command_if),
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
        .m_axi_read(gemm_axi),
        .m_axi_write(gemm_axi)
    );

    top_read_owner_stable_until_rlast: assert property (
        @(posedge clk_i) disable iff (!rst_ni || control_soft_reset)
            (read_owner_q != READ_OWNER_NONE) && !read_last_fire
            |=> read_owner_q == $past(read_owner_q)
    );
    top_read_response_has_single_owner: assert property (
        @(posedge clk_i) disable iff (!rst_ni || control_soft_reset)
            !(cmd_axi.rvalid && gemm_axi.rvalid)
    );
    top_no_unowned_read_response: assert property (
        @(posedge clk_i) disable iff (!rst_ni || control_soft_reset)
            (read_owner_q == READ_OWNER_NONE) |-> !cmd_axi.rvalid && !gemm_axi.rvalid
    );

endmodule
/* verilator lint_on DECLFILENAME */

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

    npu_axi_lite_if #(
        .ADDR_W(AXIL_ADDR_W),
        .DATA_W(AXIL_DATA_W)
    ) axil_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W)
    ) axi_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    assign axil_if.awaddr = s_axil_awaddr_i;
    assign axil_if.awvalid = s_axil_awvalid_i;
    assign s_axil_awready_o = axil_if.awready;
    assign axil_if.wdata = s_axil_wdata_i;
    assign axil_if.wstrb = s_axil_wstrb_i;
    assign axil_if.wvalid = s_axil_wvalid_i;
    assign s_axil_wready_o = axil_if.wready;
    assign s_axil_bresp_o = axil_if.bresp;
    assign s_axil_bvalid_o = axil_if.bvalid;
    assign axil_if.bready = s_axil_bready_i;
    assign axil_if.araddr = s_axil_araddr_i;
    assign axil_if.arvalid = s_axil_arvalid_i;
    assign s_axil_arready_o = axil_if.arready;
    assign s_axil_rdata_o = axil_if.rdata;
    assign s_axil_rresp_o = axil_if.rresp;
    assign s_axil_rvalid_o = axil_if.rvalid;
    assign axil_if.rready = s_axil_rready_i;

    assign m_axi_araddr_o = axi_if.araddr;
    assign m_axi_arlen_o = axi_if.arlen;
    assign m_axi_arsize_o = axi_if.arsize;
    assign m_axi_arburst_o = axi_if.arburst;
    assign m_axi_arvalid_o = axi_if.arvalid;
    assign axi_if.arready = m_axi_arready_i;
    assign axi_if.rid = '0;
    assign axi_if.rdata = {m_axi_rdata_hi_i, m_axi_rdata_lo_i};
    assign axi_if.rresp = m_axi_rresp_i;
    assign axi_if.rlast = m_axi_rlast_i;
    assign axi_if.rvalid = m_axi_rvalid_i;
    assign m_axi_rready_o = axi_if.rready;

    assign m_axi_awaddr_o = axi_if.awaddr;
    assign m_axi_awlen_o = axi_if.awlen;
    assign m_axi_awsize_o = axi_if.awsize;
    assign m_axi_awburst_o = axi_if.awburst;
    assign m_axi_awvalid_o = axi_if.awvalid;
    assign axi_if.awready = m_axi_awready_i;
    assign m_axi_wdata_lo_o = axi_if.wdata[63:0];
    assign m_axi_wdata_hi_o = axi_if.wdata[127:64];
    assign m_axi_wstrb_o = axi_if.wstrb;
    assign m_axi_wlast_o = axi_if.wlast;
    assign m_axi_wvalid_o = axi_if.wvalid;
    assign axi_if.wready = m_axi_wready_i;
    assign axi_if.bid = '0;
    assign axi_if.bresp = m_axi_bresp_i;
    assign axi_if.bvalid = m_axi_bvalid_i;
    assign m_axi_bready_o = axi_if.bready;

    npu_top_core #(
        .AXIL_ADDR_W(AXIL_ADDR_W),
        .AXIL_DATA_W(AXIL_DATA_W),
        .AXI_ADDR_W(AXI_ADDR_W),
        .AXI_DATA_W(AXI_DATA_W)
    ) u_core (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .s_axil(axil_if),
        .m_axi(axi_if),
        .irq_o(irq_o),
        .stage_o(stage_o)
    );

endmodule
