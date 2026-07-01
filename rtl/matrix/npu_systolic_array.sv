module npu_systolic_array #(
    parameter int unsigned ARRAY_K = 16,
    parameter int unsigned ARRAY_N = 16,
    parameter int unsigned INPUT_W = 8,
    parameter int unsigned ACC_W = 32,
    localparam int unsigned K_IDX_W = (ARRAY_K <= 1) ? 1 : $clog2(ARRAY_K)
) (
    input  logic                         clk_i,
    input  logic                         rst_ni,
    input  logic                         clear_i,
    input  logic                         weight_valid_i,
    input  logic [K_IDX_W-1:0]           weight_k_i,
    input  logic [ARRAY_N-1:0]           weight_col_mask_i,
    input  logic signed [INPUT_W-1:0]    weight_i [ARRAY_N],
    input  logic                         step_valid_i,
    input  logic [ARRAY_K-1:0]           k_mask_i,
    input  logic [ARRAY_N-1:0]           col_mask_i,
    input  logic [ARRAY_K-1:0]           a_valid_i,
    input  logic signed [INPUT_W-1:0]    a_i [ARRAY_K],
    input  logic [ARRAY_N-1:0]           psum_valid_i,
    input  logic signed [ACC_W-1:0]      psum_i [ARRAY_N],
    output logic [ARRAY_N-1:0]           psum_valid_o,
    output logic signed [ACC_W-1:0]      psum_o [ARRAY_N]
);

    logic signed [INPUT_W-1:0] a_pipe_q [ARRAY_K][ARRAY_N];
    logic                      a_valid_pipe_q [ARRAY_K][ARRAY_N];
    logic signed [INPUT_W-1:0] a_to_pe [ARRAY_K][ARRAY_N];
    logic                      a_valid_to_pe [ARRAY_K][ARRAY_N];
    logic                      pe_valid_i [ARRAY_K][ARRAY_N];
    logic signed [ACC_W-1:0]   pe_psum_i [ARRAY_K][ARRAY_N];
    logic                      pe_valid_o [ARRAY_K][ARRAY_N];
    logic signed [ACC_W-1:0]   pe_psum_o [ARRAY_K][ARRAY_N];

    always_comb begin
        for (int unsigned k_row = 0; k_row < ARRAY_K; k_row++) begin
            for (int unsigned col = 0; col < ARRAY_N; col++) begin
                if (col == 0) begin
                    a_to_pe[k_row][col] = a_i[k_row];
                    a_valid_to_pe[k_row][col] = a_valid_i[k_row];
                end else begin
                    a_to_pe[k_row][col] = a_pipe_q[k_row][col - 1];
                    a_valid_to_pe[k_row][col] = a_valid_pipe_q[k_row][col - 1];
                end

                if (k_row == 0) begin
                    pe_valid_i[k_row][col] = step_valid_i && psum_valid_i[col];
                    pe_psum_i[k_row][col] = psum_i[col];
                end else begin
                    pe_valid_i[k_row][col] = step_valid_i && pe_valid_o[k_row - 1][col];
                    pe_psum_i[k_row][col] = pe_psum_o[k_row - 1][col];
                end
            end
        end
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            for (int unsigned k_row = 0; k_row < ARRAY_K; k_row++) begin
                for (int unsigned col = 0; col < ARRAY_N; col++) begin
                    a_pipe_q[k_row][col] <= '0;
                    a_valid_pipe_q[k_row][col] <= 1'b0;
                end
            end
        end else if (clear_i) begin
            for (int unsigned k_row = 0; k_row < ARRAY_K; k_row++) begin
                for (int unsigned col = 0; col < ARRAY_N; col++) begin
                    a_pipe_q[k_row][col] <= '0;
                    a_valid_pipe_q[k_row][col] <= 1'b0;
                end
            end
        end else if (step_valid_i) begin
            for (int unsigned k_row = 0; k_row < ARRAY_K; k_row++) begin
                for (int unsigned col = 0; col < ARRAY_N; col++) begin
                    a_pipe_q[k_row][col] <= a_to_pe[k_row][col];
                    a_valid_pipe_q[k_row][col] <= a_valid_to_pe[k_row][col];
                end
            end
        end
    end

    for (genvar k_row = 0; k_row < ARRAY_K; k_row++) begin : gen_k_rows
        for (genvar col = 0; col < ARRAY_N; col++) begin : gen_cols
            npu_pe_i8 #(
                .INPUT_W(INPUT_W),
                .ACC_W(ACC_W)
            ) u_pe (
                .clk_i(clk_i),
                .rst_ni(rst_ni),
                .clear_i(clear_i),
                .weight_valid_i(weight_valid_i && (weight_k_i == K_IDX_W'(k_row))),
                .weight_mask_i(weight_col_mask_i[col]),
                .weight_i(weight_i[col]),
                .valid_i(pe_valid_i[k_row][col]),
                .mask_i(k_mask_i[k_row] && col_mask_i[col] && a_valid_to_pe[k_row][col]),
                .a_i(a_to_pe[k_row][col]),
                .psum_i(pe_psum_i[k_row][col]),
                .valid_o(pe_valid_o[k_row][col]),
                .psum_o(pe_psum_o[k_row][col])
            );
        end
    end

    always_comb begin
        for (int unsigned col = 0; col < ARRAY_N; col++) begin
            psum_valid_o[col] = pe_valid_o[ARRAY_K - 1][col];
            psum_o[col] = pe_psum_o[ARRAY_K - 1][col];
        end
    end

endmodule
