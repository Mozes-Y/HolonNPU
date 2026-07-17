/* verilator lint_off DECLFILENAME */

module npu_v2_engine_data_arbiter_core (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  logic                  soft_reset_i,

    npu_v2_localmem_wr_if.slave   dma_wr,
    npu_v2_localmem_wr_if.slave   vector_wr,
    npu_v2_localmem_wr_if.slave   matrix_wr,
    npu_v2_localmem_wr_if.slave   scalar_wr,
    npu_v2_localmem_wr_if.master  memory_wr,

    npu_v2_localmem_rd_if.slave   dma_rd,
    npu_v2_localmem_rd_if.slave   vector_rd,
    npu_v2_localmem_rd_if.slave   matrix_rd,
    npu_v2_localmem_rd_if.slave   scalar_rd,
    npu_v2_localmem_rd_if.master  memory_rd
);

    typedef enum logic [2:0] {
        RD_OWNER_NONE,
        RD_OWNER_DMA,
        RD_OWNER_VECTOR,
        RD_OWNER_MATRIX,
        RD_OWNER_SCALAR
    } rd_owner_e;

    typedef enum logic [2:0] {
        WR_OWNER_NONE,
        WR_OWNER_DMA,
        WR_OWNER_VECTOR,
        WR_OWNER_MATRIX,
        WR_OWNER_SCALAR
    } wr_owner_e;

    typedef enum logic [1:0] {
        PRIORITY_DMA,
        PRIORITY_VECTOR,
        PRIORITY_MATRIX,
        PRIORITY_SCALAR
    } priority_e;

    rd_owner_e pending_resp_owner_q;
    wr_owner_e pending_wr_resp_owner_q;
    priority_e write_priority_q;
    priority_e read_priority_q;
    logic grant_dma_wr;
    logic grant_vector_wr;
    logic grant_matrix_wr;
    logic grant_scalar_wr;
    logic grant_dma_rd;
    logic grant_vector_rd;
    logic grant_matrix_rd;
    logic grant_scalar_rd;
    logic read_slot_available;
    logic write_slot_available;
    logic write_fire;
    logic read_fire;

    assign write_slot_available = (pending_wr_resp_owner_q == WR_OWNER_NONE) ||
                                  memory_wr.resp_valid;

    always_comb begin
        grant_dma_wr = 1'b0;
        grant_vector_wr = 1'b0;
        grant_matrix_wr = 1'b0;
        grant_scalar_wr = 1'b0;
        if (write_slot_available) begin
            unique case (write_priority_q)
                PRIORITY_DMA: begin
                    if (dma_wr.req_valid) grant_dma_wr = 1'b1;
                    else if (vector_wr.req_valid) grant_vector_wr = 1'b1;
                    else if (matrix_wr.req_valid) grant_matrix_wr = 1'b1;
                    else if (scalar_wr.req_valid) grant_scalar_wr = 1'b1;
                end
                PRIORITY_VECTOR: begin
                    if (vector_wr.req_valid) grant_vector_wr = 1'b1;
                    else if (matrix_wr.req_valid) grant_matrix_wr = 1'b1;
                    else if (scalar_wr.req_valid) grant_scalar_wr = 1'b1;
                    else if (dma_wr.req_valid) grant_dma_wr = 1'b1;
                end
                PRIORITY_MATRIX: begin
                    if (matrix_wr.req_valid) grant_matrix_wr = 1'b1;
                    else if (scalar_wr.req_valid) grant_scalar_wr = 1'b1;
                    else if (dma_wr.req_valid) grant_dma_wr = 1'b1;
                    else if (vector_wr.req_valid) grant_vector_wr = 1'b1;
                end
                default: begin
                    if (scalar_wr.req_valid) grant_scalar_wr = 1'b1;
                    else if (dma_wr.req_valid) grant_dma_wr = 1'b1;
                    else if (vector_wr.req_valid) grant_vector_wr = 1'b1;
                    else if (matrix_wr.req_valid) grant_matrix_wr = 1'b1;
                end
            endcase
        end
    end
    assign memory_wr.req_valid = grant_dma_wr || grant_vector_wr || grant_matrix_wr || grant_scalar_wr;
    assign memory_wr.req_addr = grant_scalar_wr ? scalar_wr.req_addr :
                                grant_matrix_wr ? matrix_wr.req_addr :
                                grant_vector_wr ? vector_wr.req_addr : dma_wr.req_addr;
    assign memory_wr.req_data = grant_scalar_wr ? scalar_wr.req_data :
                                grant_matrix_wr ? matrix_wr.req_data :
                                grant_vector_wr ? vector_wr.req_data : dma_wr.req_data;
    assign memory_wr.req_strb = grant_scalar_wr ? scalar_wr.req_strb :
                                grant_matrix_wr ? matrix_wr.req_strb :
                                grant_vector_wr ? vector_wr.req_strb : dma_wr.req_strb;
    assign dma_wr.req_ready = grant_dma_wr && memory_wr.req_ready;
    assign vector_wr.req_ready = grant_vector_wr && memory_wr.req_ready;
    assign matrix_wr.req_ready = grant_matrix_wr && memory_wr.req_ready;
    assign scalar_wr.req_ready = grant_scalar_wr && memory_wr.req_ready;
    assign write_fire = memory_wr.req_valid && memory_wr.req_ready;

    assign dma_wr.resp_valid = (pending_wr_resp_owner_q == WR_OWNER_DMA) &&
                               memory_wr.resp_valid;
    assign dma_wr.resp_error = (pending_wr_resp_owner_q == WR_OWNER_DMA) &&
                               memory_wr.resp_error;
    assign vector_wr.resp_valid = (pending_wr_resp_owner_q == WR_OWNER_VECTOR) &&
                                  memory_wr.resp_valid;
    assign vector_wr.resp_error = (pending_wr_resp_owner_q == WR_OWNER_VECTOR) &&
                                  memory_wr.resp_error;
    assign matrix_wr.resp_valid = (pending_wr_resp_owner_q == WR_OWNER_MATRIX) &&
                                  memory_wr.resp_valid;
    assign matrix_wr.resp_error = (pending_wr_resp_owner_q == WR_OWNER_MATRIX) &&
                                  memory_wr.resp_error;
    assign scalar_wr.resp_valid = (pending_wr_resp_owner_q == WR_OWNER_SCALAR) &&
                                  memory_wr.resp_valid;
    assign scalar_wr.resp_error = (pending_wr_resp_owner_q == WR_OWNER_SCALAR) &&
                                  memory_wr.resp_error;

    assign read_slot_available = (pending_resp_owner_q == RD_OWNER_NONE) ||
                                 memory_rd.resp_valid;
    always_comb begin
        grant_dma_rd = 1'b0;
        grant_vector_rd = 1'b0;
        grant_matrix_rd = 1'b0;
        grant_scalar_rd = 1'b0;
        if (read_slot_available) begin
            unique case (read_priority_q)
                PRIORITY_DMA: begin
                    if (dma_rd.req_valid) grant_dma_rd = 1'b1;
                    else if (vector_rd.req_valid) grant_vector_rd = 1'b1;
                    else if (matrix_rd.req_valid) grant_matrix_rd = 1'b1;
                    else if (scalar_rd.req_valid) grant_scalar_rd = 1'b1;
                end
                PRIORITY_VECTOR: begin
                    if (vector_rd.req_valid) grant_vector_rd = 1'b1;
                    else if (matrix_rd.req_valid) grant_matrix_rd = 1'b1;
                    else if (scalar_rd.req_valid) grant_scalar_rd = 1'b1;
                    else if (dma_rd.req_valid) grant_dma_rd = 1'b1;
                end
                PRIORITY_MATRIX: begin
                    if (matrix_rd.req_valid) grant_matrix_rd = 1'b1;
                    else if (scalar_rd.req_valid) grant_scalar_rd = 1'b1;
                    else if (dma_rd.req_valid) grant_dma_rd = 1'b1;
                    else if (vector_rd.req_valid) grant_vector_rd = 1'b1;
                end
                default: begin
                    if (scalar_rd.req_valid) grant_scalar_rd = 1'b1;
                    else if (dma_rd.req_valid) grant_dma_rd = 1'b1;
                    else if (vector_rd.req_valid) grant_vector_rd = 1'b1;
                    else if (matrix_rd.req_valid) grant_matrix_rd = 1'b1;
                end
            endcase
        end
    end
    assign memory_rd.req_valid = grant_dma_rd || grant_vector_rd || grant_matrix_rd || grant_scalar_rd;
    assign memory_rd.req_addr = grant_scalar_rd ? scalar_rd.req_addr :
                                grant_matrix_rd ? matrix_rd.req_addr :
                                grant_vector_rd ? vector_rd.req_addr : dma_rd.req_addr;
    assign dma_rd.req_ready = grant_dma_rd && memory_rd.req_ready;
    assign vector_rd.req_ready = grant_vector_rd && memory_rd.req_ready;
    assign matrix_rd.req_ready = grant_matrix_rd && memory_rd.req_ready;
    assign scalar_rd.req_ready = grant_scalar_rd && memory_rd.req_ready;
    assign read_fire = memory_rd.req_valid && memory_rd.req_ready;

    assign dma_rd.resp_valid = (pending_resp_owner_q == RD_OWNER_DMA) &&
                               memory_rd.resp_valid;
    assign dma_rd.resp_data = memory_rd.resp_data;
    assign dma_rd.resp_error = (pending_resp_owner_q == RD_OWNER_DMA) &&
                               memory_rd.resp_error;
    assign vector_rd.resp_valid = (pending_resp_owner_q == RD_OWNER_VECTOR) &&
                                  memory_rd.resp_valid;
    assign vector_rd.resp_data = memory_rd.resp_data;
    assign vector_rd.resp_error = (pending_resp_owner_q == RD_OWNER_VECTOR) &&
                                  memory_rd.resp_error;
    assign matrix_rd.resp_valid = (pending_resp_owner_q == RD_OWNER_MATRIX) &&
                                  memory_rd.resp_valid;
    assign matrix_rd.resp_data = memory_rd.resp_data;
    assign matrix_rd.resp_error = (pending_resp_owner_q == RD_OWNER_MATRIX) &&
                                  memory_rd.resp_error;
    assign scalar_rd.resp_valid = (pending_resp_owner_q == RD_OWNER_SCALAR) &&
                                  memory_rd.resp_valid;
    assign scalar_rd.resp_data = memory_rd.resp_data;
    assign scalar_rd.resp_error = (pending_resp_owner_q == RD_OWNER_SCALAR) &&
                                  memory_rd.resp_error;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            pending_resp_owner_q <= RD_OWNER_NONE;
            pending_wr_resp_owner_q <= WR_OWNER_NONE;
            write_priority_q <= PRIORITY_DMA;
            read_priority_q <= PRIORITY_DMA;
        end else if (soft_reset_i) begin
            pending_resp_owner_q <= RD_OWNER_NONE;
            pending_wr_resp_owner_q <= WR_OWNER_NONE;
            write_priority_q <= PRIORITY_DMA;
            read_priority_q <= PRIORITY_DMA;
        end else begin
            if (write_fire) begin
                if (grant_dma_wr) begin
                    write_priority_q <= PRIORITY_VECTOR;
                    pending_wr_resp_owner_q <= WR_OWNER_DMA;
                end else if (grant_vector_wr) begin
                    write_priority_q <= PRIORITY_MATRIX;
                    pending_wr_resp_owner_q <= WR_OWNER_VECTOR;
                end else if (grant_matrix_wr) begin
                    write_priority_q <= PRIORITY_SCALAR;
                    pending_wr_resp_owner_q <= WR_OWNER_MATRIX;
                end else begin
                    write_priority_q <= PRIORITY_DMA;
                    pending_wr_resp_owner_q <= WR_OWNER_SCALAR;
                end
            end else if (memory_wr.resp_valid) begin
                pending_wr_resp_owner_q <= WR_OWNER_NONE;
            end

            if (read_fire) begin
                if (grant_dma_rd) begin
                    read_priority_q <= PRIORITY_VECTOR;
                    pending_resp_owner_q <= RD_OWNER_DMA;
                end else if (grant_vector_rd) begin
                    read_priority_q <= PRIORITY_MATRIX;
                    pending_resp_owner_q <= RD_OWNER_VECTOR;
                end else if (grant_matrix_rd) begin
                    read_priority_q <= PRIORITY_SCALAR;
                    pending_resp_owner_q <= RD_OWNER_MATRIX;
                end else begin
                    read_priority_q <= PRIORITY_DMA;
                    pending_resp_owner_q <= RD_OWNER_SCALAR;
                end
            end else if (memory_rd.resp_valid) begin
                pending_resp_owner_q <= RD_OWNER_NONE;
            end
        end
    end

    v2_engine_data_arbiter_single_write_grant: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            $onehot0({dma_wr.req_ready, vector_wr.req_ready,
                      matrix_wr.req_ready, scalar_wr.req_ready})
    );
    v2_engine_data_arbiter_single_read_grant: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            $onehot0({dma_rd.req_ready, vector_rd.req_ready,
                      matrix_rd.req_ready, scalar_rd.req_ready})
    );
    v2_engine_data_arbiter_response_has_owner: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            memory_rd.resp_valid |-> (pending_resp_owner_q != RD_OWNER_NONE)
    );
    v2_engine_data_arbiter_write_response_has_owner: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            memory_wr.resp_valid |-> (pending_wr_resp_owner_q != WR_OWNER_NONE)
    );
    v2_engine_data_arbiter_write_contention_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            (dma_wr.req_valid && vector_wr.req_valid) ||
            (dma_wr.req_valid && matrix_wr.req_valid) ||
            (dma_wr.req_valid && scalar_wr.req_valid) ||
            (vector_wr.req_valid && matrix_wr.req_valid) ||
            (vector_wr.req_valid && scalar_wr.req_valid) ||
            (matrix_wr.req_valid && scalar_wr.req_valid)
    );
    v2_engine_data_arbiter_read_contention_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            (dma_rd.req_valid && vector_rd.req_valid) ||
            (dma_rd.req_valid && matrix_rd.req_valid) ||
            (dma_rd.req_valid && scalar_rd.req_valid) ||
            (vector_rd.req_valid && matrix_rd.req_valid) ||
            (vector_rd.req_valid && scalar_rd.req_valid) ||
            (matrix_rd.req_valid && scalar_rd.req_valid)
    );
    v2_engine_data_arbiter_matrix_access_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            (matrix_wr.req_valid && matrix_wr.req_ready) ||
            (matrix_rd.req_valid && matrix_rd.req_ready)
    );
    v2_engine_data_arbiter_vector_access_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            (vector_wr.req_valid && vector_wr.req_ready) ||
            (vector_rd.req_valid && vector_rd.req_ready)
    );
    v2_engine_data_arbiter_scalar_access_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            (scalar_wr.req_valid && scalar_wr.req_ready) ||
            (scalar_rd.req_valid && scalar_rd.req_ready)
    );

endmodule

/* verilator lint_on DECLFILENAME */
