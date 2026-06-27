module npu_systolic_array_test_top #(
    parameter int unsigned ARRAY_M = 17,
    parameter int unsigned ARRAY_N = 19,
    parameter int unsigned INPUT_W = 8,
    parameter int unsigned ACC_W = 32,
    parameter int unsigned DIM_W = 8,
    localparam int unsigned ROW_IDX_W = (ARRAY_M <= 1) ? 1 : $clog2(ARRAY_M),
    localparam int unsigned COL_IDX_W = (ARRAY_N <= 1) ? 1 : $clog2(ARRAY_N)
) (
    input  logic                       clk_i,
    input  logic                       rst_ni,
    input  logic                       clear_i,
    input  logic                       step_valid_i,
    input  logic [DIM_W-1:0]           active_m_i,
    input  logic [DIM_W-1:0]           active_n_i,
    input  logic [DIM_W-1:0]           active_k_i,
    input  logic [DIM_W-1:0]           k_index_i,
    input  logic [1:0]                 pattern_i,
    input  logic [ROW_IDX_W-1:0]       read_row_i,
    input  logic [COL_IDX_W-1:0]       read_col_i,
    output logic                       read_valid_o,
    output logic signed [ACC_W-1:0]    read_data_o
);

    logic [ARRAY_M-1:0]        row_mask;
    logic [ARRAY_N-1:0]        col_mask;
    logic [ARRAY_M-1:0]        a_valid;
    logic [ARRAY_N-1:0]        b_valid;
    logic signed [INPUT_W-1:0] a_vec [ARRAY_M];
    logic signed [INPUT_W-1:0] b_vec [ARRAY_N];
    logic [ARRAY_N-1:0]        c_valid [ARRAY_M];
    logic signed [ACC_W-1:0]   c [ARRAY_M][ARRAY_N];

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

    always_comb begin
        for (int unsigned row = 0; row < ARRAY_M; row++) begin
            int signed skew_k;

            row_mask[row] = (DIM_W'(row) < active_m_i);
            skew_k = int'(k_index_i) - int'(row);
            a_valid[row] = row_mask[row] && (skew_k >= 0) && (DIM_W'(skew_k) < active_k_i);
            a_vec[row] = a_valid[row] ? make_a(row, skew_k, pattern_i) : '0;
        end

        for (int unsigned col = 0; col < ARRAY_N; col++) begin
            int signed skew_k;

            col_mask[col] = (DIM_W'(col) < active_n_i);
            skew_k = int'(k_index_i) - int'(col);
            b_valid[col] = col_mask[col] && (skew_k >= 0) && (DIM_W'(skew_k) < active_k_i);
            b_vec[col] = b_valid[col] ? make_b(col, skew_k, pattern_i) : '0;
        end
    end

    npu_systolic_array #(
        .ARRAY_M(ARRAY_M),
        .ARRAY_N(ARRAY_N),
        .INPUT_W(INPUT_W),
        .ACC_W(ACC_W)
    ) u_array (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .clear_i(clear_i),
        .step_valid_i(step_valid_i),
        .row_mask_i(row_mask),
        .col_mask_i(col_mask),
        .a_valid_i(a_valid),
        .b_valid_i(b_valid),
        .a_i(a_vec),
        .b_i(b_vec),
        .c_valid_o(c_valid),
        .c_o(c)
    );

    always_comb begin
        read_valid_o = c_valid[read_row_i][read_col_i];
        read_data_o = c[read_row_i][read_col_i];
    end

endmodule
