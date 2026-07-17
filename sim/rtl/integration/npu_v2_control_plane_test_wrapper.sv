module npu_v2_control_plane_test_wrapper #(
    parameter int unsigned AXIL_ADDR_W = 12,
    parameter int unsigned AXIL_DATA_W = 32,
    parameter int unsigned AXI_ADDR_W = 64,
    parameter int unsigned AXI_DATA_W = 128
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,

    input  logic [AXIL_ADDR_W-1:0]  s_axil_awaddr_i,
    input  logic                    s_axil_awvalid_i,
    output logic                    s_axil_awready_o,
    input  logic [AXIL_DATA_W-1:0]  s_axil_wdata_i,
    input  logic [(AXIL_DATA_W/8)-1:0] s_axil_wstrb_i,
    input  logic                    s_axil_wvalid_i,
    output logic                    s_axil_wready_o,
    output logic [1:0]              s_axil_bresp_o,
    output logic                    s_axil_bvalid_o,
    input  logic                    s_axil_bready_i,
    input  logic [AXIL_ADDR_W-1:0]  s_axil_araddr_i,
    input  logic                    s_axil_arvalid_i,
    output logic                    s_axil_arready_o,
    output logic [AXIL_DATA_W-1:0]  s_axil_rdata_o,
    output logic [1:0]              s_axil_rresp_o,
    output logic                    s_axil_rvalid_o,
    input  logic                    s_axil_rready_i,

    input  logic                    frontend_done_i,
    input  logic                    frontend_fault_i,
    input  logic [31:0]             frontend_fault_code_i,
    input  logic                    frontend_halted_i,
    input  logic [31:0]             frontend_debug_pc_i,
    input  logic [63:0]             frontend_instret_i,

    output logic                    soft_reset_o,
    output logic                    halt_request_o,
    output logic                    resume_o,
    output logic                    debug_step_o,
    output logic                    clear_perf_o,
    output logic [63:0]             perf_cycle_o,
    output logic                    irq_o,

    output logic [7:0]              program_format_o,
    output logic [15:0]             holon_isa_major_o,
    output logic [15:0]             holon_isa_minor_o,
    output logic [63:0]             required_caps_o,
    output logic [63:0]             required_op_classes_o,
    output logic [63:0]             code_addr_o,
    output logic [31:0]             code_size_bytes_o,
    output logic [31:0]             entry_pc_o,
    output logic [63:0]             arg_addr_o,
    output logic [31:0]             arg_size_bytes_o,
    output logic [31:0]             local_mem_bytes_o,
    output logic [31:0]             program_mem_bytes_o,
    output logic [31:0]             stack_bytes_o,
    output logic [63:0]             completion_addr_o,
    output logic [31:0]             flags_o,

    input  logic                    program_rd_valid_i,
    input  logic [31:0]             program_rd_addr_i,
    output logic                    program_rd_valid_o,
    output logic [31:0]             program_rd_data_o,
    output logic                    program_rd_error_o,

    input  logic                    data_rd_valid_i,
    output logic                    data_rd_ready_o,
    input  logic [31:0]             data_rd_addr_i,
    output logic                    data_rd_valid_o,
    output logic [31:0]             data_rd_data_o,
    output logic                    data_rd_error_o,

    output logic                    loader_busy_o,
    output logic                    loader_done_o,
    output logic                    loader_fault_o,
    output logic [31:0]             loader_fault_code_o,

    output logic [AXI_ADDR_W-1:0]   m_axi_araddr_o,
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
    output logic                    m_axi_rready_o
);

    npu_axi_lite_if #(
        .ADDR_W(AXIL_ADDR_W),
        .DATA_W(AXIL_DATA_W)
    ) axil_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W),
        .ID_W(1)
    ) axi_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    npu_v2_localmem_wr_if data_client_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_v2_localmem_rd_if data_client_rd_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    assign data_client_wr_if.req_valid = 1'b0;
    assign data_client_wr_if.req_addr = 32'h0000_0000;
    assign data_client_wr_if.req_data = 32'h0000_0000;
    assign data_client_wr_if.req_strb = 4'h0;
    assign data_client_rd_if.req_valid = 1'b0;
    assign data_client_rd_if.req_addr = 32'h0000_0000;

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

    npu_v2_control_plane_core #(
        .AXI_ADDR_W(AXI_ADDR_W),
        .AXI_DATA_W(AXI_DATA_W)
    ) u_core (
        .s_axil(axil_if),
        .frontend_done_i(frontend_done_i),
        .frontend_fault_i(frontend_fault_i),
        .frontend_fault_code_i(frontend_fault_code_i),
        .frontend_halted_i(frontend_halted_i),
        .frontend_debug_pc_i(frontend_debug_pc_i),
        .frontend_instret_i(frontend_instret_i),
        .soft_reset_o(soft_reset_o),
        .halt_request_o(halt_request_o),
        .resume_o(resume_o),
        .debug_step_o(debug_step_o),
        .clear_perf_o(clear_perf_o),
        .perf_cycle_o(perf_cycle_o),
        .irq_o(irq_o),
        .program_format_o(program_format_o),
        .holon_isa_major_o(holon_isa_major_o),
        .holon_isa_minor_o(holon_isa_minor_o),
        .required_caps_o(required_caps_o),
        .required_op_classes_o(required_op_classes_o),
        .code_addr_o(code_addr_o),
        .code_size_bytes_o(code_size_bytes_o),
        .entry_pc_o(entry_pc_o),
        .arg_addr_o(arg_addr_o),
        .arg_size_bytes_o(arg_size_bytes_o),
        .local_mem_bytes_o(local_mem_bytes_o),
        .program_mem_bytes_o(program_mem_bytes_o),
        .stack_bytes_o(stack_bytes_o),
        .completion_addr_o(completion_addr_o),
        .flags_o(flags_o),
        .program_rd_valid_i(program_rd_valid_i),
        .program_rd_addr_i(program_rd_addr_i),
        .program_rd_valid_o(program_rd_valid_o),
        .program_rd_data_o(program_rd_data_o),
        .program_rd_error_o(program_rd_error_o),
        .data_rd_valid_i(data_rd_valid_i),
        .data_rd_ready_o(data_rd_ready_o),
        .data_rd_addr_i(data_rd_addr_i),
        .data_rd_valid_o(data_rd_valid_o),
        .data_rd_data_o(data_rd_data_o),
        .data_rd_error_o(data_rd_error_o),
        .data_client_wr(data_client_wr_if),
        .data_client_rd(data_client_rd_if),
        .loader_busy_o(loader_busy_o),
        .loader_done_o(loader_done_o),
        .loader_fault_o(loader_fault_o),
        .loader_fault_code_o(loader_fault_code_o),
        .m_axi(axi_if)
    );

endmodule
