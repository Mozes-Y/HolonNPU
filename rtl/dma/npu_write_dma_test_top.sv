module npu_write_dma_test_top (
    input  logic        clk_i,
    input  logic        rst_ni,

    input  logic        start_i,
    input  logic [63:0] addr_i,
    input  logic [31:0] bytes_i,
    output logic        busy_o,
    output logic        done_o,
    output logic        error_o,
    output logic [31:0] error_code_o,

    output logic [63:0] m_axi_awaddr_o,
    output logic [7:0]  m_axi_awlen_o,
    output logic [2:0]  m_axi_awsize_o,
    output logic [1:0]  m_axi_awburst_o,
    output logic        m_axi_awvalid_o,
    input  logic        m_axi_awready_i,
    output logic [63:0] m_axi_wdata_lo_o,
    output logic [63:0] m_axi_wdata_hi_o,
    output logic [15:0] m_axi_wstrb_o,
    output logic        m_axi_wlast_o,
    output logic        m_axi_wvalid_o,
    input  logic        m_axi_wready_i,
    input  logic [1:0]  m_axi_bresp_i,
    input  logic        m_axi_bvalid_i,
    output logic        m_axi_bready_o,

    input  logic        in_valid_i,
    output logic        in_ready_o,
    input  logic [63:0] in_data_lo_i,
    input  logic [63:0] in_data_hi_i
);

    logic [127:0] in_data;
    logic [127:0] wdata;

    assign in_data = {in_data_hi_i, in_data_lo_i};
    assign m_axi_wdata_lo_o = wdata[63:0];
    assign m_axi_wdata_hi_o = wdata[127:64];

    npu_axi4_write_dma u_write_dma (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .start_i(start_i),
        .addr_i(addr_i),
        .bytes_i(bytes_i),
        .busy_o(busy_o),
        .done_o(done_o),
        .error_o(error_o),
        .error_code_o(error_code_o),
        .m_axi_awaddr_o(m_axi_awaddr_o),
        .m_axi_awlen_o(m_axi_awlen_o),
        .m_axi_awsize_o(m_axi_awsize_o),
        .m_axi_awburst_o(m_axi_awburst_o),
        .m_axi_awvalid_o(m_axi_awvalid_o),
        .m_axi_awready_i(m_axi_awready_i),
        .m_axi_wdata_o(wdata),
        .m_axi_wstrb_o(m_axi_wstrb_o),
        .m_axi_wlast_o(m_axi_wlast_o),
        .m_axi_wvalid_o(m_axi_wvalid_o),
        .m_axi_wready_i(m_axi_wready_i),
        .m_axi_bresp_i(m_axi_bresp_i),
        .m_axi_bvalid_i(m_axi_bvalid_i),
        .m_axi_bready_o(m_axi_bready_o),
        .in_valid_i(in_valid_i),
        .in_ready_o(in_ready_o),
        .in_data_i(in_data)
    );

endmodule
