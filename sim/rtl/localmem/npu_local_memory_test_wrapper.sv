module npu_local_memory_test_wrapper (
    input  logic        clk_i,
    input  logic        rst_ni,
    input  logic        soft_reset_i,

    input  logic        program_wr_valid_i,
    output logic        program_wr_ready_o,
    input  logic [31:0] program_wr_addr_i,
    input  logic [31:0] program_wr_data_i,
    output logic        program_wr_error_o,

    input  logic        data_wr_valid_i,
    output logic        data_wr_ready_o,
    input  logic [31:0] data_wr_addr_i,
    input  logic [31:0] data_wr_data_i,
    input  logic [3:0]  data_wr_strb_i,
    output logic        data_wr_error_o,

    input  logic        client_data_wr_valid_i,
    output logic        client_data_wr_ready_o,
    input  logic [31:0] client_data_wr_addr_i,
    input  logic [31:0] client_data_wr_data_i,
    input  logic [3:0]  client_data_wr_strb_i,

    input  logic        program_rd_valid_i,
    output logic        program_rd_ready_o,
    input  logic [31:0] program_rd_addr_i,
    output logic        program_rd_valid_o,
    output logic [31:0] program_rd_data_o,
    output logic        program_rd_error_o,

    input  logic        data_rd_valid_i,
    output logic        data_rd_ready_o,
    input  logic [31:0] data_rd_addr_i,
    output logic        data_rd_valid_o,
    output logic [31:0] data_rd_data_o,
    output logic        data_rd_error_o
);

    npu_localmem_wr_if program_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_localmem_wr_if data_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_localmem_wr_if client_data_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_localmem_wr_if memory_data_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_localmem_rd_if program_rd_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_localmem_rd_if client_data_rd_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_localmem_rd_if memory_data_rd_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    assign program_wr_if.req_valid = program_wr_valid_i;
    assign program_wr_ready_o = program_wr_if.req_ready;
    assign program_wr_if.req_addr = program_wr_addr_i;
    assign program_wr_if.req_data = program_wr_data_i;
    assign program_wr_if.req_strb = 4'hF;
    assign program_wr_error_o = program_wr_if.resp_valid && program_wr_if.resp_error;

    assign data_wr_if.req_valid = data_wr_valid_i;
    assign data_wr_ready_o = data_wr_if.req_ready;
    assign data_wr_if.req_addr = data_wr_addr_i;
    assign data_wr_if.req_data = data_wr_data_i;
    assign data_wr_if.req_strb = data_wr_strb_i;
    assign data_wr_error_o = data_wr_if.resp_valid && data_wr_if.resp_error;

    assign client_data_wr_if.req_valid = client_data_wr_valid_i;
    assign client_data_wr_ready_o = client_data_wr_if.req_ready;
    assign client_data_wr_if.req_addr = client_data_wr_addr_i;
    assign client_data_wr_if.req_data = client_data_wr_data_i;
    assign client_data_wr_if.req_strb = client_data_wr_strb_i;

    assign program_rd_if.req_valid = program_rd_valid_i;
    assign program_rd_ready_o = program_rd_if.req_ready;
    assign program_rd_if.req_addr = program_rd_addr_i;
    assign program_rd_valid_o = program_rd_if.resp_valid;
    assign program_rd_data_o = program_rd_if.resp_data;
    assign program_rd_error_o = program_rd_if.resp_error;

    assign client_data_rd_if.req_valid = data_rd_valid_i;
    assign data_rd_ready_o = client_data_rd_if.req_ready;
    assign client_data_rd_if.req_addr = data_rd_addr_i;
    assign data_rd_valid_o = client_data_rd_if.resp_valid;
    assign data_rd_data_o = client_data_rd_if.resp_data;
    assign data_rd_error_o = client_data_rd_if.resp_error;

    npu_data_port_arbiter_core u_data_arbiter (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(soft_reset_i),
        .loader_wr(data_wr_if),
        .client_wr(client_data_wr_if),
        .memory_wr(memory_data_wr_if),
        .client_rd(client_data_rd_if),
        .memory_rd(memory_data_rd_if)
    );

    npu_local_memory_core u_core (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(soft_reset_i),
        .program_wr(program_wr_if),
        .data_wr(memory_data_wr_if),
        .program_rd(program_rd_if),
        .data_rd(memory_data_rd_if)
    );

endmodule
