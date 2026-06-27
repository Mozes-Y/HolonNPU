module npu_pe_i8 #(
    parameter int unsigned INPUT_W = 8,
    parameter int unsigned ACC_W = 32,
    localparam int unsigned PRODUCT_W = INPUT_W * 2
) (
    input  logic                         clk_i,
    input  logic                         rst_ni,
    input  logic                         clear_i,
    input  logic                         valid_i,
    input  logic                         mask_i,
    input  logic signed [INPUT_W-1:0]    a_i,
    input  logic signed [INPUT_W-1:0]    b_i,
    output logic                         valid_o,
    output logic signed [ACC_W-1:0]      acc_o
);

    logic signed [PRODUCT_W-1:0] product;
    logic signed [ACC_W-1:0]     product_ext;

    assign product = a_i * b_i;
    assign product_ext = {{(ACC_W - PRODUCT_W){product[PRODUCT_W-1]}}, product};

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            valid_o <= 1'b0;
            acc_o   <= '0;
        end else if (clear_i) begin
            valid_o <= 1'b0;
            acc_o   <= '0;
        end else if (valid_i) begin
            if (mask_i) begin
                valid_o <= 1'b1;
                acc_o   <= acc_o + product_ext;
            end
        end
    end

endmodule
