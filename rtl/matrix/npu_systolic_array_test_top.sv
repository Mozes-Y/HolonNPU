module npu_systolic_array_test_top #(
    parameter int unsigned TILE_M = 17,
    parameter int unsigned ARRAY_K = 23,
    parameter int unsigned ARRAY_N = 19,
    parameter int unsigned INPUT_W = 8,
    parameter int unsigned ACC_W = 32,
    parameter int unsigned DIM_W = 8,
    localparam int unsigned ROW_IDX_W = (TILE_M <= 1) ? 1 : $clog2(TILE_M),
    localparam int unsigned K_IDX_W = (ARRAY_K <= 1) ? 1 : $clog2(ARRAY_K),
    localparam int unsigned COL_IDX_W = (ARRAY_N <= 1) ? 1 : $clog2(ARRAY_N)
) (
    input  logic                       clk_i,
    input  logic                       rst_ni,
    input  logic                       clear_i,
    input  logic                       weight_load_i,
    input  logic [K_IDX_W-1:0]         weight_k_i,
    input  logic                       step_valid_i,
    input  logic [DIM_W-1:0]           active_m_i,
    input  logic [DIM_W-1:0]           active_n_i,
    input  logic [DIM_W-1:0]           active_k_i,
    input  logic [DIM_W-1:0]           compute_cycle_i,
    input  logic [1:0]                 pattern_i,
    input  logic [ROW_IDX_W-1:0]       read_row_i,
    input  logic [COL_IDX_W-1:0]       read_col_i,
    output logic                       read_valid_o,
    output logic signed [ACC_W-1:0]    read_data_o
);

    logic [ARRAY_K-1:0]        k_mask;
    logic [ARRAY_N-1:0]        col_mask;
    logic [ARRAY_N-1:0]        weight_col_mask;
    logic [ARRAY_K-1:0]        a_valid;
    logic [ARRAY_N-1:0]        psum_valid;
    logic [ARRAY_N-1:0]        stream_valid;
    logic signed [INPUT_W-1:0] a_vec [ARRAY_K];
    logic signed [INPUT_W-1:0] weight_vec [ARRAY_N];
    logic signed [ACC_W-1:0]   psum_zero [ARRAY_N];
    logic signed [ACC_W-1:0]   stream_data [ARRAY_N];
    logic signed [ACC_W-1:0]   c_q [TILE_M][ARRAY_N];
    logic [ARRAY_N-1:0]        c_valid_q [TILE_M];
    logic [DIM_W-1:0]          stream_cycle_q;
    logic                      stream_cycle_valid_q;

    function automatic logic signed [INPUT_W-1:0] make_a(
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

        make_a = INPUT_W'(value);
    endfunction

    function automatic logic signed [INPUT_W-1:0] make_b(
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

        make_b = INPUT_W'(value);
    endfunction

    function automatic logic active_m_index(input int signed index);
        active_m_index = (index >= 0) && (DIM_W'(index) < active_m_i);
    endfunction

    always_comb begin
        for (int unsigned k_row = 0; k_row < ARRAY_K; k_row++) begin
            int signed active_m;

            k_mask[k_row] = (DIM_W'(k_row) < active_k_i);
            active_m = int'(compute_cycle_i) - int'(k_row);
            a_valid[k_row] = k_mask[k_row] && active_m_index(active_m);
            a_vec[k_row] = a_valid[k_row] ? make_a(active_m, k_row, pattern_i) : '0;
        end

        for (int unsigned col = 0; col < ARRAY_N; col++) begin
            int signed active_m;

            col_mask[col] = (DIM_W'(col) < active_n_i);
            weight_col_mask[col] = col_mask[col];
            weight_vec[col] = make_b(col, int'(weight_k_i), pattern_i);
            active_m = int'(compute_cycle_i) - int'(col);
            psum_valid[col] = col_mask[col] && active_m_index(active_m);
            psum_zero[col] = '0;
        end
    end

    npu_systolic_array #(
        .ARRAY_K(ARRAY_K),
        .ARRAY_N(ARRAY_N),
        .INPUT_W(INPUT_W),
        .ACC_W(ACC_W)
    ) u_array (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .clear_i(clear_i),
        .weight_valid_i(weight_load_i),
        .weight_k_i(weight_k_i),
        .weight_col_mask_i(weight_col_mask),
        .weight_i(weight_vec),
        .step_valid_i(step_valid_i),
        .k_mask_i(k_mask),
        .col_mask_i(col_mask),
        .a_valid_i(a_valid),
        .a_i(a_vec),
        .psum_valid_i(psum_valid),
        .psum_i(psum_zero),
        .psum_valid_o(stream_valid),
        .psum_o(stream_data)
    );

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            stream_cycle_q <= '0;
            stream_cycle_valid_q <= 1'b0;
            for (int unsigned row = 0; row < TILE_M; row++) begin
                c_valid_q[row] <= '0;
                for (int unsigned col = 0; col < ARRAY_N; col++) begin
                    c_q[row][col] <= '0;
                end
            end
        end else if (clear_i) begin
            stream_cycle_q <= '0;
            stream_cycle_valid_q <= 1'b0;
            for (int unsigned row = 0; row < TILE_M; row++) begin
                c_valid_q[row] <= '0;
                for (int unsigned col = 0; col < ARRAY_N; col++) begin
                    c_q[row][col] <= '0;
                end
            end
        end else begin
            if (stream_cycle_valid_q) begin
                for (int unsigned col = 0; col < ARRAY_N; col++) begin
                    int signed out_row;

                    out_row = int'(stream_cycle_q) - int'(ARRAY_K - 1) - int'(col);
                    if (stream_valid[col] && active_m_index(out_row)) begin
                        c_q[out_row][col] <= stream_data[col];
                        c_valid_q[out_row][col] <= 1'b1;
                    end
                end
            end

            stream_cycle_q <= compute_cycle_i;
            stream_cycle_valid_q <= step_valid_i;
        end
    end

    always_comb begin
        read_valid_o = c_valid_q[read_row_i][read_col_i];
        read_data_o = c_q[read_row_i][read_col_i];
    end

endmodule
