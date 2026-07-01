module npu_pe_i8 #(
    parameter int unsigned INPUT_W = 8,
    parameter int unsigned ACC_W = 32,
    localparam int unsigned PRODUCT_W = INPUT_W * 2
) (
    input  logic                         clk_i,
    input  logic                         rst_ni,
    input  logic                         clear_i,
    input  logic                         weight_valid_i,
    input  logic                         weight_mask_i,
    input  logic signed [INPUT_W-1:0]    weight_i,
    input  logic                         valid_i,
    input  logic                         mask_i,
    input  logic signed [INPUT_W-1:0]    a_i,
    input  logic signed [ACC_W-1:0]      psum_i,
    output logic                         valid_o,
    output logic signed [ACC_W-1:0]      psum_o
);

    logic signed [INPUT_W-1:0]    weight_q;
    logic signed [PRODUCT_W-1:0]  product;
    logic signed [ACC_W-1:0]      product_ext;

    assign product = a_i * weight_q;
    assign product_ext = {{(ACC_W - PRODUCT_W){product[PRODUCT_W-1]}}, product};

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            weight_q <= '0;
            valid_o  <= 1'b0;
            psum_o   <= '0;
        end else if (clear_i) begin
            weight_q <= '0;
            valid_o  <= 1'b0;
            psum_o   <= '0;
        end else begin
            if (weight_valid_i && weight_mask_i) begin
                weight_q <= weight_i;
            end

            valid_o <= valid_i;
            if (valid_i) begin
                psum_o <= mask_i ? (psum_i + product_ext) : psum_i;
            end else begin
                psum_o <= '0;
            end
        end
    end

endmodule
