/* verilator lint_off DECLFILENAME */

module npu_v2_data_port_arbiter_core (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  logic                  soft_reset_i,

    npu_v2_localmem_wr_if.slave   loader_wr,
    npu_v2_localmem_wr_if.slave   client_wr,
    npu_v2_localmem_wr_if.master  memory_wr,

    npu_v2_localmem_rd_if.slave   host_rd,
    npu_v2_localmem_rd_if.slave   client_rd,
    npu_v2_localmem_rd_if.master  memory_rd
);

    typedef enum logic [1:0] {
        RD_OWNER_NONE,
        RD_OWNER_HOST,
        RD_OWNER_CLIENT
    } rd_owner_e;

    typedef enum logic [1:0] {
        WR_OWNER_NONE,
        WR_OWNER_LOADER,
        WR_OWNER_CLIENT
    } wr_owner_e;

    rd_owner_e pending_resp_owner_q;
    wr_owner_e pending_wr_resp_owner_q;
    logic prefer_client_q;
    logic grant_host;
    logic grant_client;
    logic client_priority;

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

    assign client_priority = client_rd.req_valid &&
                             (!host_rd.req_valid || prefer_client_q);
    assign grant_client = client_priority;
    assign grant_host = host_rd.req_valid && !client_priority;

    assign memory_rd.req_valid = grant_host || grant_client;
    assign memory_rd.req_addr = grant_client ? client_rd.req_addr : host_rd.req_addr;
    assign host_rd.req_ready = grant_host && memory_rd.req_ready;
    assign client_rd.req_ready = grant_client && memory_rd.req_ready;

    assign host_rd.resp_valid = (pending_resp_owner_q == RD_OWNER_HOST) &&
                                memory_rd.resp_valid;
    assign host_rd.resp_data = memory_rd.resp_data;
    assign host_rd.resp_error = (pending_resp_owner_q == RD_OWNER_HOST) &&
                                memory_rd.resp_error;

    assign client_rd.resp_valid = (pending_resp_owner_q == RD_OWNER_CLIENT) &&
                                  memory_rd.resp_valid;
    assign client_rd.resp_data = memory_rd.resp_data;
    assign client_rd.resp_error = (pending_resp_owner_q == RD_OWNER_CLIENT) &&
                                  memory_rd.resp_error;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            pending_resp_owner_q <= RD_OWNER_NONE;
            pending_wr_resp_owner_q <= WR_OWNER_NONE;
            prefer_client_q <= 1'b1;
        end else if (soft_reset_i) begin
            pending_resp_owner_q <= RD_OWNER_NONE;
            pending_wr_resp_owner_q <= WR_OWNER_NONE;
            prefer_client_q <= 1'b1;
        end else begin
            if (memory_wr.req_valid && memory_wr.req_ready) begin
                pending_wr_resp_owner_q <= loader_wr.req_valid
                    ? WR_OWNER_LOADER
                    : WR_OWNER_CLIENT;
            end else if (memory_wr.resp_valid) begin
                pending_wr_resp_owner_q <= WR_OWNER_NONE;
            end

            if (memory_rd.req_valid && memory_rd.req_ready) begin
                pending_resp_owner_q <= grant_client ? RD_OWNER_CLIENT : RD_OWNER_HOST;
                prefer_client_q <= grant_client ? 1'b0 : 1'b1;
            end else if (memory_rd.resp_valid) begin
                pending_resp_owner_q <= RD_OWNER_NONE;
            end
        end
    end

    v2_data_arbiter_one_write_winner: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            loader_wr.req_valid && client_wr.req_valid |-> !client_wr.req_ready
    );
    v2_data_arbiter_write_response_has_owner: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            memory_wr.resp_valid |-> (pending_wr_resp_owner_q != WR_OWNER_NONE)
    );
    v2_data_arbiter_response_has_owner: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            memory_rd.resp_valid |-> (pending_resp_owner_q != RD_OWNER_NONE)
    );
    v2_data_arbiter_client_read_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            client_rd.req_valid && client_rd.req_ready
    );
    v2_data_arbiter_host_read_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            host_rd.req_valid && host_rd.req_ready
    );
    v2_data_arbiter_write_contention_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            loader_wr.req_valid && client_wr.req_valid
    );

endmodule

/* verilator lint_on DECLFILENAME */
