/* verilator lint_off DECLFILENAME */

module npu_local_memory_core #(
    parameter int unsigned PROGRAM_WORDS = 16_384,
    parameter int unsigned DATA_WORDS = 65_536
) (
    input  logic        clk_i,
    input  logic        rst_ni,
    input  logic        soft_reset_i,

    npu_localmem_wr_if.slave program_wr,
    npu_localmem_wr_if.slave data_wr,

    npu_localmem_rd_if.slave program_rd,
    npu_localmem_rd_if.slave data_rd
);

    localparam int unsigned WORD_SHIFT = 2;

    logic [31:0] program_mem_q [PROGRAM_WORDS];
    logic [31:0] data_mem_q [DATA_WORDS];

    logic program_wr_ok;
    logic data_wr_ok;
    logic program_rd_ok;
    logic data_rd_ok;

    assign program_wr.req_ready = 1'b1;
    assign data_wr.req_ready = 1'b1;
    assign program_rd.req_ready = 1'b1;
    assign data_rd.req_ready = 1'b1;

    function automatic logic word_addr_ok(
        input logic [31:0] addr,
        input int unsigned words
    );
        word_addr_ok = (addr[1:0] == 2'b00) && ((addr >> WORD_SHIFT) < 32'(words));
    endfunction

    assign program_wr_ok = word_addr_ok(program_wr.req_addr, PROGRAM_WORDS) &&
                           (program_wr.req_strb == 4'hF);
    assign data_wr_ok = word_addr_ok(data_wr.req_addr, DATA_WORDS) &&
                        (data_wr.req_strb != 4'h0);
    assign program_rd_ok = word_addr_ok(program_rd.req_addr, PROGRAM_WORDS);
    assign data_rd_ok = word_addr_ok(data_rd.req_addr, DATA_WORDS);

    always_ff @(posedge clk_i) begin
        if (program_wr.req_valid && program_wr.req_ready && program_wr_ok) begin
            program_mem_q[program_wr.req_addr >> WORD_SHIFT] <= program_wr.req_data;
        end
        if (data_wr.req_valid && data_wr.req_ready && data_wr_ok) begin
            for (int unsigned byte_index = 0; byte_index < 4; byte_index++) begin
                if (data_wr.req_strb[byte_index]) begin
                    data_mem_q[data_wr.req_addr >> WORD_SHIFT][byte_index * 8 +: 8]
                        <= data_wr.req_data[byte_index * 8 +: 8];
                end
            end
        end
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            program_wr.resp_valid <= 1'b0;
            program_wr.resp_error <= 1'b0;
            data_wr.resp_valid <= 1'b0;
            data_wr.resp_error <= 1'b0;
            program_rd.resp_valid <= 1'b0;
            program_rd.resp_data <= '0;
            program_rd.resp_error <= 1'b0;
            data_rd.resp_valid <= 1'b0;
            data_rd.resp_data <= '0;
            data_rd.resp_error <= 1'b0;
        end else if (soft_reset_i) begin
            program_wr.resp_valid <= 1'b0;
            program_wr.resp_error <= 1'b0;
            data_wr.resp_valid <= 1'b0;
            data_wr.resp_error <= 1'b0;
            program_rd.resp_valid <= 1'b0;
            program_rd.resp_data <= '0;
            program_rd.resp_error <= 1'b0;
            data_rd.resp_valid <= 1'b0;
            data_rd.resp_data <= '0;
            data_rd.resp_error <= 1'b0;
        end else begin
            program_wr.resp_valid <= program_wr.req_valid && program_wr.req_ready;
            program_wr.resp_error <= program_wr.req_valid && program_wr.req_ready &&
                                     !program_wr_ok;
            data_wr.resp_valid <= data_wr.req_valid && data_wr.req_ready;
            data_wr.resp_error <= data_wr.req_valid && data_wr.req_ready && !data_wr_ok;

            program_rd.resp_valid <= program_rd.req_valid && program_rd.req_ready;
            program_rd.resp_error <= program_rd.req_valid && program_rd.req_ready && !program_rd_ok;
            if (program_rd.req_valid && program_rd.req_ready && program_rd_ok) begin
                program_rd.resp_data <= program_mem_q[program_rd.req_addr >> WORD_SHIFT];
            end else begin
                program_rd.resp_data <= '0;
            end

            data_rd.resp_valid <= data_rd.req_valid && data_rd.req_ready;
            data_rd.resp_error <= data_rd.req_valid && data_rd.req_ready && !data_rd_ok;
            if (data_rd.req_valid && data_rd.req_ready && data_rd_ok) begin
                data_rd.resp_data <= data_mem_q[data_rd.req_addr >> WORD_SHIFT];
            end else begin
                data_rd.resp_data <= '0;
            end
        end
    end

     local_memory_program_read_responds: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            program_rd.req_valid && program_rd.req_ready |=> program_rd.resp_valid
    );
     local_memory_data_read_responds: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            data_rd.req_valid && data_rd.req_ready |=> data_rd.resp_valid
    );
     local_memory_program_write_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni)
            program_wr.req_valid && program_wr.req_ready && program_wr_ok
    );
     local_memory_data_write_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni)
            data_wr.req_valid && data_wr.req_ready && data_wr_ok
    );
     local_memory_write_responds: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            (program_wr.req_valid && program_wr.req_ready |=> program_wr.resp_valid) and
            (data_wr.req_valid && data_wr.req_ready |=> data_wr.resp_valid)
    );

endmodule

/* verilator lint_on DECLFILENAME */
