/* verilator lint_off DECLFILENAME */

module npu_data_port_arbiter_core (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  logic                  soft_reset_i,

    npu_localmem_wr_if.slave   loader_wr,
    npu_localmem_wr_if.slave   client_wr,
    npu_localmem_wr_if.master  memory_wr,

    npu_localmem_rd_if.slave   client_rd,
    npu_localmem_rd_if.master  memory_rd
);

    typedef enum logic [1:0] {
        WR_OWNER_NONE,
        WR_OWNER_LOADER,
        WR_OWNER_CLIENT
    } wr_owner_e;

    wr_owner_e pending_wr_resp_owner_q;

    assign memory_wr.req_valid = loader_wr.req_valid || client_wr.req_valid;
    assign memory_wr.req_addr = loader_wr.req_valid ? loader_wr.req_addr : client_wr.req_addr;
    assign memory_wr.req_data = loader_wr.req_valid ? loader_wr.req_data : client_wr.req_data;
    assign memory_wr.req_strb = loader_wr.req_valid ? loader_wr.req_strb : client_wr.req_strb;
    assign loader_wr.req_ready = memory_wr.req_ready;
    assign client_wr.req_ready = !loader_wr.req_valid && memory_wr.req_ready;

    assign loader_wr.resp_valid = (pending_wr_resp_owner_q == WR_OWNER_LOADER) &&
                                  memory_wr.resp_valid;
    assign loader_wr.resp_error = (pending_wr_resp_owner_q == WR_OWNER_LOADER) &&
                                  memory_wr.resp_error;
    assign client_wr.resp_valid = (pending_wr_resp_owner_q == WR_OWNER_CLIENT) &&
                                  memory_wr.resp_valid;
    assign client_wr.resp_error = (pending_wr_resp_owner_q == WR_OWNER_CLIENT) &&
                                  memory_wr.resp_error;

    assign memory_rd.req_valid = client_rd.req_valid;
    assign memory_rd.req_addr = client_rd.req_addr;
    assign client_rd.req_ready = memory_rd.req_ready;
    assign client_rd.resp_valid = memory_rd.resp_valid;
    assign client_rd.resp_data = memory_rd.resp_data;
    assign client_rd.resp_error = memory_rd.resp_error;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            pending_wr_resp_owner_q <= WR_OWNER_NONE;
        end else if (soft_reset_i) begin
            pending_wr_resp_owner_q <= WR_OWNER_NONE;
        end else begin
            if (memory_wr.req_valid && memory_wr.req_ready) begin
                pending_wr_resp_owner_q <= loader_wr.req_valid
                    ? WR_OWNER_LOADER
                    : WR_OWNER_CLIENT;
            end else if (memory_wr.resp_valid) begin
                pending_wr_resp_owner_q <= WR_OWNER_NONE;
            end
        end
    end

     data_arbiter_one_write_winner: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            loader_wr.req_valid && client_wr.req_valid |-> !client_wr.req_ready
    );
     data_arbiter_write_response_has_owner: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            memory_wr.resp_valid |-> (pending_wr_resp_owner_q != WR_OWNER_NONE)
    );
     data_arbiter_client_read_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            client_rd.req_valid && client_rd.req_ready
    );
     data_arbiter_write_contention_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            loader_wr.req_valid && client_wr.req_valid
    );

endmodule

/* verilator lint_on DECLFILENAME */
