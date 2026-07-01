module npu_command_processor_test_top (
    input  logic        clk_i,
    input  logic        rst_ni,
    input  logic        soft_reset_i,

    input  logic        start_i,
    input  logic [63:0] desc_addr_i,
    output logic        busy_o,
    output logic        done_o,
    output logic        error_o,
    output logic [31:0] error_code_o,
    output logic        irq_on_done_o,
    output logic        irq_on_error_o,
    output logic        clear_perf_on_start_o,

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

    output logic        command_valid_o,
    input  logic        command_ready_i,
    output logic [31:0] command_m_o,
    output logic [31:0] command_n_o,
    output logic [31:0] command_k_o,
    output logic [63:0] command_a_addr_o,
    output logic [63:0] command_b_addr_o,
    output logic [63:0] command_c_addr_o,
    output logic [31:0] command_a_stride_o,
    output logic [31:0] command_b_stride_o,
    output logic [31:0] command_c_stride_o
);

    import npu_pkg::*;

    logic [127:0] rdata;
    npu_axi4_if #(
        .ADDR_W(64),
        .DATA_W(128),
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

    assign rdata = {m_axi_rdata_hi_i, m_axi_rdata_lo_i};
    assign m_axi_araddr_o = axi_if.araddr;
    assign m_axi_arlen_o = axi_if.arlen;
    assign m_axi_arsize_o = axi_if.arsize;
    assign m_axi_arburst_o = axi_if.arburst;
    assign m_axi_arvalid_o = axi_if.arvalid;
    assign axi_if.arready = m_axi_arready_i;
    assign axi_if.rid = '0;
    assign axi_if.rdata = rdata;
    assign axi_if.rresp = m_axi_rresp_i;
    assign axi_if.rlast = m_axi_rlast_i;
    assign axi_if.rvalid = m_axi_rvalid_i;
    assign m_axi_rready_o = axi_if.rready;

    assign command_valid_o = command_if.valid;
    assign command_if.ready = command_ready_i;
    assign command_m_o = command_if.data[NPU_GEMM_CMD_M_LSB +: 32];
    assign command_n_o = command_if.data[NPU_GEMM_CMD_N_LSB +: 32];
    assign command_k_o = command_if.data[NPU_GEMM_CMD_K_LSB +: 32];
    assign command_a_addr_o = command_if.data[NPU_GEMM_CMD_A_ADDR_LSB +: 64];
    assign command_b_addr_o = command_if.data[NPU_GEMM_CMD_B_ADDR_LSB +: 64];
    assign command_c_addr_o = command_if.data[NPU_GEMM_CMD_C_ADDR_LSB +: 64];
    assign command_a_stride_o = command_if.data[NPU_GEMM_CMD_A_STRIDE_LSB +: 32];
    assign command_b_stride_o = command_if.data[NPU_GEMM_CMD_B_STRIDE_LSB +: 32];
    assign command_c_stride_o = command_if.data[NPU_GEMM_CMD_C_STRIDE_LSB +: 32];

    npu_command_processor u_command_processor (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(soft_reset_i),
        .start_i(start_i),
        .desc_addr_i(desc_addr_i),
        .busy_o(busy_o),
        .done_o(done_o),
        .error_o(error_o),
        .error_code_o(error_code_o),
        .irq_on_done_o(irq_on_done_o),
        .irq_on_error_o(irq_on_error_o),
        .clear_perf_on_start_o(clear_perf_on_start_o),
        .m_axi(axi_if),
        .command_o(command_if)
    );

endmodule
