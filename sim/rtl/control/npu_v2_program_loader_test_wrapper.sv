/* verilator lint_off DECLFILENAME */

module npu_v2_program_loader_test_wrapper #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic                    soft_reset_i,

    input  logic                    start_i,
    input  logic [ADDR_W-1:0]       desc_addr_i,
    output logic                    busy_o,
    output logic                    done_o,
    output logic                    fault_o,
    output logic [31:0]             fault_code_o,

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

    output logic                    program_wr_valid_o,
    input  logic                    program_wr_ready_i,
    output logic [31:0]             program_wr_addr_o,
    output logic [31:0]             program_wr_data_o,

    output logic                    data_wr_valid_o,
    input  logic                    data_wr_ready_i,
    output logic [31:0]             data_wr_addr_o,
    output logic [31:0]             data_wr_data_o,

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
    output logic                    m_axi_rready_o
);

    npu_axi4_if #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W),
        .ID_W(1)
    ) axi_if (
        .aclk_i(clk_i),
        .aresetn_i(rst_ni)
    );

    npu_v2_localmem_wr_if program_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_v2_localmem_wr_if data_wr_if (
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
    assign axi_if.rdata = {m_axi_rdata_hi_i, m_axi_rdata_lo_i};
    assign axi_if.rresp = m_axi_rresp_i;
    assign axi_if.rlast = m_axi_rlast_i;
    assign axi_if.rvalid = m_axi_rvalid_i;
    assign m_axi_rready_o = axi_if.rready;

    assign program_wr_valid_o = program_wr_if.req_valid;
    assign program_wr_addr_o = program_wr_if.req_addr;
    assign program_wr_data_o = program_wr_if.req_data;

    assign data_wr_valid_o = data_wr_if.req_valid;
    assign data_wr_addr_o = data_wr_if.req_addr;
    assign data_wr_data_o = data_wr_if.req_data;

    npu_v2_program_loader_ready_sink_test_wrapper u_program_wr_sink (
        .ready_i(program_wr_ready_i),
        .local_wr(program_wr_if)
    );

    npu_v2_program_loader_ready_sink_test_wrapper u_data_wr_sink (
        .ready_i(data_wr_ready_i),
        .local_wr(data_wr_if)
    );

    npu_v2_program_loader_core #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) u_core (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(soft_reset_i),
        .start_i(start_i),
        .desc_addr_i(desc_addr_i),
        .busy_o(busy_o),
        .done_o(done_o),
        .fault_o(fault_o),
        .fault_code_o(fault_code_o),
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
        .program_wr(program_wr_if),
        .data_wr(data_wr_if),
        .m_axi(axi_if)
    );

endmodule

module npu_v2_program_loader_ready_sink_test_wrapper (
    input logic ready_i,
    npu_v2_localmem_wr_if.slave local_wr
);

    assign local_wr.req_ready = ready_i;
    assign local_wr.resp_error = 1'b0;

    always_ff @(posedge local_wr.clk_i or negedge local_wr.rst_ni) begin
        if (!local_wr.rst_ni) begin
            local_wr.resp_valid <= 1'b0;
        end else begin
            local_wr.resp_valid <= local_wr.req_valid && local_wr.req_ready;
        end
    end

endmodule

/* verilator lint_on DECLFILENAME */
