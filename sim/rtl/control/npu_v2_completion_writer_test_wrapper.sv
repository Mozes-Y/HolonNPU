module npu_v2_completion_writer_test_wrapper #(
    parameter int unsigned AXI_ADDR_W = 64,
    parameter int unsigned AXI_DATA_W = 128
) (
    input  logic                       clk_i,
    input  logic                       rst_ni,
    input  logic                       soft_reset_i,
    input  logic                       start_i,
    input  logic [AXI_ADDR_W-1:0]      completion_addr_i,
    input  logic                       terminal_fault_i,
    input  logic [31:0]                fault_code_i,
    input  logic [31:0]                debug_pc_i,
    input  logic [63:0]                cycle_count_i,
    input  logic [63:0]                instret_i,
    output logic                       busy_o,
    output logic                       done_o,
    output logic                       fault_o,
    output logic [31:0]                fault_code_o,

    output logic [AXI_ADDR_W-1:0]      m_axi_awaddr_o,
    output logic [7:0]                 m_axi_awlen_o,
    output logic [2:0]                 m_axi_awsize_o,
    output logic [1:0]                 m_axi_awburst_o,
    output logic                       m_axi_awvalid_o,
    input  logic                       m_axi_awready_i,
    output logic [63:0]                m_axi_wdata_lo_o,
    output logic [63:0]                m_axi_wdata_hi_o,
    output logic [(AXI_DATA_W/8)-1:0]  m_axi_wstrb_o,
    output logic                       m_axi_wlast_o,
    output logic                       m_axi_wvalid_o,
    input  logic                       m_axi_wready_i,
    input  logic [1:0]                 m_axi_bresp_i,
    input  logic                       m_axi_bvalid_i,
    output logic                       m_axi_bready_o
);

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W),
        .ID_W(1)
    ) axi_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

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

    npu_v2_completion_writer_core #(
        .AXI_ADDR_W(AXI_ADDR_W),
        .AXI_DATA_W(AXI_DATA_W)
    ) u_core (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(soft_reset_i),
        .start_i(start_i),
        .completion_addr_i(completion_addr_i),
        .terminal_fault_i(terminal_fault_i),
        .fault_code_i(fault_code_i),
        .debug_pc_i(debug_pc_i),
        .cycle_count_i(cycle_count_i),
        .instret_i(instret_i),
        .busy_o(busy_o),
        .done_o(done_o),
        .fault_o(fault_o),
        .fault_code_o(fault_code_o),
        .m_axi(axi_if)
    );

endmodule
