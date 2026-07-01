module npu_axi4_read_dma_test_wrapper #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128,
    parameter int unsigned LEN_W = 32
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

    output logic [ADDR_W-1:0]       m_axi_araddr_o,
    output logic [7:0]              m_axi_arlen_o,
    output logic [2:0]              m_axi_arsize_o,
    output logic [1:0]              m_axi_arburst_o,
    output logic                    m_axi_arvalid_o,
    input  logic                    m_axi_arready_i,
    input  logic [DATA_W-1:0]       m_axi_rdata_i,
    input  logic [1:0]              m_axi_rresp_i,
    input  logic                    m_axi_rlast_i,
    input  logic                    m_axi_rvalid_i,
    output logic                    m_axi_rready_o,

    output logic                    out_valid_o,
    input  logic                    out_ready_i,
    output logic [DATA_W-1:0]       out_data_o,
    output logic                    out_last_o
);

    npu_axi4_if #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W),
        .ID_W(1)
    ) axi_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    npu_vr_if #(
        .DATA_W(DATA_W + 1)
    ) out_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

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

    assign out_valid_o = out_if.valid;
    assign out_data_o = out_if.data[DATA_W-1:0];
    assign out_last_o = out_if.data[DATA_W];
    assign out_if.ready = out_ready_i;

    npu_axi4_read_dma_core #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W),
        .LEN_W(LEN_W)
    ) u_core (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .start_i(start_i),
        .addr_i(addr_i),
        .bytes_i(bytes_i),
        .busy_o(busy_o),
        .done_o(done_o),
        .error_o(error_o),
        .error_code_o(error_code_o),
        .m_axi(axi_if),
        .out_o(out_if)
    );

endmodule
