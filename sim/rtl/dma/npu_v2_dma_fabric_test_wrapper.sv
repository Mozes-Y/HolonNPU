module npu_v2_dma_fabric_test_wrapper #(
    parameter int unsigned AXI_ADDR_W = 64,
    parameter int unsigned AXI_DATA_W = 128
) (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  logic                  soft_reset_i,

    input  logic                  dma_issue_valid_i,
    input  logic                  dma_store_i,
    input  logic [63:0]           dma_system_addr_i,
    input  logic [31:0]           dma_local_addr_i,
    input  logic [27:0]           dma_byte_count_i,
    input  logic [31:0]           local_mem_bytes_i,
    output logic                  dma_issue_ready_o,
    output logic                  dma_event_valid_o,
    output logic                  dma_fault_valid_o,
    output logic [31:0]           dma_fault_code_o,

    output logic                  data_wr_valid_o,
    output logic [31:0]           data_wr_addr_o,
    output logic [31:0]           data_wr_data_o,
    input  logic                  data_wr_ready_i,

    output logic                  data_rd_valid_o,
    output logic [31:0]           data_rd_addr_o,
    input  logic                  data_rd_valid_i,
    input  logic [31:0]           data_rd_data_i,
    input  logic                  data_rd_error_i,

    output logic [AXI_ADDR_W-1:0] m_axi_araddr_o,
    output logic [7:0]            m_axi_arlen_o,
    output logic [2:0]            m_axi_arsize_o,
    output logic [1:0]            m_axi_arburst_o,
    output logic                  m_axi_arvalid_o,
    input  logic                  m_axi_arready_i,

    input  logic [63:0]           m_axi_rdata_lo_i,
    input  logic [63:0]           m_axi_rdata_hi_i,
    input  logic [1:0]            m_axi_rresp_i,
    input  logic                  m_axi_rlast_i,
    input  logic                  m_axi_rvalid_i,
    output logic                  m_axi_rready_o,

    output logic [AXI_ADDR_W-1:0] m_axi_awaddr_o,
    output logic [7:0]            m_axi_awlen_o,
    output logic [2:0]            m_axi_awsize_o,
    output logic [1:0]            m_axi_awburst_o,
    output logic                  m_axi_awvalid_o,
    input  logic                  m_axi_awready_i,

    output logic [63:0]           m_axi_wdata_lo_o,
    output logic [63:0]           m_axi_wdata_hi_o,
    output logic [(AXI_DATA_W/8)-1:0] m_axi_wstrb_o,
    output logic                  m_axi_wlast_o,
    output logic                  m_axi_wvalid_o,
    input  logic                  m_axi_wready_i,

    input  logic [1:0]            m_axi_bresp_i,
    input  logic                  m_axi_bvalid_i,
    output logic                  m_axi_bready_o
);

    npu_frontend_if frontend_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_v2_localmem_wr_if data_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W),
        .ID_W(1)
    ) axi_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    npu_v2_localmem_rd_if data_rd_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    assign frontend_if.soft_reset = soft_reset_i;
    assign frontend_if.start = 1'b0;
    assign frontend_if.halt_request = 1'b0;
    assign frontend_if.resume = 1'b0;
    assign frontend_if.debug_step = 1'b0;
    assign frontend_if.entry_pc = 32'h0000_0000;
    assign frontend_if.program_size_bytes = 32'h0000_0000;
    assign frontend_if.local_mem_bytes = local_mem_bytes_i;
    assign frontend_if.program_rd_resp_valid = 1'b0;
    assign frontend_if.program_rd_resp_data = 32'h0000_0000;
    assign frontend_if.program_rd_resp_error = 1'b0;
    assign frontend_if.running = 1'b0;
    assign frontend_if.halted = 1'b0;
    assign frontend_if.done = 1'b0;
    assign frontend_if.fault = 1'b0;
    assign frontend_if.fault_code = 32'h0000_0000;
    assign frontend_if.debug_pc = 32'h0000_0000;
    assign frontend_if.instret = 64'h0000_0000_0000_0000;
    assign frontend_if.program_rd_valid = 1'b0;
    assign frontend_if.program_rd_addr = 32'h0000_0000;
    assign frontend_if.dma_issue_valid = dma_issue_valid_i;
    assign frontend_if.dma_issue_data = {
        dma_store_i ? 4'h1 : 4'h0,
        dma_byte_count_i,
        dma_local_addr_i,
        dma_system_addr_i
    };
    assign frontend_if.vector_issue_valid = 1'b0;
    assign frontend_if.vector_issue_data = 128'h0;
    assign frontend_if.vector_result_valid = 1'b0;
    assign frontend_if.vector_result_data = 64'h0;
    assign frontend_if.vector_result_ready = 1'b0;
    assign frontend_if.matrix_issue_valid = 1'b0;
    assign frontend_if.matrix_issue_data = 128'h0;
    assign frontend_if.matrix_result_valid = 1'b0;
    assign frontend_if.matrix_result_data = 64'h0;
    assign frontend_if.matrix_result_ready = 1'b0;
    assign frontend_if.sync_issue_valid = 1'b0;
    assign frontend_if.sync_issue_data = 64'h0;
    assign frontend_if.vector_issue_ready = 1'b1;
    assign frontend_if.matrix_issue_ready = 1'b1;
    assign frontend_if.sync_issue_ready = 1'b1;

    assign dma_issue_ready_o = frontend_if.dma_issue_ready;
    assign dma_event_valid_o = frontend_if.dma_event_valid;
    assign dma_fault_valid_o = frontend_if.dma_fault_valid;
    assign dma_fault_code_o = frontend_if.dma_fault_code;

    assign data_wr_valid_o = data_wr_if.req_valid;
    assign data_wr_addr_o = data_wr_if.req_addr;
    assign data_wr_data_o = data_wr_if.req_data;
    assign data_wr_if.req_ready = data_wr_ready_i;
    assign data_wr_if.resp_error = 1'b0;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            data_wr_if.resp_valid <= 1'b0;
        end else if (soft_reset_i) begin
            data_wr_if.resp_valid <= 1'b0;
        end else begin
            data_wr_if.resp_valid <= data_wr_if.req_valid && data_wr_if.req_ready;
        end
    end

    assign data_rd_valid_o = data_rd_if.req_valid;
    assign data_rd_addr_o = data_rd_if.req_addr;
    assign data_rd_if.req_ready = 1'b1;
    assign data_rd_if.resp_valid = data_rd_valid_i;
    assign data_rd_if.resp_data = data_rd_data_i;
    assign data_rd_if.resp_error = data_rd_error_i;

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

    npu_v2_dma_fabric_core #(
        .AXI_ADDR_W(AXI_ADDR_W),
        .AXI_DATA_W(AXI_DATA_W)
    ) u_core (
        .frontend(frontend_if),
        .data_wr(data_wr_if),
        .data_rd(data_rd_if),
        .m_axi(axi_if)
    );

endmodule
