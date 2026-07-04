module npu_assert_fail_test_top (
    input  logic        clk_i,
    input  logic        rst_ni,
    input  logic        valid_i,
    input  logic        ready_i,
    input  logic [31:0] data_i
);

    npu_vr_if #(
        .DATA_W(32)
    ) vr_if (
        .clk_i(clk_i),
        .rst_ni(rst_ni)
    );

    assign vr_if.valid = valid_i;
    assign vr_if.ready = ready_i;
    assign vr_if.data = data_i;

endmodule
