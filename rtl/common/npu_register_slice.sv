module npu_register_slice #(
    parameter int unsigned DATA_W = 32
) (
    npu_vr_if.sink           in_i,
    npu_vr_if.source         out_o
);

    logic              valid_q;
    logic [DATA_W-1:0] data_q;

    assign in_i.ready  = !valid_q || out_o.ready;
    assign out_o.valid = valid_q;
    assign out_o.data  = data_q;

    always_ff @(posedge in_i.clk_i or negedge in_i.rst_ni) begin
        if (!in_i.rst_ni) begin
            valid_q <= 1'b0;
            data_q  <= '0;
        end else if (in_i.ready) begin
            valid_q <= in_i.valid;
            if (in_i.valid) begin
                data_q <= in_i.data;
            end
        end
    end

endmodule
