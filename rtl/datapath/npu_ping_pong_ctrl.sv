module npu_ping_pong_ctrl (
    input  logic       clk_i,
    input  logic       rst_ni,
    input  logic       start_i,
    input  logic       compute_done_i,
    input  logic       store_done_i,
    output logic [2:0] state_o,
    output logic       load_valid_o,
    output logic       compute_valid_o,
    output logic       store_valid_o,
    output logic       done_o,
    output logic       load_bank_o,
    output logic       compute_bank_o,
    output logic       store_bank_o
);

    typedef enum logic [2:0] {
        STATE_IDLE    = 3'd0,
        STATE_LOAD    = 3'd1,
        STATE_COMPUTE = 3'd2,
        STATE_STORE   = 3'd3,
        STATE_DONE    = 3'd4
    } state_e;

    state_e state_q;
    logic   bank_q;

    assign state_o = state_q;
    assign load_valid_o = (state_q == STATE_LOAD);
    assign compute_valid_o = (state_q == STATE_COMPUTE);
    assign store_valid_o = (state_q == STATE_STORE);
    assign done_o = (state_q == STATE_DONE);

    assign load_bank_o = bank_q;
    assign compute_bank_o = bank_q;
    assign store_bank_o = bank_q;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state_q <= STATE_IDLE;
            bank_q  <= 1'b0;
        end else begin
            unique case (state_q)
                STATE_IDLE: begin
                    if (start_i) begin
                        state_q <= STATE_LOAD;
                    end
                end

                STATE_LOAD: begin
                    state_q <= STATE_COMPUTE;
                end

                STATE_COMPUTE: begin
                    if (compute_done_i) begin
                        state_q <= STATE_STORE;
                    end
                end

                STATE_STORE: begin
                    if (store_done_i) begin
                        state_q <= STATE_DONE;
                        bank_q <= !bank_q;
                    end
                end

                STATE_DONE: begin
                    state_q <= STATE_IDLE;
                end

                default: begin
                    state_q <= STATE_IDLE;
                    bank_q <= 1'b0;
                end
            endcase
        end
    end

endmodule
