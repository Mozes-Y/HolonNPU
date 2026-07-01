module npu_control_regs_test_wrapper #(
    parameter int unsigned ADDR_W = 12,
    parameter int unsigned DATA_W = 32,
    localparam int unsigned STRB_W = DATA_W / 8
) (
    input  logic                  clk_i,
    input  logic                  rst_ni,

    input  logic [ADDR_W-1:0]     s_axil_awaddr_i,
    input  logic                  s_axil_awvalid_i,
    output logic                  s_axil_awready_o,
    input  logic [DATA_W-1:0]     s_axil_wdata_i,
    input  logic [STRB_W-1:0]     s_axil_wstrb_i,
    input  logic                  s_axil_wvalid_i,
    output logic                  s_axil_wready_o,
    output logic [1:0]            s_axil_bresp_o,
    output logic                  s_axil_bvalid_o,
    input  logic                  s_axil_bready_i,

    input  logic [ADDR_W-1:0]     s_axil_araddr_i,
    input  logic                  s_axil_arvalid_i,
    output logic                  s_axil_arready_o,
    output logic [DATA_W-1:0]     s_axil_rdata_o,
    output logic [1:0]            s_axil_rresp_o,
    output logic                  s_axil_rvalid_o,
    input  logic                  s_axil_rready_i,

    input  logic                  backend_done_i,
    input  logic                  backend_error_i,
    input  logic [31:0]           backend_error_code_i,
    input  logic                  backend_busy_i,
    input  logic                  backend_clear_perf_i,
    input  logic                  backend_irq_on_done_i,
    input  logic                  backend_irq_on_error_i,

    output logic                  command_start_o,
    output logic [63:0]           command_desc_addr_o,
    output logic                  soft_reset_o,
    output logic                  clear_perf_o,
    output logic                  irq_o
);

    npu_axi_lite_if #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) axil_if (
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

    npu_control_regs_core #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) u_core (
        .s_axil(axil_if),
        .backend_done_i(backend_done_i),
        .backend_error_i(backend_error_i),
        .backend_error_code_i(backend_error_code_i),
        .backend_busy_i(backend_busy_i),
        .backend_clear_perf_i(backend_clear_perf_i),
        .backend_irq_on_done_i(backend_irq_on_done_i),
        .backend_irq_on_error_i(backend_irq_on_error_i),
        .command_start_o(command_start_o),
        .command_desc_addr_o(command_desc_addr_o),
        .soft_reset_o(soft_reset_o),
        .clear_perf_o(clear_perf_o),
        .irq_o(irq_o)
    );

endmodule
