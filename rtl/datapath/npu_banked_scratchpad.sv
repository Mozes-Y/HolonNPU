module npu_banked_scratchpad #(
    parameter int unsigned DATA_W = 32,
    parameter int unsigned BANKS = 2,
    parameter int unsigned DEPTH = 256,
    localparam int unsigned BANK_W = (BANKS <= 1) ? 1 : $clog2(BANKS),
    localparam int unsigned ADDR_W = (DEPTH <= 1) ? 1 : $clog2(DEPTH),
    localparam int unsigned BANK_SEL_W = BANK_W + 1,
    localparam int unsigned ADDR_SEL_W = ADDR_W + 1
) (
    input  logic                         clk_i,
    input  logic                         rst_ni,
    input  logic                         wr_valid_i,
    input  logic [BANK_SEL_W-1:0]        wr_bank_i,
    input  logic [ADDR_SEL_W-1:0]        wr_addr_i,
    input  logic [DATA_W-1:0]            wr_data_i,
    output logic                         wr_ready_o,
    output logic                         wr_error_o,
    input  logic                         rd_valid_i,
    input  logic [BANK_SEL_W-1:0]        rd_bank_i,
    input  logic [ADDR_SEL_W-1:0]        rd_addr_i,
    output logic                         rd_valid_o,
    output logic [DATA_W-1:0]            rd_data_o,
    output logic                         rd_error_o
);

    localparam logic [BANK_SEL_W-1:0] BANK_LIMIT = BANK_SEL_W'(BANKS);
    localparam logic [ADDR_SEL_W-1:0] ADDR_LIMIT = ADDR_SEL_W'(DEPTH);

    logic [DATA_W-1:0] mem_q [BANKS][DEPTH];

    logic wr_bank_ok;
    logic wr_addr_ok;
    logic rd_bank_ok;
    logic rd_addr_ok;

    assign wr_bank_ok = (wr_bank_i < BANK_LIMIT);
    assign wr_addr_ok = (wr_addr_i < ADDR_LIMIT);
    assign rd_bank_ok = (rd_bank_i < BANK_LIMIT);
    assign rd_addr_ok = (rd_addr_i < ADDR_LIMIT);

    assign wr_ready_o = wr_bank_ok && wr_addr_ok;
    assign wr_error_o = wr_valid_i && !wr_ready_o;

    assign rd_valid_o = rd_valid_i && rd_bank_ok && rd_addr_ok;
    assign rd_error_o = rd_valid_i && !(rd_bank_ok && rd_addr_ok);
    assign rd_data_o = rd_valid_o
        ? mem_q[rd_bank_i[BANK_W-1:0]][rd_addr_i[ADDR_W-1:0]]
        : '0;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            for (int unsigned bank = 0; bank < BANKS; bank++) begin
                for (int unsigned addr = 0; addr < DEPTH; addr++) begin
                    mem_q[bank][addr] <= '0;
                end
            end
        end else if (wr_valid_i && wr_ready_o) begin
            mem_q[wr_bank_i[BANK_W-1:0]][wr_addr_i[ADDR_W-1:0]] <= wr_data_i;
        end
    end

endmodule
