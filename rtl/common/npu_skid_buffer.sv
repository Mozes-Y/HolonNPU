module npu_skid_buffer #(
    parameter int unsigned DATA_W = 32
) (
    npu_vr_if.sink           in_i,
    npu_vr_if.source         out_o
);

    logic              full_q;
    logic [DATA_W-1:0] data_q;

    assign in_i.ready  = !full_q || out_o.ready;
    assign out_o.valid = full_q || in_i.valid;
    assign out_o.data  = full_q ? data_q : in_i.data;

    always_ff @(posedge in_i.clk_i or negedge in_i.rst_ni) begin
        if (!in_i.rst_ni) begin
            full_q <= 1'b0;
            data_q <= '0;
        end else if (full_q) begin
            if (out_o.ready) begin
                if (in_i.valid) begin
                    full_q <= 1'b1;
                    data_q <= in_i.data;
                end else begin
                    full_q <= 1'b0;
                end
            end
        end else if (in_i.valid && !out_o.ready) begin
            full_q <= 1'b1;
            data_q <= in_i.data;
        end
    end

endmodule
