module npu_gemm_tile_scratchpad #(
    parameter int unsigned TILE_M = 16,
    parameter int unsigned TILE_N = 16,
    parameter int unsigned TILE_K = 16,
    parameter int unsigned INPUT_W = 8,
    parameter int unsigned DATA_W = 128
) (
    input  logic                         clk_i,
    input  logic                         rst_ni,
    input  logic                         clear_i,

    input  logic                         a_wr_valid_i,
    input  logic [3:0]                   a_wr_row_i,
    input  logic [DATA_W-1:0]            a_wr_data_i,
    input  logic                         b_wr_valid_i,
    input  logic [3:0]                   b_wr_k_i,
    input  logic [DATA_W-1:0]            b_wr_data_i,

    input  logic [31:0]                  m_i,
    input  logic [31:0]                  n_i,
    input  logic [31:0]                  k_i,
    input  logic [31:0]                  m_base_i,
    input  logic [31:0]                  n_base_i,
    input  logic [31:0]                  k_base_i,
    input  logic [5:0]                   compute_cycle_i,

    output logic [TILE_M-1:0]            row_mask_o,
    output logic [TILE_N-1:0]            col_mask_o,
    output logic [TILE_M-1:0]            a_valid_o,
    output logic [TILE_N-1:0]            b_valid_o,
    output logic signed [INPUT_W-1:0]    a_o [TILE_M],
    output logic signed [INPUT_W-1:0]    b_o [TILE_N]
);

    logic signed [INPUT_W-1:0] a_mem_q [TILE_M][TILE_K];
    logic signed [INPUT_W-1:0] b_mem_q [TILE_K][TILE_N];

    function automatic logic active_k_index(input int signed index);
        active_k_index = (index >= 0) && (index < TILE_K) &&
                         ((k_base_i + 32'(index)) < k_i);
    endfunction

    always_comb begin
        for (int unsigned row = 0; row < TILE_M; row++) begin
            int signed skew_k;

            row_mask_o[row] = ((m_base_i + 32'(row)) < m_i);
            skew_k = int'(compute_cycle_i) - int'(row);
            a_valid_o[row] = row_mask_o[row] && active_k_index(skew_k);
            a_o[row] = a_valid_o[row] ? a_mem_q[row][skew_k] : '0;
        end

        for (int unsigned col = 0; col < TILE_N; col++) begin
            int signed skew_k;

            col_mask_o[col] = ((n_base_i + 32'(col)) < n_i);
            skew_k = int'(compute_cycle_i) - int'(col);
            b_valid_o[col] = col_mask_o[col] && active_k_index(skew_k);
            b_o[col] = b_valid_o[col] ? b_mem_q[skew_k][col] : '0;
        end
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            for (int unsigned row = 0; row < TILE_M; row++) begin
                for (int unsigned col = 0; col < TILE_K; col++) begin
                    a_mem_q[row][col] <= '0;
                end
            end
            for (int unsigned row = 0; row < TILE_K; row++) begin
                for (int unsigned col = 0; col < TILE_N; col++) begin
                    b_mem_q[row][col] <= '0;
                end
            end
        end else if (clear_i) begin
            for (int unsigned row = 0; row < TILE_M; row++) begin
                for (int unsigned col = 0; col < TILE_K; col++) begin
                    a_mem_q[row][col] <= '0;
                end
            end
            for (int unsigned row = 0; row < TILE_K; row++) begin
                for (int unsigned col = 0; col < TILE_N; col++) begin
                    b_mem_q[row][col] <= '0;
                end
            end
        end else begin
            if (a_wr_valid_i) begin
                for (int unsigned col = 0; col < TILE_K; col++) begin
                    a_mem_q[int'(a_wr_row_i)][col] <= a_wr_data_i[(col * INPUT_W) +: INPUT_W];
                end
            end

            if (b_wr_valid_i) begin
                for (int unsigned col = 0; col < TILE_N; col++) begin
                    b_mem_q[int'(b_wr_k_i)][col] <= b_wr_data_i[(col * INPUT_W) +: INPUT_W];
                end
            end
        end
    end

endmodule
