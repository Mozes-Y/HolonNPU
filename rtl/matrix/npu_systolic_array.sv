module npu_systolic_array #(
    parameter int unsigned ARRAY_M = 16,
    parameter int unsigned ARRAY_N = 16,
    parameter int unsigned INPUT_W = 8,
    parameter int unsigned ACC_W = 32
) (
    input  logic                         clk_i,
    input  logic                         rst_ni,
    input  logic                         clear_i,
    input  logic                         step_valid_i,
    input  logic [ARRAY_M-1:0]           row_mask_i,
    input  logic [ARRAY_N-1:0]           col_mask_i,
    input  logic [ARRAY_M-1:0]           a_valid_i,
    input  logic [ARRAY_N-1:0]           b_valid_i,
    input  logic signed [INPUT_W-1:0]    a_i [ARRAY_M],
    input  logic signed [INPUT_W-1:0]    b_i [ARRAY_N],
    output logic [ARRAY_N-1:0]           c_valid_o [ARRAY_M],
    output logic signed [ACC_W-1:0]      c_o [ARRAY_M][ARRAY_N]
);

    logic signed [INPUT_W-1:0] a_pipe_q [ARRAY_M][ARRAY_N];
    logic signed [INPUT_W-1:0] b_pipe_q [ARRAY_M][ARRAY_N];
    logic                      a_valid_pipe_q [ARRAY_M][ARRAY_N];
    logic                      b_valid_pipe_q [ARRAY_M][ARRAY_N];

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            for (int unsigned row = 0; row < ARRAY_M; row++) begin
                for (int unsigned col = 0; col < ARRAY_N; col++) begin
                    a_pipe_q[row][col] <= '0;
                    b_pipe_q[row][col] <= '0;
                    a_valid_pipe_q[row][col] <= 1'b0;
                    b_valid_pipe_q[row][col] <= 1'b0;
                end
            end
        end else if (clear_i) begin
            for (int unsigned row = 0; row < ARRAY_M; row++) begin
                for (int unsigned col = 0; col < ARRAY_N; col++) begin
                    a_pipe_q[row][col] <= '0;
                    b_pipe_q[row][col] <= '0;
                    a_valid_pipe_q[row][col] <= 1'b0;
                    b_valid_pipe_q[row][col] <= 1'b0;
                end
            end
        end else if (step_valid_i) begin
            for (int unsigned row = 0; row < ARRAY_M; row++) begin
                for (int unsigned col = 0; col < ARRAY_N; col++) begin
                    if (col == 0) begin
                        a_pipe_q[row][col] <= a_i[row];
                        a_valid_pipe_q[row][col] <= a_valid_i[row];
                    end else begin
                        a_pipe_q[row][col] <= a_pipe_q[row][col - 1];
                        a_valid_pipe_q[row][col] <= a_valid_pipe_q[row][col - 1];
                    end

                    if (row == 0) begin
                        b_pipe_q[row][col] <= b_i[col];
                        b_valid_pipe_q[row][col] <= b_valid_i[col];
                    end else begin
                        b_pipe_q[row][col] <= b_pipe_q[row - 1][col];
                        b_valid_pipe_q[row][col] <= b_valid_pipe_q[row - 1][col];
                    end
                end
            end
        end
    end

    for (genvar row = 0; row < ARRAY_M; row++) begin : gen_rows
        for (genvar col = 0; col < ARRAY_N; col++) begin : gen_cols
            npu_pe_i8 #(
                .INPUT_W(INPUT_W),
                .ACC_W(ACC_W)
            ) u_pe (
                .clk_i(clk_i),
                .rst_ni(rst_ni),
                .clear_i(clear_i),
                .valid_i(step_valid_i),
                .mask_i(row_mask_i[row] && col_mask_i[col] &&
                        a_valid_pipe_q[row][col] && b_valid_pipe_q[row][col]),
                .a_i(a_pipe_q[row][col]),
                .b_i(b_pipe_q[row][col]),
                .valid_o(c_valid_o[row][col]),
                .acc_o(c_o[row][col])
            );
        end
    end

endmodule
