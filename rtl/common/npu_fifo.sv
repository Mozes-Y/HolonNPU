module npu_fifo #(
    parameter int unsigned DATA_W = 32,
    parameter int unsigned DEPTH = 2,
    localparam int unsigned PTR_W = (DEPTH <= 1) ? 1 : $clog2(DEPTH),
    localparam int unsigned COUNT_W = $clog2(DEPTH + 1)
) (
    input  logic                 clk_i,
    input  logic                 rst_ni,
    input  logic                 in_valid_i,
    output logic                 in_ready_o,
    input  logic [DATA_W-1:0]    in_data_i,
    output logic                 out_valid_o,
    input  logic                 out_ready_i,
    output logic [DATA_W-1:0]    out_data_o,
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

    assign in_ready_o  = (count_q != DEPTH_COUNT);
    assign out_valid_o = (count_q != '0);
    assign out_data_o  = mem_q[rd_ptr_q];
    assign count_o     = count_q;

    assign push = in_valid_i && in_ready_o;
    assign pop  = out_valid_o && out_ready_i;

    function automatic logic [PTR_W-1:0] ptr_inc(input logic [PTR_W-1:0] ptr);
        if (ptr == LAST_PTR) begin
            ptr_inc = '0;
        end else begin
            ptr_inc = ptr + 1'b1;
        end
    endfunction

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            rd_ptr_q <= '0;
            wr_ptr_q <= '0;
            count_q  <= '0;
            for (int unsigned i = 0; i < DEPTH; i++) begin
                mem_q[i] <= '0;
            end
        end else begin
            if (push) begin
                mem_q[wr_ptr_q] <= in_data_i;
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
