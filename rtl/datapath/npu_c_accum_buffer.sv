module npu_c_accum_buffer #(
    parameter int unsigned BANKS = 2,
    parameter int unsigned DEPTH = 256,
    localparam int unsigned BANK_W = (BANKS <= 1) ? 1 : $clog2(BANKS),
    localparam int unsigned ADDR_W = (DEPTH <= 1) ? 1 : $clog2(DEPTH),
    localparam int unsigned BANK_SEL_W = BANK_W + 1,
    localparam int unsigned ADDR_SEL_W = ADDR_W + 1
) (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  logic                  wr_valid_i,
    input  logic [BANK_SEL_W-1:0] wr_bank_i,
    input  logic [ADDR_SEL_W-1:0] wr_addr_i,
    input  logic [31:0]           wr_data_i,
    output logic                  wr_ready_o,
    output logic                  wr_error_o,
    input  logic                  rd_valid_i,
    input  logic [BANK_SEL_W-1:0] rd_bank_i,
    input  logic [ADDR_SEL_W-1:0] rd_addr_i,
    output logic                  rd_valid_o,
    output logic [31:0]           rd_data_o,
    output logic                  rd_error_o
);

    npu_banked_scratchpad #(
        .DATA_W(32),
        .BANKS(BANKS),
        .DEPTH(DEPTH)
    ) u_storage (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .wr_valid_i(wr_valid_i),
        .wr_bank_i(wr_bank_i),
        .wr_addr_i(wr_addr_i),
        .wr_data_i(wr_data_i),
        .wr_ready_o(wr_ready_o),
        .wr_error_o(wr_error_o),
        .rd_valid_i(rd_valid_i),
        .rd_bank_i(rd_bank_i),
        .rd_addr_i(rd_addr_i),
        .rd_valid_o(rd_valid_o),
        .rd_data_o(rd_data_o),
        .rd_error_o(rd_error_o)
    );

endmodule
