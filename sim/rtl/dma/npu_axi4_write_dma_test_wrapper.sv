module npu_axi4_write_dma_test_wrapper #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128,
    parameter int unsigned LEN_W = 32,
    localparam int unsigned STRB_W = DATA_W / 8
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

    output logic [ADDR_W-1:0]       m_axi_awaddr_o,
    output logic [7:0]              m_axi_awlen_o,
    output logic [2:0]              m_axi_awsize_o,
    output logic [1:0]              m_axi_awburst_o,
    output logic                    m_axi_awvalid_o,
    input  logic                    m_axi_awready_i,
    output logic [DATA_W-1:0]       m_axi_wdata_o,
    output logic [STRB_W-1:0]       m_axi_wstrb_o,
    output logic                    m_axi_wlast_o,
    output logic                    m_axi_wvalid_o,
    input  logic                    m_axi_wready_i,
    input  logic [1:0]              m_axi_bresp_i,
    input  logic                    m_axi_bvalid_i,
    output logic                    m_axi_bready_o,

    input  logic                    in_valid_i,
    output logic                    in_ready_o,
    input  logic [DATA_W-1:0]       in_data_i
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
        .DATA_W(DATA_W)
    ) in_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

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

    assign in_if.valid = in_valid_i;
    assign in_if.data = in_data_i;
    assign in_ready_o = in_if.ready;

    npu_axi4_write_dma_core #(
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
        .in_i(in_if)
    );

endmodule
