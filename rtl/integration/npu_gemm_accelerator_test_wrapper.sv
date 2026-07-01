module npu_gemm_accelerator_test_wrapper #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128,
    localparam int unsigned STRB_W = DATA_W / 8
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic                    soft_reset_i,
    input  logic                    clear_perf_i,

    input  logic                    command_valid_i,
    output logic                    command_ready_o,
    input  logic [31:0]             command_m_i,
    input  logic [31:0]             command_n_i,
    input  logic [31:0]             command_k_i,
    input  logic [ADDR_W-1:0]       command_a_addr_i,
    input  logic [ADDR_W-1:0]       command_b_addr_i,
    input  logic [ADDR_W-1:0]       command_c_addr_i,
    input  logic [31:0]             command_a_stride_i,
    input  logic [31:0]             command_b_stride_i,
    input  logic [31:0]             command_c_stride_i,
    input  logic                    command_irq_on_done_i,
    input  logic                    command_irq_on_error_i,
    input  logic                    command_clear_perf_on_start_i,

    output logic                    busy_o,
    output logic                    done_o,
    output logic                    error_o,
    output logic [31:0]             error_code_o,
    output logic                    irq_o,
    output logic [3:0]              stage_o,
    output logic [63:0]             perf_cycles_o,
    output logic [63:0]             perf_busy_cycles_o,
    output logic [31:0]             perf_desc_count_o,
    output logic [31:0]             perf_error_count_o,

    output logic [ADDR_W-1:0]       m_axi_araddr_o,
    output logic [7:0]              m_axi_arlen_o,
    output logic [2:0]              m_axi_arsize_o,
    output logic [1:0]              m_axi_arburst_o,
    output logic                    m_axi_arvalid_o,
    input  logic                    m_axi_arready_i,
    input  logic [63:0]             m_axi_rdata_lo_i,
    input  logic [63:0]             m_axi_rdata_hi_i,
    input  logic [1:0]              m_axi_rresp_i,
    input  logic                    m_axi_rlast_i,
    input  logic                    m_axi_rvalid_i,
    output logic                    m_axi_rready_o,

    output logic [ADDR_W-1:0]       m_axi_awaddr_o,
    output logic [7:0]              m_axi_awlen_o,
    output logic [2:0]              m_axi_awsize_o,
    output logic [1:0]              m_axi_awburst_o,
    output logic                    m_axi_awvalid_o,
    input  logic                    m_axi_awready_i,
    output logic [63:0]             m_axi_wdata_lo_o,
    output logic [63:0]             m_axi_wdata_hi_o,
    output logic [STRB_W-1:0]       m_axi_wstrb_o,
    output logic                    m_axi_wlast_o,
    output logic                    m_axi_wvalid_o,
    input  logic                    m_axi_wready_i,
    input  logic [1:0]              m_axi_bresp_i,
    input  logic                    m_axi_bvalid_i,
    output logic                    m_axi_bready_o
);

    import npu_pkg::*;

    npu_axi4_if #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W),
        .ID_W(1)
    ) axi_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    npu_vr_if #(
        .DATA_W(NPU_GEMM_CMD_W)
    ) command_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    assign command_if.valid = command_valid_i;
    assign command_ready_o = command_if.ready;
    assign command_if.data[NPU_GEMM_CMD_IRQ_ON_DONE_BIT] = command_irq_on_done_i;
    assign command_if.data[NPU_GEMM_CMD_IRQ_ON_ERROR_BIT] = command_irq_on_error_i;
    assign command_if.data[NPU_GEMM_CMD_CLEAR_PERF_BIT] = command_clear_perf_on_start_i;
    assign command_if.data[NPU_GEMM_CMD_M_LSB +: 32] = command_m_i;
    assign command_if.data[NPU_GEMM_CMD_N_LSB +: 32] = command_n_i;
    assign command_if.data[NPU_GEMM_CMD_K_LSB +: 32] = command_k_i;
    assign command_if.data[NPU_GEMM_CMD_A_ADDR_LSB +: ADDR_W] = command_a_addr_i;
    assign command_if.data[NPU_GEMM_CMD_B_ADDR_LSB +: ADDR_W] = command_b_addr_i;
    assign command_if.data[NPU_GEMM_CMD_C_ADDR_LSB +: ADDR_W] = command_c_addr_i;
    assign command_if.data[NPU_GEMM_CMD_A_STRIDE_LSB +: 32] = command_a_stride_i;
    assign command_if.data[NPU_GEMM_CMD_B_STRIDE_LSB +: 32] = command_b_stride_i;
    assign command_if.data[NPU_GEMM_CMD_C_STRIDE_LSB +: 32] = command_c_stride_i;

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

    npu_gemm_accelerator_core #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) u_core (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(soft_reset_i),
        .clear_perf_i(clear_perf_i),
        .command_i(command_if),
        .busy_o(busy_o),
        .done_o(done_o),
        .error_o(error_o),
        .error_code_o(error_code_o),
        .irq_o(irq_o),
        .stage_o(stage_o),
        .perf_cycles_o(perf_cycles_o),
        .perf_busy_cycles_o(perf_busy_cycles_o),
        .perf_desc_count_o(perf_desc_count_o),
        .perf_error_count_o(perf_error_count_o),
        .m_axi_read(axi_if),
        .m_axi_write(axi_if)
    );

endmodule
