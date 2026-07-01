module npu_read_dma_test_top (
    input  logic        clk_i,
    input  logic        rst_ni,

    input  logic        start_i,
    input  logic [63:0] addr_i,
    input  logic [31:0] bytes_i,
    output logic        busy_o,
    output logic        done_o,
    output logic        error_o,
    output logic [31:0] error_code_o,

    output logic [63:0] m_axi_araddr_o,
    output logic [7:0]  m_axi_arlen_o,
    output logic [2:0]  m_axi_arsize_o,
    output logic [1:0]  m_axi_arburst_o,
    output logic        m_axi_arvalid_o,
    input  logic        m_axi_arready_i,
    input  logic [63:0] m_axi_rdata_lo_i,
    input  logic [63:0] m_axi_rdata_hi_i,
    input  logic [1:0]  m_axi_rresp_i,
    input  logic        m_axi_rlast_i,
    input  logic        m_axi_rvalid_i,
    output logic        m_axi_rready_o,

    output logic        out_valid_o,
    input  logic        out_ready_i,
    output logic [63:0] out_data_lo_o,
    output logic [63:0] out_data_hi_o,
    output logic        out_last_o
);

    logic [127:0] rdata;
    logic [127:0] out_data;

    assign rdata = {m_axi_rdata_hi_i, m_axi_rdata_lo_i};
    assign out_data_lo_o = out_data[63:0];
    assign out_data_hi_o = out_data[127:64];

    npu_axi4_read_dma_test_wrapper u_read_dma (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .start_i(start_i),
        .addr_i(addr_i),
        .bytes_i(bytes_i),
        .busy_o(busy_o),
        .done_o(done_o),
        .error_o(error_o),
        .error_code_o(error_code_o),
        .m_axi_araddr_o(m_axi_araddr_o),
        .m_axi_arlen_o(m_axi_arlen_o),
        .m_axi_arsize_o(m_axi_arsize_o),
        .m_axi_arburst_o(m_axi_arburst_o),
        .m_axi_arvalid_o(m_axi_arvalid_o),
        .m_axi_arready_i(m_axi_arready_i),
        .m_axi_rdata_i(rdata),
        .m_axi_rresp_i(m_axi_rresp_i),
        .m_axi_rlast_i(m_axi_rlast_i),
        .m_axi_rvalid_i(m_axi_rvalid_i),
        .m_axi_rready_o(m_axi_rready_o),
        .out_valid_o(out_valid_o),
        .out_ready_i(out_ready_i),
        .out_data_o(out_data),
        .out_last_o(out_last_o)
    );

endmodule
