module npu_v2_control_regs_test_wrapper #(
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

    input  logic                  loader_done_i,
    input  logic                  frontend_done_i,
    input  logic                  frontend_fault_i,
    input  logic [31:0]           frontend_fault_code_i,
    input  logic                  frontend_halted_i,
    input  logic [31:0]           frontend_debug_pc_i,
    input  logic [63:0]           frontend_instret_i,
    input  logic                  irq_on_done_i,
    input  logic                  irq_on_fault_i,
    input  logic                  debug_snapshot_on_fault_i,
    input  logic                  clear_perf_on_start_i,

    output logic                  program_start_o,
    output logic [63:0]           program_desc_addr_o,
    output logic                  soft_reset_o,
    output logic                  halt_request_o,
    output logic                  resume_o,
    output logic                  debug_step_o,
    output logic                  clear_perf_o,
    output logic [63:0]           perf_cycle_o,
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

    npu_v2_control_regs_core #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) u_core (
        .s_axil(axil_if),
        .loader_done_i(loader_done_i),
        .frontend_done_i(frontend_done_i),
        .frontend_fault_i(frontend_fault_i),
        .frontend_fault_code_i(frontend_fault_code_i),
        .frontend_halted_i(frontend_halted_i),
        .frontend_debug_pc_i(frontend_debug_pc_i),
        .frontend_instret_i(frontend_instret_i),
        .irq_on_done_i(irq_on_done_i),
        .irq_on_fault_i(irq_on_fault_i),
        .debug_snapshot_on_fault_i(debug_snapshot_on_fault_i),
        .clear_perf_on_start_i(clear_perf_on_start_i),
        .program_start_o(program_start_o),
        .program_desc_addr_o(program_desc_addr_o),
        .soft_reset_o(soft_reset_o),
        .halt_request_o(halt_request_o),
        .resume_o(resume_o),
        .debug_step_o(debug_step_o),
        .clear_perf_o(clear_perf_o),
        .perf_cycle_o(perf_cycle_o),
        .irq_o(irq_o)
    );

endmodule
