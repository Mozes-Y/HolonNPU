module npu_tile_mask #(
    parameter int unsigned TILE_M = 16,
    parameter int unsigned TILE_N = 16,
    parameter int unsigned TILE_K = 16,
    parameter int unsigned DIM_W = 16
) (
    input  logic [DIM_W-1:0] m_remaining_i,
    input  logic [DIM_W-1:0] n_remaining_i,
    input  logic [DIM_W-1:0] k_remaining_i,
    output logic [TILE_M-1:0] row_mask_o,
    output logic [TILE_N-1:0] col_mask_o,
    output logic [TILE_K-1:0] k_mask_o
);

    always_comb begin
        for (int unsigned row = 0; row < TILE_M; row++) begin
            row_mask_o[row] = (DIM_W'(row) < m_remaining_i);
        end

        for (int unsigned col = 0; col < TILE_N; col++) begin
            col_mask_o[col] = (DIM_W'(col) < n_remaining_i);
        end

        for (int unsigned k = 0; k < TILE_K; k++) begin
            k_mask_o[k] = (DIM_W'(k) < k_remaining_i);
        end
    end

endmodule
