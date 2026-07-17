/* verilator lint_off DECLFILENAME */

module npu_v2_vector_engine_test_wrapper (
    input  logic         clk_i,
    input  logic         rst_ni,
    input  logic         soft_reset_i,
    input  logic [31:0]  local_mem_bytes_i,

    input  logic         issue_valid_i,
    output logic         issue_ready_o,
    input  logic [127:0] issue_data_i,

    output logic         event_valid_o,
    input  logic         event_ready_i,
    output logic [63:0]  event_data_o,

    input  logic         host_wr_valid_i,
    output logic         host_wr_ready_o,
    input  logic [31:0]  host_wr_addr_i,
    input  logic [31:0]  host_wr_data_i,

    input  logic         host_rd_valid_i,
    output logic         host_rd_ready_o,
    input  logic [31:0]  host_rd_addr_i,
    output logic         host_rd_resp_valid_o,
    output logic [31:0]  host_rd_resp_data_o,
    output logic         host_rd_resp_error_o
);

    npu_vr_if #(.DATA_W(128)) issue_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );
    npu_vr_if #(.DATA_W(64)) event_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );
    npu_v2_localmem_wr_if host_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );
    npu_v2_localmem_wr_if vector_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );
    npu_v2_localmem_wr_if memory_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );
    npu_v2_localmem_wr_if program_wr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    npu_v2_localmem_rd_if host_rd_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );
    npu_v2_localmem_rd_if vector_rd_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );
    npu_v2_localmem_rd_if memory_data_rd_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );
    npu_v2_localmem_rd_if program_rd_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    assign issue_if.valid = issue_valid_i;
    assign issue_if.data = issue_data_i;
    assign issue_ready_o = issue_if.ready;

    assign event_valid_o = event_if.valid;
    assign event_if.ready = event_ready_i;
    assign event_data_o = event_if.data;

    assign host_wr_if.req_valid = host_wr_valid_i;
    assign host_wr_if.req_addr = host_wr_addr_i;
    assign host_wr_if.req_data = host_wr_data_i;
    assign host_wr_if.req_strb = 4'hF;
    assign host_wr_ready_o = host_wr_if.req_ready;

    assign host_rd_if.req_valid = host_rd_valid_i;
    assign host_rd_if.req_addr = host_rd_addr_i;
    assign host_rd_ready_o = host_rd_if.req_ready;
    assign host_rd_resp_valid_o = host_rd_if.resp_valid;
    assign host_rd_resp_data_o = host_rd_if.resp_data;
    assign host_rd_resp_error_o = host_rd_if.resp_error;

    assign program_wr_if.req_valid = 1'b0;
    assign program_wr_if.req_addr = 32'd0;
    assign program_wr_if.req_data = 32'd0;
    assign program_wr_if.req_strb = 4'd0;
    assign program_rd_if.req_valid = 1'b0;
    assign program_rd_if.req_addr = 32'd0;

    npu_v2_vector_engine_core vector_engine (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(soft_reset_i),
        .local_mem_bytes_i(local_mem_bytes_i),
        .issue(issue_if.sink),
        .result(event_if.source),
        .data_rd(vector_rd_if.master),
        .data_wr(vector_wr_if.master)
    );

    npu_v2_data_port_arbiter_core data_arbiter (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(soft_reset_i),
        .loader_wr(host_wr_if.slave),
        .client_wr(vector_wr_if.slave),
        .memory_wr(memory_wr_if.master),
        .host_rd(host_rd_if.slave),
        .client_rd(vector_rd_if.slave),
        .memory_rd(memory_data_rd_if.master)
    );

    npu_v2_local_memory_core local_memory (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .soft_reset_i(soft_reset_i),
        .program_wr(program_wr_if.slave),
        .data_wr(memory_wr_if.slave),
        .program_rd(program_rd_if.slave),
        .data_rd(memory_data_rd_if.slave)
    );

    v2_vector_test_wrapper_no_unexpected_write_error: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            !(host_wr_if.resp_valid && host_wr_if.resp_error) &&
            !(vector_wr_if.resp_valid && vector_wr_if.resp_error)
    );

endmodule

/* verilator lint_on DECLFILENAME */
