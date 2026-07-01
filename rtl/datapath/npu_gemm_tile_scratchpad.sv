module npu_gemm_tile_scratchpad #(
    parameter int unsigned TILE_M = 16,
    parameter int unsigned TILE_N = 16,
    parameter int unsigned TILE_K = 16,
    parameter int unsigned INPUT_W = 8,
    parameter int unsigned ACC_W = 32,
    parameter int unsigned DATA_W = 128
) (
    input  logic                         clk_i,
    input  logic                         rst_ni,
    input  logic                         clear_i,

    input  logic                         a_wr_valid_i,
    input  logic [3:0]                   a_wr_row_i,
    input  logic [DATA_W-1:0]            a_wr_data_i,

    input  logic [31:0]                  m_i,
    input  logic [31:0]                  n_i,
    input  logic [31:0]                  k_i,
    input  logic [31:0]                  m_base_i,
    input  logic [31:0]                  n_base_i,
    input  logic [31:0]                  k_base_i,
    input  logic [5:0]                   compute_cycle_i,

    output logic [TILE_K-1:0]            k_mask_o,
    output logic [TILE_N-1:0]            col_mask_o,
    output logic [TILE_K-1:0]            a_valid_o,
    output logic signed [INPUT_W-1:0]    a_o [TILE_K],
    output logic [TILE_N-1:0]            psum_valid_o,
    output logic signed [ACC_W-1:0]      psum_o [TILE_N]
);

    logic signed [INPUT_W-1:0] a_mem_q [TILE_M][TILE_K];

    function automatic logic active_m_index(input int signed index);
        active_m_index = (index >= 0) && (index < TILE_M) &&
                         ((m_base_i + 32'(index)) < m_i);
    endfunction

    function automatic logic active_k_index(input int signed index);
        active_k_index = (index >= 0) && (index < TILE_K) &&
                         ((k_base_i + 32'(index)) < k_i);
    endfunction

    always_comb begin
        for (int unsigned k_row = 0; k_row < TILE_K; k_row++) begin
            int signed active_m;

            k_mask_o[k_row] = active_k_index(k_row);
            active_m = int'(compute_cycle_i) - int'(k_row);
            a_valid_o[k_row] = k_mask_o[k_row] && active_m_index(active_m);
            a_o[k_row] = '0;
            if (a_valid_o[k_row]) begin
                a_o[k_row] = a_mem_q[active_m][k_row];
            end
        end

        for (int unsigned col = 0; col < TILE_N; col++) begin
            int signed active_m;

            col_mask_o[col] = ((n_base_i + 32'(col)) < n_i);
            active_m = int'(compute_cycle_i) - int'(col);
            psum_valid_o[col] = col_mask_o[col] && active_m_index(active_m);
            psum_o[col] = '0;
        end
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            for (int unsigned row = 0; row < TILE_M; row++) begin
                for (int unsigned col = 0; col < TILE_K; col++) begin
                    a_mem_q[row][col] <= '0;
                end
            end
        end else if (clear_i) begin
            for (int unsigned row = 0; row < TILE_M; row++) begin
                for (int unsigned col = 0; col < TILE_K; col++) begin
                    a_mem_q[row][col] <= '0;
                end
            end
        end else if (a_wr_valid_i) begin
            for (int unsigned col = 0; col < TILE_K; col++) begin
                a_mem_q[int'(a_wr_row_i)][col] <= a_wr_data_i[(col * INPUT_W) +: INPUT_W];
            end
        end
    end

endmodule
