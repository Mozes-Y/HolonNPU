module npu_fifo #(
    parameter int unsigned DATA_W = 32,
    parameter int unsigned DEPTH = 2,
    localparam int unsigned PTR_W = (DEPTH <= 1) ? 1 : $clog2(DEPTH),
    localparam int unsigned COUNT_W = $clog2(DEPTH + 1)
) (
    npu_vr_if.sink               in_i,
    npu_vr_if.source             out_o,
    output logic [COUNT_W-1:0]   count_o
);

    localparam logic [PTR_W-1:0]   LAST_PTR = PTR_W'(DEPTH - 1);
    localparam logic [COUNT_W-1:0] DEPTH_COUNT = COUNT_W'(DEPTH);

    logic [DATA_W-1:0]  mem_q [DEPTH];
    logic [PTR_W-1:0]   rd_ptr_q;
    logic [PTR_W-1:0]   wr_ptr_q;
    logic [COUNT_W-1:0] count_q;

    logic push;
    logic pop;

    assign in_i.ready  = (count_q != DEPTH_COUNT);
    assign out_o.valid = (count_q != '0);
    assign out_o.data  = mem_q[rd_ptr_q];
    assign count_o     = count_q;

    assign push = in_i.valid && in_i.ready;
    assign pop  = out_o.valid && out_o.ready;

    function automatic logic [PTR_W-1:0] ptr_inc(input logic [PTR_W-1:0] ptr);
        if (ptr == LAST_PTR) begin
            ptr_inc = '0;
        end else begin
            ptr_inc = ptr + 1'b1;
        end
    endfunction

    always_ff @(posedge in_i.clk_i or negedge in_i.rst_ni) begin
        if (!in_i.rst_ni) begin
            rd_ptr_q <= '0;
            wr_ptr_q <= '0;
            count_q  <= '0;
            for (int unsigned i = 0; i < DEPTH; i++) begin
                mem_q[i] <= '0;
            end
        end else begin
            if (push) begin
                mem_q[wr_ptr_q] <= in_i.data;
                wr_ptr_q <= ptr_inc(wr_ptr_q);
            end

            if (pop) begin
                rd_ptr_q <= ptr_inc(rd_ptr_q);
            end

            unique case ({push, pop})
                2'b10: count_q <= count_q + 1'b1;
                2'b01: count_q <= count_q - 1'b1;
                default: count_q <= count_q;
            endcase
        end
    end

endmodule
