module npu_smoke_top (
    input  logic       clk_i,
    input  logic       rst_ni,
    input  logic       in_valid_i,
    input  logic [7:0] in_data_i,
    output logic       out_valid_o,
    output logic [7:0] out_data_o
);

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            out_valid_o <= 1'b0;
            out_data_o  <= 8'h00;
        end else begin
            out_valid_o <= in_valid_i;
            if (in_valid_i) begin
                out_data_o <= in_data_i + 8'h01;
            end
        end
    end

endmodule
