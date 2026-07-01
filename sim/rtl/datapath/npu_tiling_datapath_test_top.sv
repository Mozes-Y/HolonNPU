module npu_tiling_datapath_test_top #(
    parameter int unsigned TILE_M = 16,
    parameter int unsigned TILE_N = 16,
    parameter int unsigned TILE_K = 16,
    parameter int unsigned DIM_W = 16,
    parameter int unsigned BANKS = 2,
    parameter int unsigned DEPTH = 256,
    localparam int unsigned BANK_W = (BANKS <= 1) ? 1 : $clog2(BANKS),
    localparam int unsigned ADDR_W = (DEPTH <= 1) ? 1 : $clog2(DEPTH),
    localparam int unsigned BANK_SEL_W = BANK_W + 1,
    localparam int unsigned ADDR_SEL_W = ADDR_W + 1
) (
    input  logic                         clk_i,
    input  logic                         rst_ni,

    input  logic [DIM_W-1:0]             m_remaining_i,
    input  logic [DIM_W-1:0]             n_remaining_i,
    input  logic [DIM_W-1:0]             k_remaining_i,
    output logic [TILE_M-1:0]            row_mask_o,
    output logic [TILE_N-1:0]            col_mask_o,
    output logic [TILE_K-1:0]            k_mask_o,

    input  logic                         start_i,
    input  logic                         compute_done_i,
    input  logic                         store_done_i,
    output logic [2:0]                   schedule_state_o,
    output logic                         load_valid_o,
    output logic                         compute_valid_o,
    output logic                         store_valid_o,
    output logic                         done_o,
    output logic                         load_bank_o,
    output logic                         compute_bank_o,
    output logic                         store_bank_o,

    input  logic                         a_wr_valid_i,
    input  logic [BANK_SEL_W-1:0]        a_wr_bank_i,
    input  logic [ADDR_SEL_W-1:0]        a_wr_addr_i,
    input  logic [7:0]                   a_wr_data_i,
    output logic                         a_wr_ready_o,
    output logic                         a_wr_error_o,
    input  logic                         a_rd_valid_i,
    input  logic [BANK_SEL_W-1:0]        a_rd_bank_i,
    input  logic [ADDR_SEL_W-1:0]        a_rd_addr_i,
    output logic                         a_rd_valid_o,
    output logic [7:0]                   a_rd_data_o,
    output logic                         a_rd_error_o,

    input  logic                         b_wr_valid_i,
    input  logic [BANK_SEL_W-1:0]        b_wr_bank_i,
    input  logic [ADDR_SEL_W-1:0]        b_wr_addr_i,
    input  logic [7:0]                   b_wr_data_i,
    output logic                         b_wr_ready_o,
    output logic                         b_wr_error_o,
    input  logic                         b_rd_valid_i,
    input  logic [BANK_SEL_W-1:0]        b_rd_bank_i,
    input  logic [ADDR_SEL_W-1:0]        b_rd_addr_i,
    output logic                         b_rd_valid_o,
    output logic [7:0]                   b_rd_data_o,
    output logic                         b_rd_error_o,

    input  logic                         c_wr_valid_i,
    input  logic [BANK_SEL_W-1:0]        c_wr_bank_i,
    input  logic [ADDR_SEL_W-1:0]        c_wr_addr_i,
    input  logic [31:0]                  c_wr_data_i,
    output logic                         c_wr_ready_o,
    output logic                         c_wr_error_o,
    input  logic                         c_rd_valid_i,
    input  logic [BANK_SEL_W-1:0]        c_rd_bank_i,
    input  logic [ADDR_SEL_W-1:0]        c_rd_addr_i,
    output logic                         c_rd_valid_o,
    output logic [31:0]                  c_rd_data_o,
    output logic                         c_rd_error_o,

    input  logic                         compute_clear_i,
    input  logic                         compute_weight_load_i,
    input  logic [3:0]                   compute_weight_k_i,
    input  logic                         compute_step_valid_i,
    input  logic [5:0]                   compute_k_index_i,
    input  logic [1:0]                   compute_pattern_i,
    input  logic [3:0]                   compute_read_row_i,
    input  logic [3:0]                   compute_read_col_i,
    output logic                         compute_read_valid_o,
    output logic signed [31:0]           compute_read_data_o
);

    logic signed [7:0]      compute_a_vec [TILE_K];
    logic signed [7:0]      compute_weight_vec [TILE_N];
    logic signed [31:0]     compute_psum_zero [TILE_N];
    logic [TILE_K-1:0]      compute_a_valid;
    logic [TILE_N-1:0]      compute_psum_valid;
    logic [TILE_N-1:0]      compute_stream_valid;
    logic signed [31:0]     compute_stream_data [TILE_N];
    logic [TILE_N-1:0]      compute_c_valid [TILE_M];
    logic signed [31:0]     compute_c [TILE_M][TILE_N];
    logic [5:0]             compute_stream_cycle_q;
    logic                   compute_stream_cycle_valid_q;
    logic                   compute_k_active;

    function automatic logic signed [7:0] make_a(
        input int unsigned row,
        input int unsigned k,
        input logic [1:0] pattern
    );
        byte signed value;

        unique case (pattern)
            2'd0: value = byte'(int'((row + (2 * k)) % 7) - 3);
            2'd1: value = byte'(int'(((3 * row) + (5 * k) + 11) % 127) - 63);
            default: value = byte'(int'(((7 * row) + (9 * k) + 5) % 255) - 127);
        endcase

        make_a = value;
    endfunction

    function automatic logic signed [7:0] make_b(
        input int unsigned col,
        input int unsigned k,
        input logic [1:0] pattern
    );
        byte signed value;

        unique case (pattern)
            2'd0: value = byte'(int'(((2 * col) + k) % 5) - 2);
            2'd1: value = byte'(int'(((4 * col) + (7 * k) + 3) % 127) - 63);
            default: value = byte'(int'(((11 * col) + (13 * k) + 17) % 255) - 127);
        endcase

        make_b = value;
    endfunction

    npu_tile_mask #(
        .TILE_M(TILE_M),
        .TILE_N(TILE_N),
        .TILE_K(TILE_K),
        .DIM_W(DIM_W)
    ) u_tile_mask (
        .m_remaining_i(m_remaining_i),
        .n_remaining_i(n_remaining_i),
        .k_remaining_i(k_remaining_i),
        .row_mask_o(row_mask_o),
        .col_mask_o(col_mask_o),
        .k_mask_o(k_mask_o)
    );

    npu_ping_pong_ctrl u_ping_pong (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .start_i(start_i),
        .compute_done_i(compute_done_i),
        .store_done_i(store_done_i),
        .state_o(schedule_state_o),
        .load_valid_o(load_valid_o),
        .compute_valid_o(compute_valid_o),
        .store_valid_o(store_valid_o),
        .done_o(done_o),
        .load_bank_o(load_bank_o),
        .compute_bank_o(compute_bank_o),
        .store_bank_o(store_bank_o)
    );

    npu_i8_tile_buffer #(
        .BANKS(BANKS),
        .DEPTH(DEPTH)
    ) u_a_tile_buffer (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .wr_valid_i(a_wr_valid_i),
        .wr_bank_i(a_wr_bank_i),
        .wr_addr_i(a_wr_addr_i),
        .wr_data_i(a_wr_data_i),
        .wr_ready_o(a_wr_ready_o),
        .wr_error_o(a_wr_error_o),
        .rd_valid_i(a_rd_valid_i),
        .rd_bank_i(a_rd_bank_i),
        .rd_addr_i(a_rd_addr_i),
        .rd_valid_o(a_rd_valid_o),
        .rd_data_o(a_rd_data_o),
        .rd_error_o(a_rd_error_o)
    );

    npu_i8_tile_buffer #(
        .BANKS(BANKS),
        .DEPTH(DEPTH)
    ) u_b_tile_buffer (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .wr_valid_i(b_wr_valid_i),
        .wr_bank_i(b_wr_bank_i),
        .wr_addr_i(b_wr_addr_i),
        .wr_data_i(b_wr_data_i),
        .wr_ready_o(b_wr_ready_o),
        .wr_error_o(b_wr_error_o),
        .rd_valid_i(b_rd_valid_i),
        .rd_bank_i(b_rd_bank_i),
        .rd_addr_i(b_rd_addr_i),
        .rd_valid_o(b_rd_valid_o),
        .rd_data_o(b_rd_data_o),
        .rd_error_o(b_rd_error_o)
    );

    npu_c_accum_buffer #(
        .BANKS(BANKS),
        .DEPTH(DEPTH)
    ) u_c_accum_buffer (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .wr_valid_i(c_wr_valid_i),
        .wr_bank_i(c_wr_bank_i),
        .wr_addr_i(c_wr_addr_i),
        .wr_data_i(c_wr_data_i),
        .wr_ready_o(c_wr_ready_o),
        .wr_error_o(c_wr_error_o),
        .rd_valid_i(c_rd_valid_i),
        .rd_bank_i(c_rd_bank_i),
        .rd_addr_i(c_rd_addr_i),
        .rd_valid_o(c_rd_valid_o),
        .rd_data_o(c_rd_data_o),
        .rd_error_o(c_rd_error_o)
    );

    always_comb begin
        for (int unsigned k_row = 0; k_row < TILE_K; k_row++) begin
            int signed active_m;

            active_m = int'(compute_k_index_i) - int'(k_row);
            compute_a_valid[k_row] = k_mask_o[k_row] && (active_m >= 0) &&
                                     (active_m < TILE_M) && row_mask_o[active_m];
            compute_a_vec[k_row] = compute_a_valid[k_row]
                ? make_a(active_m, k_row, compute_pattern_i)
                : '0;
        end

        for (int unsigned col = 0; col < TILE_N; col++) begin
            int signed active_m;

            compute_weight_vec[col] = make_b(col, int'(compute_weight_k_i), compute_pattern_i);
            active_m = int'(compute_k_index_i) - int'(col);
            compute_psum_valid[col] = col_mask_o[col] && (active_m >= 0) &&
                                      (active_m < TILE_M) && row_mask_o[active_m];
            compute_psum_zero[col] = '0;
        end
    end

    assign compute_k_active = compute_step_valid_i;

    npu_systolic_array #(
        .ARRAY_K(TILE_K),
        .ARRAY_N(TILE_N),
        .INPUT_W(8),
        .ACC_W(32)
    ) u_masked_compute_array (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .clear_i(compute_clear_i),
        .weight_valid_i(compute_weight_load_i),
        .weight_k_i(compute_weight_k_i),
        .weight_col_mask_i(col_mask_o),
        .weight_i(compute_weight_vec),
        .step_valid_i(compute_k_active),
        .k_mask_i(k_mask_o),
        .col_mask_i(col_mask_o),
        .a_valid_i(compute_a_valid),
        .a_i(compute_a_vec),
        .psum_valid_i(compute_psum_valid),
        .psum_i(compute_psum_zero),
        .psum_valid_o(compute_stream_valid),
        .psum_o(compute_stream_data)
    );

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            compute_stream_cycle_q <= '0;
            compute_stream_cycle_valid_q <= 1'b0;
            for (int unsigned row = 0; row < TILE_M; row++) begin
                compute_c_valid[row] <= '0;
                for (int unsigned col = 0; col < TILE_N; col++) begin
                    compute_c[row][col] <= '0;
                end
            end
        end else if (compute_clear_i) begin
            compute_stream_cycle_q <= '0;
            compute_stream_cycle_valid_q <= 1'b0;
            for (int unsigned row = 0; row < TILE_M; row++) begin
                compute_c_valid[row] <= '0;
                for (int unsigned col = 0; col < TILE_N; col++) begin
                    compute_c[row][col] <= '0;
                end
            end
        end else begin
            if (compute_stream_cycle_valid_q) begin
                for (int unsigned col = 0; col < TILE_N; col++) begin
                    int signed out_row;

                    out_row = int'(compute_stream_cycle_q) - int'(TILE_K - 1) - int'(col);
                    if (compute_stream_valid[col] && (out_row >= 0) &&
                        (out_row < TILE_M) && row_mask_o[out_row]) begin
                        compute_c[out_row][col] <= compute_stream_data[col];
                        compute_c_valid[out_row][col] <= 1'b1;
                    end
                end
            end

            compute_stream_cycle_q <= compute_k_index_i;
            compute_stream_cycle_valid_q <= compute_step_valid_i;
        end
    end

    always_comb begin
        compute_read_valid_o = compute_c_valid[compute_read_row_i][compute_read_col_i];
        compute_read_data_o = compute_c[compute_read_row_i][compute_read_col_i];
    end

endmodule
