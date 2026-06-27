module npu_skid_buffer #(
    parameter int unsigned DATA_W = 32
) (
    input  logic              clk_i,
    input  logic              rst_ni,
    input  logic              in_valid_i,
    output logic              in_ready_o,
    input  logic [DATA_W-1:0] in_data_i,
    output logic              out_valid_o,
    input  logic              out_ready_i,
    output logic [DATA_W-1:0] out_data_o
);

    logic              full_q;
    logic [DATA_W-1:0] data_q;

    assign in_ready_o  = !full_q || out_ready_i;
    assign out_valid_o = full_q || in_valid_i;
    assign out_data_o  = full_q ? data_q : in_data_i;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            full_q <= 1'b0;
            data_q <= '0;
        end else if (full_q) begin
            if (out_ready_i) begin
                if (in_valid_i) begin
                    full_q <= 1'b1;
                    data_q <= in_data_i;
                end else begin
                    full_q <= 1'b0;
                end
            end
        end else if (in_valid_i && !out_ready_i) begin
            full_q <= 1'b1;
            data_q <= in_data_i;
        end
    end

endmodule
