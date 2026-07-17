/* verilator lint_off DECLFILENAME */

module npu_v2_top #(
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

    output logic [AXI_ADDR_W-1:0]      m_axi_araddr_o,
    output logic [7:0]                 m_axi_arlen_o,
    output logic [2:0]                 m_axi_arsize_o,
    output logic [1:0]                 m_axi_arburst_o,
    output logic                       m_axi_arvalid_o,
    input  logic                       m_axi_arready_i,
    input  logic [AXI_DATA_W-1:0]      m_axi_rdata_i,
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
    output logic [AXI_DATA_W-1:0]      m_axi_wdata_o,
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
        .DATA_W(AXI_DATA_W),
        .ID_W(1)
    ) axi_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    initial begin
        if (AXIL_DATA_W != 32) $fatal("HolonNPU V2 control plane requires 32-bit AXI-Lite data");
        if (AXI_ADDR_W != 64) $fatal("HolonNPU V2 top requires 64-bit AXI addresses");
        if (AXI_DATA_W != 128) $fatal("HolonNPU V2 top requires 128-bit AXI data");
    end

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
    assign axi_if.rdata = m_axi_rdata_i;
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
    assign m_axi_wdata_o = axi_if.wdata;
    assign m_axi_wstrb_o = axi_if.wstrb;
    assign m_axi_wlast_o = axi_if.wlast;
    assign m_axi_wvalid_o = axi_if.wvalid;
    assign axi_if.wready = m_axi_wready_i;
    assign axi_if.bid = '0;
    assign axi_if.bresp = m_axi_bresp_i;
    assign axi_if.bvalid = m_axi_bvalid_i;
    assign m_axi_bready_o = axi_if.bready;

    /* verilator lint_off PINCONNECTEMPTY */
    npu_v2_frontend_tile_core #(
        .AXI_ADDR_W(AXI_ADDR_W),
        .AXI_DATA_W(AXI_DATA_W)
    ) u_core (
        .s_axil(axil_if),
        .m_axi(axi_if),
        .data_rd_valid_i(1'b0),
        .data_rd_ready_o(),
        .data_rd_addr_i(32'h0000_0000),
        .data_rd_valid_o(),
        .data_rd_data_o(),
        .data_rd_error_o(),
        .irq_o(irq_o),
        .loader_busy_o(),
        .loader_done_o(),
        .loader_fault_o(),
        .loader_fault_code_o(),
        .frontend_running_o(),
        .frontend_halted_o(),
        .frontend_done_o(),
        .frontend_fault_o(),
        .frontend_fault_code_o(),
        .frontend_debug_pc_o(),
        .frontend_instret_o()
    );
    /* verilator lint_on PINCONNECTEMPTY */

endmodule

/* verilator lint_on DECLFILENAME */
