/* verilator lint_off DECLFILENAME */
`include "npu_assert.svh"

module npu_gemm_accelerator_core #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic                    soft_reset_i,
    input  logic                    clear_perf_i,

    npu_vr_if.sink                  command_i,

    output logic                    busy_o,
    output logic                    done_o,
    output logic                    error_o,
    output logic [31:0]             error_code_o,
    output logic                    irq_o,
    output logic [3:0]              stage_o,
    output logic [63:0]             perf_cycles_o,
    output logic [63:0]             perf_busy_cycles_o,
    output logic [31:0]             perf_desc_count_o,
    output logic [31:0]             perf_error_count_o,

    npu_axi4_if.read_master         m_axi_read,
    npu_axi4_if.write_master        m_axi_write
);

    import npu_pkg::*;

    localparam int unsigned TILE_M = 16;
    localparam int unsigned TILE_N = 16;
    localparam int unsigned TILE_K = 16;
    localparam int unsigned INPUT_W = 8;
    localparam int unsigned ACC_W = 32;
    localparam int unsigned SYSTOLIC_CYCLES = TILE_K + TILE_M + TILE_N - 1;
    localparam logic [31:0] DMA_BEAT_BYTES = 32'd16;

    localparam logic [3:0] STAGE_IDLE    = 4'd0;
    localparam logic [3:0] STAGE_LOAD_A  = 4'd1;
    localparam logic [3:0] STAGE_LOAD_B  = 4'd2;
    localparam logic [3:0] STAGE_COMPUTE = 4'd3;
    localparam logic [3:0] STAGE_STORE   = 4'd4;
    localparam logic [3:0] STAGE_DONE    = 4'd5;
    localparam logic [3:0] STAGE_ERROR   = 4'd6;

    typedef enum logic [3:0] {
        STATE_IDLE        = 4'd0,
        STATE_TILE_CLEAR  = 4'd1,
        STATE_LOAD_A_PREP = 4'd2,
        STATE_LOAD_A_WAIT = 4'd3,
        STATE_LOAD_B_PREP = 4'd4,
        STATE_LOAD_B_WAIT = 4'd5,
        STATE_COMPUTE     = 4'd6,
        STATE_K_ADVANCE   = 4'd7,
        STATE_STORE_PREP  = 4'd8,
        STATE_STORE_WAIT  = 4'd9,
        STATE_NEXT_TILE   = 4'd10,
        STATE_DONE        = 4'd11,
        STATE_ERR         = 4'd12
    } state_e;

    state_e state_q;

    logic [31:0] m_q;
    logic [31:0] n_q;
    logic [31:0] k_q;
    logic [ADDR_W-1:0] a_addr_q;
    logic [ADDR_W-1:0] b_addr_q;
    logic [ADDR_W-1:0] c_addr_q;
    logic [31:0] a_stride_q;
    logic [31:0] b_stride_q;
    logic [31:0] c_stride_q;
    logic irq_on_done_q;
    logic irq_on_error_q;

    logic [31:0] m_base_q;
    logic [31:0] n_base_q;
    logic [31:0] k_base_q;
    logic [4:0]  load_row_q;
    logic [4:0]  load_k_q;
    logic [5:0]  compute_k_q;
    logic [4:0]  store_row_q;
    logic [2:0]  store_chunk_q;

    logic [31:0] error_code_q;
    logic [63:0] perf_cycles_q;
    logic [63:0] perf_busy_cycles_q;
    logic [31:0] perf_desc_count_q;
    logic [31:0] perf_error_count_q;

    logic signed [INPUT_W-1:0] a_vec [TILE_K];
    logic signed [INPUT_W-1:0] weight_vec [TILE_N];
    logic signed [ACC_W-1:0] psum_zero [TILE_N];
    logic [TILE_K-1:0] k_mask;
    logic [TILE_N-1:0] col_mask;
    logic [TILE_K-1:0] a_valid_vec;
    logic [TILE_N-1:0] psum_valid_vec;
    logic [TILE_N-1:0] c_stream_valid;
    logic signed [ACC_W-1:0] c_stream_data [TILE_N];
    logic signed [ACC_W-1:0] c_accum_q [TILE_M][TILE_N];
    logic [5:0] array_output_cycle_q;
    logic array_output_valid_q;

    logic array_clear;
    logic array_step_valid;

    logic read_start_q;
    logic dma_rst_ni;
    logic [ADDR_W-1:0] read_addr_q;
    logic read_dma_busy;
    logic read_dma_done;
    logic read_dma_error;
    logic [31:0] read_dma_error_code;
    logic read_out_valid;
    logic read_out_ready;
    logic [DATA_W-1:0] read_out_data;
    logic read_out_last;
    logic scratch_a_wr_valid;
    logic scratch_b_wr_valid;

    logic write_start_q;
    logic [ADDR_W-1:0] write_addr_q;
    logic write_dma_busy;
    logic write_dma_done;
    logic write_dma_error;
    logic [31:0] write_dma_error_code;
    logic write_payload_valid_q;
    logic write_in_valid;
    logic write_in_ready;
    logic [DATA_W-1:0] write_in_data;

    logic command_fire;
    logic busy_state;
    logic command_dimension_zero;
    logic command_dimension_unsupported;
    logic command_alignment_error;
    logic [31:0] command_c_stride_min;
    logic [2:0] store_chunk_count;
    logic [31:0] command_m_i;
    logic [31:0] command_n_i;
    logic [31:0] command_k_i;
    logic [ADDR_W-1:0] command_a_addr_i;
    logic [ADDR_W-1:0] command_b_addr_i;
    logic [ADDR_W-1:0] command_c_addr_i;
    logic [31:0] command_a_stride_i;
    logic [31:0] command_b_stride_i;
    logic [31:0] command_c_stride_i;
    logic command_irq_on_done_i;
    logic command_irq_on_error_i;
    logic command_clear_perf_on_start_i;

    assign command_fire = command_i.valid && command_i.ready;
    assign busy_state = (state_q != STATE_IDLE) && (state_q != STATE_DONE) && (state_q != STATE_ERR);

    assign command_i.ready = (state_q == STATE_IDLE) || (state_q == STATE_DONE) || (state_q == STATE_ERR);
    assign busy_o = busy_state || read_dma_busy || write_dma_busy ||
                    (((state_q == STATE_LOAD_A_WAIT) || (state_q == STATE_LOAD_B_WAIT)) && read_dma_done) ||
                    ((state_q == STATE_STORE_WAIT) && write_in_ready);
    assign done_o = (state_q == STATE_DONE) && !command_fire;
    assign error_o = (state_q == STATE_ERR) && !command_fire;
    assign error_code_o = error_code_q;
    assign irq_o = (done_o && irq_on_done_q) || (error_o && irq_on_error_q);
    assign perf_cycles_o = perf_cycles_q;
    assign perf_busy_cycles_o = perf_busy_cycles_q;
    assign perf_desc_count_o = perf_desc_count_q;
    assign perf_error_count_o = perf_error_count_q;

    assign command_irq_on_done_i = command_i.data[NPU_GEMM_CMD_IRQ_ON_DONE_BIT];
    assign command_irq_on_error_i = command_i.data[NPU_GEMM_CMD_IRQ_ON_ERROR_BIT];
    assign command_clear_perf_on_start_i = command_i.data[NPU_GEMM_CMD_CLEAR_PERF_BIT];
    assign command_m_i = command_i.data[NPU_GEMM_CMD_M_LSB +: 32];
    assign command_n_i = command_i.data[NPU_GEMM_CMD_N_LSB +: 32];
    assign command_k_i = command_i.data[NPU_GEMM_CMD_K_LSB +: 32];
    assign command_a_addr_i = command_i.data[NPU_GEMM_CMD_A_ADDR_LSB +: ADDR_W];
    assign command_b_addr_i = command_i.data[NPU_GEMM_CMD_B_ADDR_LSB +: ADDR_W];
    assign command_c_addr_i = command_i.data[NPU_GEMM_CMD_C_ADDR_LSB +: ADDR_W];
    assign command_a_stride_i = command_i.data[NPU_GEMM_CMD_A_STRIDE_LSB +: 32];
    assign command_b_stride_i = command_i.data[NPU_GEMM_CMD_B_STRIDE_LSB +: 32];
    assign command_c_stride_i = command_i.data[NPU_GEMM_CMD_C_STRIDE_LSB +: 32];

    assign read_out_ready = (state_q == STATE_LOAD_A_WAIT) || (state_q == STATE_LOAD_B_WAIT);
    assign dma_rst_ni = rst_ni && !soft_reset_i;
    assign scratch_a_wr_valid = (state_q == STATE_LOAD_A_WAIT) && read_out_valid && read_out_last;
    assign scratch_b_wr_valid = (state_q == STATE_LOAD_B_WAIT) && read_out_valid && read_out_last;

    assign write_in_valid = (state_q == STATE_STORE_WAIT) && write_payload_valid_q;
    assign write_in_data = pack_store_beat(store_row_q, store_chunk_q);
    assign array_clear = (state_q == STATE_TILE_CLEAR);
    assign array_step_valid = (state_q == STATE_COMPUTE);

    assign command_c_stride_min = command_n_i << 2;
    assign command_dimension_zero = (command_m_i == 32'd0) ||
                                    (command_n_i == 32'd0) ||
                                    (command_k_i == 32'd0);
    assign command_dimension_unsupported = (command_m_i > 32'd65535) ||
                                           (command_n_i > 32'd65535) ||
                                           (command_k_i > 32'd65535);
    assign command_alignment_error = (command_a_addr_i[3:0] != 4'h0) ||
                                     (command_b_addr_i[3:0] != 4'h0) ||
                                     (command_c_addr_i[3:0] != 4'h0) ||
                                     (command_a_stride_i[3:0] != 4'h0) ||
                                     (command_b_stride_i[3:0] != 4'h0) ||
                                     (command_c_stride_i[3:0] != 4'h0) ||
                                     (command_a_stride_i < command_k_i) ||
                                     (command_b_stride_i < command_n_i) ||
                                     (command_c_stride_i < command_c_stride_min);

    assign store_chunk_count = active_store_chunks();

    initial begin
        if (NPU_ABI_MAJOR != 2) $fatal("Unexpected NPU_ABI_MAJOR");
        if (NPU_ABI_MINOR != 0) $fatal("Unexpected NPU_ABI_MINOR");
        if (NPU_DESC_SIZE_BYTES != 128) $fatal("Unexpected NPU_DESC_SIZE_BYTES");
        if (NPU_DESC_ALIGN_BYTES != 16) $fatal("Unexpected NPU_DESC_ALIGN_BYTES");
        if (NPU_TENSOR_ALIGN_BYTES != 16) $fatal("Unexpected NPU_TENSOR_ALIGN_BYTES");
        if (NPU_ARRAY_K != 16) $fatal("Unexpected NPU_ARRAY_K");
        if (NPU_ARRAY_N != 16) $fatal("Unexpected NPU_ARRAY_N");
        if (NPU_INPUT_BITS != 8) $fatal("Unexpected NPU_INPUT_BITS");
        if (NPU_ACC_BITS != 32) $fatal("Unexpected NPU_ACC_BITS");
    end

    function automatic logic active_m_index(input logic [4:0] index);
        active_m_index = ((m_base_q + 32'(index)) < m_q);
    endfunction

    function automatic logic active_n_index(input logic [4:0] index);
        active_n_index = ((n_base_q + 32'(index)) < n_q);
    endfunction

    function automatic logic active_k_index(input logic [4:0] index);
        active_k_index = ((k_base_q + 32'(index)) < k_q);
    endfunction

    function automatic logic [ADDR_W-1:0] a_row_addr(input logic [4:0] row_index);
        a_row_addr = a_addr_q +
            ADDR_W'((64'(m_base_q) + 64'(row_index)) * 64'(a_stride_q)) +
            ADDR_W'(k_base_q);
    endfunction

    function automatic logic [ADDR_W-1:0] b_row_addr(input logic [4:0] k_index);
        b_row_addr = b_addr_q +
            ADDR_W'((64'(k_base_q) + 64'(k_index)) * 64'(b_stride_q)) +
            ADDR_W'(n_base_q);
    endfunction

    function automatic logic [ADDR_W-1:0] c_chunk_addr(
        input logic [4:0] row_index,
        input logic [2:0] chunk_index
    );
        c_chunk_addr = c_addr_q +
            ADDR_W'((64'(m_base_q) + 64'(row_index)) * 64'(c_stride_q)) +
            ADDR_W'((64'(n_base_q) + (64'(chunk_index) * 64'd4)) * 64'd4);
    endfunction

    function automatic logic [2:0] active_store_chunks();
        logic [31:0] remaining_cols;
        logic [31:0] active_cols;
        begin
            if (n_base_q >= n_q) begin
                active_store_chunks = 3'd0;
            end else begin
                remaining_cols = n_q - n_base_q;
                active_cols = (remaining_cols > 32'd16) ? 32'd16 : remaining_cols;
                active_store_chunks = 3'((active_cols + 32'd3) >> 2);
            end
        end
    endfunction

    function automatic logic [DATA_W-1:0] pack_store_beat(
        input logic [4:0] row_index,
        input logic [2:0] chunk_index
    );
        int unsigned col_index;
        begin
            pack_store_beat = '0;
            for (int unsigned lane = 0; lane < 4; lane++) begin
                col_index = (int'(chunk_index) * 4) + lane;
                if ((col_index < TILE_N) && active_m_index(row_index) &&
                    ((n_base_q + 32'(col_index)) < n_q)) begin
                    pack_store_beat[(lane * 32) +: 32] = c_accum_q[int'(row_index[3:0])][col_index];
                end
            end
        end
    endfunction

    always_comb begin
        for (int unsigned col = 0; col < TILE_N; col++) begin
            weight_vec[col] = read_out_data[(col * INPUT_W) +: INPUT_W];
        end
    end

    npu_gemm_tile_scratchpad #(
        .TILE_M(TILE_M),
        .TILE_N(TILE_N),
        .TILE_K(TILE_K),
        .INPUT_W(INPUT_W),
        .DATA_W(DATA_W)
    ) u_tile_scratchpad (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .clear_i(array_clear || soft_reset_i),
        .a_wr_valid_i(scratch_a_wr_valid),
        .a_wr_row_i(load_row_q[3:0]),
        .a_wr_data_i(read_out_data),
        .m_i(m_q),
        .n_i(n_q),
        .k_i(k_q),
        .m_base_i(m_base_q),
        .n_base_i(n_base_q),
        .k_base_i(k_base_q),
        .compute_cycle_i(compute_k_q),
        .k_mask_o(k_mask),
        .col_mask_o(col_mask),
        .a_valid_o(a_valid_vec),
        .a_o(a_vec),
        .psum_valid_o(psum_valid_vec),
        .psum_o(psum_zero)
    );

    always_comb begin
        unique case (state_q)
            STATE_IDLE: stage_o = STAGE_IDLE;
            STATE_LOAD_A_PREP,
            STATE_LOAD_A_WAIT: stage_o = STAGE_LOAD_A;
            STATE_LOAD_B_PREP,
            STATE_LOAD_B_WAIT: stage_o = STAGE_LOAD_B;
            STATE_TILE_CLEAR,
            STATE_COMPUTE,
            STATE_K_ADVANCE: stage_o = STAGE_COMPUTE;
            STATE_STORE_PREP,
            STATE_STORE_WAIT,
            STATE_NEXT_TILE: stage_o = STAGE_STORE;
            STATE_DONE: stage_o = STAGE_DONE;
            STATE_ERR: stage_o = STAGE_ERROR;
            default: stage_o = STAGE_ERROR;
        endcase
    end

    npu_vr_if #(
        .DATA_W(DATA_W + 1)
    ) read_out_if (
        .clk_i(clk_i),
        .rst_ni(dma_rst_ni)
    );

    npu_vr_if #(
        .DATA_W(DATA_W)
    ) write_in_if (
        .clk_i(clk_i),
        .rst_ni(dma_rst_ni)
    );

    assign read_out_valid = read_out_if.valid;
    assign read_out_data = read_out_if.data[DATA_W-1:0];
    assign read_out_last = read_out_if.data[DATA_W];
    assign read_out_if.ready = read_out_ready;

    assign write_in_if.valid = write_in_valid;
    assign write_in_if.data = write_in_data;
    assign write_in_ready = write_in_if.ready;

    npu_axi4_read_dma_core #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) u_read_dma (
        .clk_i(clk_i),
        .rst_ni(dma_rst_ni),
        .start_i(read_start_q),
        .addr_i(read_addr_q),
        .bytes_i(DMA_BEAT_BYTES),
        .busy_o(read_dma_busy),
        .done_o(read_dma_done),
        .error_o(read_dma_error),
        .error_code_o(read_dma_error_code),
        .m_axi(m_axi_read),
        .out_o(read_out_if)
    );

    npu_axi4_write_dma_core #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) u_write_dma (
        .clk_i(clk_i),
        .rst_ni(dma_rst_ni),
        .start_i(write_start_q),
        .addr_i(write_addr_q),
        .bytes_i(DMA_BEAT_BYTES),
        .busy_o(write_dma_busy),
        .done_o(write_dma_done),
        .error_o(write_dma_error),
        .error_code_o(write_dma_error_code),
        .m_axi(m_axi_write),
        .in_i(write_in_if)
    );

    npu_systolic_array #(
        .ARRAY_K(TILE_K),
        .ARRAY_N(TILE_N),
        .INPUT_W(INPUT_W),
        .ACC_W(ACC_W)
    ) u_array (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .clear_i(array_clear),
        .weight_valid_i(scratch_b_wr_valid),
        .weight_k_i(load_k_q[3:0]),
        .weight_col_mask_i(col_mask),
        .weight_i(weight_vec),
        .step_valid_i(array_step_valid),
        .k_mask_i(k_mask),
        .col_mask_i(col_mask),
        .a_valid_i(a_valid_vec),
        .a_i(a_vec),
        .psum_valid_i(psum_valid_vec),
        .psum_i(psum_zero),
        .psum_valid_o(c_stream_valid),
        .psum_o(c_stream_data)
    );

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni || soft_reset_i) begin
            state_q <= STATE_IDLE;
            m_q <= 32'd0;
            n_q <= 32'd0;
            k_q <= 32'd0;
            a_addr_q <= '0;
            b_addr_q <= '0;
            c_addr_q <= '0;
            a_stride_q <= 32'd0;
            b_stride_q <= 32'd0;
            c_stride_q <= 32'd0;
            irq_on_done_q <= 1'b0;
            irq_on_error_q <= 1'b0;
            m_base_q <= 32'd0;
            n_base_q <= 32'd0;
            k_base_q <= 32'd0;
            load_row_q <= 5'd0;
            load_k_q <= 5'd0;
            compute_k_q <= 6'd0;
            store_row_q <= 5'd0;
            store_chunk_q <= 3'd0;
            error_code_q <= 32'(NPU_ERR_NONE);
            perf_cycles_q <= 64'd0;
            perf_busy_cycles_q <= 64'd0;
            perf_desc_count_q <= 32'd0;
            perf_error_count_q <= 32'd0;
            read_start_q <= 1'b0;
            read_addr_q <= '0;
            write_start_q <= 1'b0;
            write_addr_q <= '0;
            write_payload_valid_q <= 1'b0;
            array_output_cycle_q <= 6'd0;
            array_output_valid_q <= 1'b0;
            for (int unsigned row = 0; row < TILE_M; row++) begin
                for (int unsigned col = 0; col < TILE_N; col++) begin
                    c_accum_q[row][col] <= '0;
                end
            end
        end else begin
            read_start_q <= 1'b0;
            write_start_q <= 1'b0;

            if (write_in_valid && write_in_ready) begin
                write_payload_valid_q <= 1'b0;
            end

            if (array_output_valid_q) begin
                for (int unsigned col = 0; col < TILE_N; col++) begin
                    int signed out_row;

                    out_row = int'(array_output_cycle_q) - int'(TILE_K - 1) - int'(col);
                    if (c_stream_valid[col] && (out_row >= 0) && (out_row < TILE_M) &&
                        ((m_base_q + 32'(out_row)) < m_q) &&
                        ((n_base_q + 32'(col)) < n_q)) begin
                        c_accum_q[out_row][col] <= c_accum_q[out_row][col] + c_stream_data[col];
                    end
                end
            end

            array_output_cycle_q <= compute_k_q;
            array_output_valid_q <= array_step_valid;

            if (clear_perf_i) begin
                perf_cycles_q <= 64'd0;
                perf_busy_cycles_q <= 64'd0;
                perf_desc_count_q <= 32'd0;
                perf_error_count_q <= 32'd0;
            end else if (busy_state) begin
                perf_cycles_q <= perf_cycles_q + 64'd1;
                perf_busy_cycles_q <= perf_busy_cycles_q + 64'd1;
            end

            unique case (state_q)
                STATE_IDLE,
                STATE_DONE,
                STATE_ERR: begin
                    if (command_fire) begin
                        if (command_clear_perf_on_start_i) begin
                            perf_cycles_q <= 64'd0;
                            perf_busy_cycles_q <= 64'd0;
                            perf_desc_count_q <= 32'd0;
                            perf_error_count_q <= 32'd0;
                        end

                        irq_on_done_q <= command_irq_on_done_i;
                        irq_on_error_q <= command_irq_on_error_i;
                        error_code_q <= 32'(NPU_ERR_NONE);

                        if (command_dimension_zero) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_DIMENSION_ZERO);
                            perf_error_count_q <= command_clear_perf_on_start_i ? 32'd1 : perf_error_count_q + 32'd1;
                        end else if (command_dimension_unsupported) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_DIMENSION_UNSUPPORTED);
                            perf_error_count_q <= command_clear_perf_on_start_i ? 32'd1 : perf_error_count_q + 32'd1;
                        end else if (command_alignment_error) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_UNSUPPORTED_ALIGNMENT);
                            perf_error_count_q <= command_clear_perf_on_start_i ? 32'd1 : perf_error_count_q + 32'd1;
                        end else begin
                            m_q <= command_m_i;
                            n_q <= command_n_i;
                            k_q <= command_k_i;
                            a_addr_q <= command_a_addr_i;
                            b_addr_q <= command_b_addr_i;
                            c_addr_q <= command_c_addr_i;
                            a_stride_q <= command_a_stride_i;
                            b_stride_q <= command_b_stride_i;
                            c_stride_q <= command_c_stride_i;
                            m_base_q <= 32'd0;
                            n_base_q <= 32'd0;
                            k_base_q <= 32'd0;
                            load_row_q <= 5'd0;
                            load_k_q <= 5'd0;
                            compute_k_q <= 6'd0;
                            store_row_q <= 5'd0;
                            store_chunk_q <= 3'd0;
                            state_q <= STATE_TILE_CLEAR;
                        end
                    end
                end

                STATE_TILE_CLEAR: begin
                    k_base_q <= 32'd0;
                    load_row_q <= 5'd0;
                    load_k_q <= 5'd0;
                    compute_k_q <= 6'd0;
                    store_row_q <= 5'd0;
                    store_chunk_q <= 3'd0;
                    array_output_cycle_q <= 6'd0;
                    array_output_valid_q <= 1'b0;
                    for (int unsigned row = 0; row < TILE_M; row++) begin
                        for (int unsigned col = 0; col < TILE_N; col++) begin
                            c_accum_q[row][col] <= '0;
                        end
                    end
                    state_q <= STATE_LOAD_A_PREP;
                end

                STATE_LOAD_A_PREP: begin
                    if (load_row_q == 5'd16) begin
                        load_k_q <= 5'd0;
                        state_q <= STATE_LOAD_B_PREP;
                    end else if (!active_m_index(load_row_q)) begin
                        load_row_q <= load_row_q + 5'd1;
                    end else begin
                        read_addr_q <= a_row_addr(load_row_q);
                        read_start_q <= 1'b1;
                        state_q <= STATE_LOAD_A_WAIT;
                    end
                end

                STATE_LOAD_A_WAIT: begin
                    if (read_dma_error) begin
                        state_q <= STATE_ERR;
                        error_code_q <= read_dma_error_code;
                        perf_error_count_q <= perf_error_count_q + 32'd1;
                    end else if (read_out_valid) begin
                        if (!read_out_last) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_INTERNAL_PROTOCOL);
                            perf_error_count_q <= perf_error_count_q + 32'd1;
                        end else begin
                            load_row_q <= load_row_q + 5'd1;
                            state_q <= STATE_LOAD_A_PREP;
                        end
                    end
                end

                STATE_LOAD_B_PREP: begin
                    if (load_k_q == 5'd16) begin
                        compute_k_q <= 6'd0;
                        state_q <= STATE_COMPUTE;
                    end else if (!active_k_index(load_k_q)) begin
                        load_k_q <= load_k_q + 5'd1;
                    end else begin
                        read_addr_q <= b_row_addr(load_k_q);
                        read_start_q <= 1'b1;
                        state_q <= STATE_LOAD_B_WAIT;
                    end
                end

                STATE_LOAD_B_WAIT: begin
                    if (read_dma_error) begin
                        state_q <= STATE_ERR;
                        error_code_q <= read_dma_error_code;
                        perf_error_count_q <= perf_error_count_q + 32'd1;
                    end else if (read_out_valid) begin
                        if (!read_out_last) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_INTERNAL_PROTOCOL);
                            perf_error_count_q <= perf_error_count_q + 32'd1;
                        end else begin
                            load_k_q <= load_k_q + 5'd1;
                            state_q <= STATE_LOAD_B_PREP;
                        end
                    end
                end

                STATE_COMPUTE: begin
                    if (compute_k_q == 6'(SYSTOLIC_CYCLES - 1)) begin
                        state_q <= STATE_K_ADVANCE;
                    end else begin
                        compute_k_q <= compute_k_q + 6'd1;
                    end
                end

                STATE_K_ADVANCE: begin
                    if ((k_base_q + 32'd16) < k_q) begin
                        k_base_q <= k_base_q + 32'd16;
                        load_row_q <= 5'd0;
                        load_k_q <= 5'd0;
                        compute_k_q <= 6'd0;
                        state_q <= STATE_LOAD_A_PREP;
                    end else begin
                        store_row_q <= 5'd0;
                        store_chunk_q <= 3'd0;
                        state_q <= STATE_STORE_PREP;
                    end
                end

                STATE_STORE_PREP: begin
                    if (store_row_q == 5'd16) begin
                        state_q <= STATE_NEXT_TILE;
                    end else if (!active_m_index(store_row_q)) begin
                        store_row_q <= store_row_q + 5'd1;
                        store_chunk_q <= 3'd0;
                    end else if (store_chunk_q >= store_chunk_count) begin
                        store_row_q <= store_row_q + 5'd1;
                        store_chunk_q <= 3'd0;
                    end else begin
                            write_addr_q <= c_chunk_addr(store_row_q, store_chunk_q);
                            write_start_q <= 1'b1;
                            write_payload_valid_q <= 1'b1;
                            state_q <= STATE_STORE_WAIT;
                        end
                    end

                STATE_STORE_WAIT: begin
                    if (write_dma_error) begin
                        state_q <= STATE_ERR;
                        error_code_q <= write_dma_error_code;
                        perf_error_count_q <= perf_error_count_q + 32'd1;
                        write_payload_valid_q <= 1'b0;
                    end else if (write_dma_done && !write_start_q && !write_payload_valid_q) begin
                        if ((store_chunk_q + 3'd1) >= store_chunk_count) begin
                            store_row_q <= store_row_q + 5'd1;
                            store_chunk_q <= 3'd0;
                        end else begin
                            store_chunk_q <= store_chunk_q + 3'd1;
                        end
                        state_q <= STATE_STORE_PREP;
                    end
                end

                STATE_NEXT_TILE: begin
                    if ((n_base_q + 32'd16) < n_q) begin
                        n_base_q <= n_base_q + 32'd16;
                        k_base_q <= 32'd0;
                        state_q <= STATE_TILE_CLEAR;
                    end else if ((m_base_q + 32'd16) < m_q) begin
                        m_base_q <= m_base_q + 32'd16;
                        n_base_q <= 32'd0;
                        k_base_q <= 32'd0;
                        state_q <= STATE_TILE_CLEAR;
                    end else begin
                        state_q <= STATE_DONE;
                        error_code_q <= 32'(NPU_ERR_NONE);
                        perf_desc_count_q <= perf_desc_count_q + 32'd1;
                    end
                end

                default: begin
                    state_q <= STATE_ERR;
                    error_code_q <= 32'(NPU_ERR_INTERNAL_PROTOCOL);
                    perf_error_count_q <= perf_error_count_q + 32'd1;
                end
            endcase
        end
    end

    `HOLON_NPU_ASSERT(gemm_terminal_states_exclusive,
        @(posedge clk_i) disable iff (!rst_ni)
            !(done_o && error_o))
    `HOLON_NPU_ASSERT(gemm_stage_is_legal,
        @(posedge clk_i) disable iff (!rst_ni)
            (stage_o == STAGE_IDLE) || (stage_o == STAGE_LOAD_A) ||
            (stage_o == STAGE_LOAD_B) || (stage_o == STAGE_COMPUTE) ||
            (stage_o == STAGE_STORE) || (stage_o == STAGE_DONE) ||
            (stage_o == STAGE_ERROR))
    `HOLON_NPU_ASSERT(gemm_tile_counters_in_bounds,
        @(posedge clk_i) disable iff (!rst_ni)
            (load_row_q <= 5'd16) && (load_k_q <= 5'd16) &&
            (compute_k_q <= 6'(SYSTOLIC_CYCLES - 1)) &&
            (store_row_q <= 5'd16) && (store_chunk_q <= 3'd4))
    `HOLON_NPU_ASSERT(gemm_store_stream_only_in_store_wait,
        @(posedge clk_i) disable iff (!rst_ni)
            write_in_valid |-> (state_q == STATE_STORE_WAIT))
    `HOLON_NPU_ASSERT(gemm_no_writeback_before_k_tiles_complete,
        @(posedge clk_i) disable iff (!rst_ni)
            (state_q == STATE_STORE_PREP) |-> ((k_base_q + 32'd16) >= k_q))

endmodule
/* verilator lint_on DECLFILENAME */
