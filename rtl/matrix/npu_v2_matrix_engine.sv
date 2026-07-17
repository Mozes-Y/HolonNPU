/* verilator lint_off DECLFILENAME */

module npu_v2_matrix_engine_core #(
    parameter int unsigned TILE_M = 16,
    parameter int unsigned TILE_N = 16,
    parameter int unsigned TILE_K = 16,
    parameter int unsigned INPUT_W = 8,
    parameter int unsigned ACC_W = 32
) (
    input  logic                  clk_i,
    input  logic                  rst_ni,
    input  logic                  soft_reset_i,
    input  logic [31:0]           local_mem_bytes_i,

    npu_vr_if.sink                issue,
    npu_vr_if.source              result,
    npu_v2_localmem_rd_if.master  data_rd,
    npu_v2_localmem_wr_if.master  data_wr
);

    import npu_v2_pkg::*;
    import npu_isa_pkg::*;

    localparam int unsigned SYSTOLIC_CYCLES = TILE_M + TILE_K + TILE_N - 1;

    typedef enum logic [4:0] {
        STATE_IDLE,
        STATE_DESC_REQ,
        STATE_DESC_WAIT,
        STATE_VALIDATE,
        STATE_ARRAY_CLEAR,
        STATE_LOAD_A_REQ,
        STATE_LOAD_A_WAIT,
        STATE_LOAD_B_REQ,
        STATE_LOAD_B_WAIT,
        STATE_LOAD_WEIGHT,
        STATE_COMPUTE,
        STATE_DRAIN,
        STATE_STORE_REQ,
        STATE_STORE_WAIT,
        STATE_EVENT
    } state_e;

    state_e state_q;
    logic [11:0] command_base_q;
    logic [31:0] command_q [8];
    logic [2:0] command_word_q;
    logic [4:0] row_q;
    logic [4:0] col_q;
    logic [4:0] k_q;
    logic [5:0] compute_cycle_q;
    logic [5:0] array_output_cycle_q;
    logic       array_output_valid_q;
    logic       event_fault_q;
    logic [31:0] event_fault_code_q;

    logic signed [INPUT_W-1:0] a_mem_q [TILE_M][TILE_K];
    logic signed [INPUT_W-1:0] b_mem_q [TILE_K][TILE_N];
    logic signed [ACC_W-1:0] accumulator_q [TILE_M][TILE_N];
    logic accumulator_valid_q;
    logic [4:0] accumulator_m_q;
    logic [4:0] accumulator_n_q;

    logic [31:0] instruction_class;
    logic [3:0] instruction_opcode;
    logic [3:0] instruction_rd;
    logic [3:0] instruction_rs1;
    logic [3:0] instruction_rs2;
    logic [11:0] instruction_imm;
    logic instruction_reserved_zero;

    logic [31:0] a_base;
    logic [31:0] b_base;
    logic [31:0] c_base;
    logic [31:0] a_stride;
    logic [31:0] b_stride;
    logic [31:0] c_stride;
    logic [7:0] active_m;
    logic [7:0] active_n;
    logic [7:0] active_k;
    logic [7:0] command_flags;
    logic command_clear;
    logic command_accumulate;
    logic command_store;
    logic command_valid;

    logic [31:0] active_a_addr;
    logic [31:0] active_b_addr;
    logic [31:0] active_c_addr;
    logic [31:0] active_read_addr;
    logic [1:0] active_byte_lane;

    logic array_clear;
    logic array_step;
    logic [TILE_K-1:0] k_mask;
    logic [TILE_N-1:0] col_mask;
    logic [TILE_K-1:0] a_valid;
    logic signed [INPUT_W-1:0] a_vector [TILE_K];
    logic [TILE_N-1:0] psum_valid;
    logic signed [ACC_W-1:0] psum_zero [TILE_N];
    logic signed [INPUT_W-1:0] weight_vector [TILE_N];
    logic [TILE_N-1:0] stream_valid;
    logic signed [ACC_W-1:0] stream_data [TILE_N];

    assign instruction_class = issue.data[31:0] & NPU_ISA_CLASS_MASK;
    assign instruction_opcode = 4'((issue.data[31:0] >> NPU_ISA_OPCODE_SHIFT) & NPU_ISA_FIELD_MASK);
    assign instruction_rd = 4'((issue.data[31:0] >> NPU_ISA_RD_SHIFT) & NPU_ISA_FIELD_MASK);
    assign instruction_rs1 = 4'((issue.data[31:0] >> NPU_ISA_RS1_SHIFT) & NPU_ISA_FIELD_MASK);
    assign instruction_rs2 = 4'((issue.data[31:0] >> NPU_ISA_RS2_SHIFT) & NPU_ISA_FIELD_MASK);
    assign instruction_imm = 12'(issue.data[31:0] & NPU_ISA_IMM_MASK);
    assign instruction_reserved_zero = issue.data[127:32] == 96'd0;

    assign a_base = command_q[NPU_ISA_MATRIX_COMMAND_A_OFFSET >> 2];
    assign b_base = command_q[NPU_ISA_MATRIX_COMMAND_B_OFFSET >> 2];
    assign c_base = command_q[NPU_ISA_MATRIX_COMMAND_C_OFFSET >> 2];
    assign a_stride = command_q[NPU_ISA_MATRIX_COMMAND_A_STRIDE_OFFSET >> 2];
    assign b_stride = command_q[NPU_ISA_MATRIX_COMMAND_B_STRIDE_OFFSET >> 2];
    assign c_stride = command_q[NPU_ISA_MATRIX_COMMAND_C_STRIDE_OFFSET >> 2];
    assign active_m = 8'((command_q[NPU_ISA_MATRIX_COMMAND_SHAPE_OFFSET >> 2] >>
                          NPU_ISA_MATRIX_SHAPE_M_SHIFT) & NPU_ISA_MATRIX_DIMENSION_MASK);
    assign active_n = 8'((command_q[NPU_ISA_MATRIX_COMMAND_SHAPE_OFFSET >> 2] >>
                          NPU_ISA_MATRIX_SHAPE_N_SHIFT) & NPU_ISA_MATRIX_DIMENSION_MASK);
    assign active_k = 8'((command_q[NPU_ISA_MATRIX_COMMAND_SHAPE_OFFSET >> 2] >>
                          NPU_ISA_MATRIX_SHAPE_K_SHIFT) & NPU_ISA_MATRIX_DIMENSION_MASK);
    assign command_flags = 8'(command_q[NPU_ISA_MATRIX_COMMAND_SHAPE_OFFSET >> 2] >>
                              NPU_ISA_MATRIX_SHAPE_FLAGS_SHIFT);
    assign command_clear = (32'(command_flags) & NPU_ISA_MATRIX_FLAG_CLEAR) != 32'd0;
    assign command_accumulate = (32'(command_flags) & NPU_ISA_MATRIX_FLAG_ACCUMULATE) != 32'd0;
    assign command_store = (32'(command_flags) & NPU_ISA_MATRIX_FLAG_STORE) != 32'd0;

    assign active_a_addr = a_base + (32'(row_q) * a_stride) + 32'(k_q);
    assign active_b_addr = b_base + (32'(k_q) * b_stride) + 32'(col_q);
    assign active_c_addr = c_base + (32'(row_q) * c_stride) + (32'(col_q) << 2);
    assign active_read_addr = (state_q == STATE_LOAD_A_REQ || state_q == STATE_LOAD_A_WAIT)
        ? active_a_addr
        : active_b_addr;
    assign active_byte_lane = active_read_addr[1:0];

    assign issue.ready = state_q == STATE_IDLE;
    assign result.valid = state_q == STATE_EVENT;
    assign result.data = {event_fault_code_q, 31'd0, event_fault_q};

    assign data_rd.req_valid = (state_q == STATE_DESC_REQ) ||
                               (state_q == STATE_LOAD_A_REQ) ||
                               (state_q == STATE_LOAD_B_REQ);
    assign data_rd.req_addr = state_q == STATE_DESC_REQ
        ? {20'd0, command_base_q} + (32'(command_word_q) << 2)
        : {active_read_addr[31:2], 2'b00};

    assign data_wr.req_valid = state_q == STATE_STORE_REQ;
    assign data_wr.req_addr = active_c_addr;
    assign data_wr.req_data = accumulator_q[row_q[3:0]][col_q[3:0]];
    assign data_wr.req_strb = 4'hF;

    assign array_clear = (state_q == STATE_ARRAY_CLEAR) || soft_reset_i;
    assign array_step = state_q == STATE_COMPUTE;

    function automatic logic range_2d_ok(
        input logic [31:0] base,
        input logic [7:0] rows,
        input logic [31:0] stride,
        input logic [31:0] row_bytes
    );
        logic [63:0] end_address;
        begin
            end_address = 64'(base) + (64'(rows - 8'd1) * 64'(stride)) + 64'(row_bytes);
            range_2d_ok = (rows != 8'd0) && (end_address <= 64'(local_mem_bytes_i));
        end
    endfunction

    always_comb begin
        command_valid = (active_m != 8'd0) && (active_n != 8'd0) && (active_k != 8'd0) &&
                        (32'(active_m) <= NPU_ISA_MATRIX_MAX_DIMENSION) &&
                        (32'(active_n) <= NPU_ISA_MATRIX_MAX_DIMENSION) &&
                        (32'(active_k) <= NPU_ISA_MATRIX_MAX_DIMENSION) &&
                        ((command_flags & ~8'(NPU_ISA_MATRIX_FLAGS_VALID_MASK)) == 8'd0) &&
                        (command_clear != command_accumulate) &&
                        (a_stride >= 32'(active_k)) &&
                        (b_stride >= 32'(active_n)) &&
                        range_2d_ok(a_base, active_m, a_stride, 32'(active_k)) &&
                        range_2d_ok(b_base, active_k, b_stride, 32'(active_n)) &&
                        (command_q[NPU_ISA_MATRIX_COMMAND_RESERVED_OFFSET >> 2] == 32'd0);
        if (command_accumulate) begin
            command_valid = command_valid && accumulator_valid_q &&
                            (accumulator_m_q == 5'(active_m)) &&
                            (accumulator_n_q == 5'(active_n));
        end
        if (command_store) begin
            command_valid = command_valid && (c_base[1:0] == 2'b00) &&
                            (c_stride[1:0] == 2'b00) &&
                            (c_stride >= (32'(active_n) << 2)) &&
                            range_2d_ok(c_base, active_m, c_stride, 32'(active_n) << 2);
        end
    end

    always_comb begin
        for (int unsigned k_index = 0; k_index < TILE_K; k_index++) begin
            int signed active_row;

            active_row = int'(compute_cycle_q) - int'(k_index);
            k_mask[k_index] = k_index < int'(active_k);
            a_valid[k_index] = k_mask[k_index] &&
                               (active_row >= 0) && (active_row < int'(active_m));
            a_vector[k_index] = '0;
            if (a_valid[k_index]) begin
                a_vector[k_index] = a_mem_q[active_row][k_index];
            end
        end
        for (int unsigned n_index = 0; n_index < TILE_N; n_index++) begin
            int signed active_row;

            active_row = int'(compute_cycle_q) - int'(n_index);
            col_mask[n_index] = n_index < int'(active_n);
            psum_valid[n_index] = col_mask[n_index] &&
                                  (active_row >= 0) && (active_row < int'(active_m));
            psum_zero[n_index] = '0;
            weight_vector[n_index] = b_mem_q[k_q[3:0]][n_index];
        end
    end

    npu_systolic_array #(
        .ARRAY_K(TILE_K),
        .ARRAY_N(TILE_N),
        .INPUT_W(INPUT_W),
        .ACC_W(ACC_W)
    ) u_array (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .clear_i(array_clear),
        .weight_valid_i(state_q == STATE_LOAD_WEIGHT),
        .weight_k_i(k_q[3:0]),
        .weight_col_mask_i(col_mask),
        .weight_i(weight_vector),
        .step_valid_i(array_step),
        .k_mask_i(k_mask),
        .col_mask_i(col_mask),
        .a_valid_i(a_valid),
        .a_i(a_vector),
        .psum_valid_i(psum_valid),
        .psum_i(psum_zero),
        .psum_valid_o(stream_valid),
        .psum_o(stream_data)
    );

    task automatic complete_ok();
        begin
            event_fault_q <= 1'b0;
            event_fault_code_q <= NPU_V2_FAULT_NONE;
            state_q <= STATE_EVENT;
        end
    endtask

    task automatic complete_fault();
        begin
            event_fault_q <= 1'b1;
            event_fault_code_q <= NPU_V2_FAULT_MATRIX_ISSUE;
            state_q <= STATE_EVENT;
        end
    endtask

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state_q <= STATE_IDLE;
            command_base_q <= 12'd0;
            command_word_q <= 3'd0;
            row_q <= 5'd0;
            col_q <= 5'd0;
            k_q <= 5'd0;
            compute_cycle_q <= 6'd0;
            array_output_cycle_q <= 6'd0;
            array_output_valid_q <= 1'b0;
            event_fault_q <= 1'b0;
            event_fault_code_q <= NPU_V2_FAULT_NONE;
            accumulator_valid_q <= 1'b0;
            accumulator_m_q <= 5'd0;
            accumulator_n_q <= 5'd0;
            for (int word = 0; word < 8; word++) command_q[word] <= 32'd0;
            for (int row = 0; row < TILE_M; row++) begin
                for (int col = 0; col < TILE_N; col++) begin
                    accumulator_q[row][col] <= '0;
                end
                for (int kk = 0; kk < TILE_K; kk++) a_mem_q[row][kk] <= '0;
            end
            for (int kk = 0; kk < TILE_K; kk++) begin
                for (int col = 0; col < TILE_N; col++) b_mem_q[kk][col] <= '0;
            end
        end else if (soft_reset_i) begin
            state_q <= STATE_IDLE;
            command_word_q <= 3'd0;
            row_q <= 5'd0;
            col_q <= 5'd0;
            k_q <= 5'd0;
            compute_cycle_q <= 6'd0;
            array_output_cycle_q <= 6'd0;
            array_output_valid_q <= 1'b0;
            event_fault_q <= 1'b0;
            event_fault_code_q <= NPU_V2_FAULT_NONE;
            accumulator_valid_q <= 1'b0;
            accumulator_m_q <= 5'd0;
            accumulator_n_q <= 5'd0;
            for (int row = 0; row < TILE_M; row++) begin
                for (int col = 0; col < TILE_N; col++) accumulator_q[row][col] <= '0;
            end
        end else begin
            if (array_output_valid_q) begin
                for (int col = 0; col < TILE_N; col++) begin
                    int signed output_row;

                    output_row = int'(array_output_cycle_q) - int'(TILE_K - 1) - col;
                    if (stream_valid[col] && (output_row >= 0) &&
                        (output_row < int'(active_m)) && (col < int'(active_n))) begin
                        accumulator_q[output_row][col] <=
                            accumulator_q[output_row][col] + stream_data[col];
                    end
                end
            end
            array_output_cycle_q <= compute_cycle_q;
            array_output_valid_q <= array_step;

            unique case (state_q)
                STATE_IDLE: begin
                    if (issue.valid && issue.ready) begin
                        command_base_q <= instruction_imm;
                        command_word_q <= 3'd0;
                        event_fault_q <= 1'b0;
                        event_fault_code_q <= NPU_V2_FAULT_NONE;
                        if (!instruction_reserved_zero ||
                            (instruction_class != NPU_ISA_CLASS_MATRIX) ||
                            (instruction_opcode != NPU_ISA_OPCODE_MATRIX_GEMM) ||
                            (instruction_rd != 4'd0) ||
                            (instruction_rs1 != 4'd0) ||
                            (instruction_rs2 != 4'd0) ||
                            ((32'(instruction_imm) & (NPU_ISA_MATRIX_COMMAND_BYTES - 1)) != 32'd0) ||
                            ((32'(instruction_imm) + NPU_ISA_MATRIX_COMMAND_BYTES) > local_mem_bytes_i)) begin
                            complete_fault();
                        end else begin
                            state_q <= STATE_DESC_REQ;
                        end
                    end
                end

                STATE_DESC_REQ: begin
                    if (data_rd.req_ready) state_q <= STATE_DESC_WAIT;
                end

                STATE_DESC_WAIT: begin
                    if (data_rd.resp_valid) begin
                        if (data_rd.resp_error) begin
                            complete_fault();
                        end else begin
                            command_q[command_word_q] <= data_rd.resp_data;
                            if (command_word_q == 3'd7) begin
                                state_q <= STATE_VALIDATE;
                            end else begin
                                command_word_q <= command_word_q + 3'd1;
                                state_q <= STATE_DESC_REQ;
                            end
                        end
                    end
                end

                STATE_VALIDATE: begin
                    if (!command_valid) begin
                        complete_fault();
                    end else begin
                        row_q <= 5'd0;
                        col_q <= 5'd0;
                        k_q <= 5'd0;
                        compute_cycle_q <= 6'd0;
                        array_output_valid_q <= 1'b0;
                        accumulator_valid_q <= 1'b1;
                        accumulator_m_q <= 5'(active_m);
                        accumulator_n_q <= 5'(active_n);
                        if (command_clear) begin
                            for (int row = 0; row < TILE_M; row++) begin
                                for (int col = 0; col < TILE_N; col++) begin
                                    accumulator_q[row][col] <= '0;
                                end
                            end
                        end
                        state_q <= STATE_ARRAY_CLEAR;
                    end
                end

                STATE_ARRAY_CLEAR: state_q <= STATE_LOAD_A_REQ;

                STATE_LOAD_A_REQ: begin
                    if (data_rd.req_ready) state_q <= STATE_LOAD_A_WAIT;
                end

                STATE_LOAD_A_WAIT: begin
                    if (data_rd.resp_valid) begin
                        if (data_rd.resp_error) begin
                            complete_fault();
                        end else begin
                            a_mem_q[row_q[3:0]][k_q[3:0]] <=
                                data_rd.resp_data[(active_byte_lane * 8) +: INPUT_W];
                            if ((k_q + 5'd1) == 5'(active_k)) begin
                                k_q <= 5'd0;
                                if ((row_q + 5'd1) == 5'(active_m)) begin
                                    row_q <= 5'd0;
                                    col_q <= 5'd0;
                                    state_q <= STATE_LOAD_B_REQ;
                                end else begin
                                    row_q <= row_q + 5'd1;
                                    state_q <= STATE_LOAD_A_REQ;
                                end
                            end else begin
                                k_q <= k_q + 5'd1;
                                state_q <= STATE_LOAD_A_REQ;
                            end
                        end
                    end
                end

                STATE_LOAD_B_REQ: begin
                    if (data_rd.req_ready) state_q <= STATE_LOAD_B_WAIT;
                end

                STATE_LOAD_B_WAIT: begin
                    if (data_rd.resp_valid) begin
                        if (data_rd.resp_error) begin
                            complete_fault();
                        end else begin
                            b_mem_q[k_q[3:0]][col_q[3:0]] <=
                                data_rd.resp_data[(active_byte_lane * 8) +: INPUT_W];
                            if ((col_q + 5'd1) == 5'(active_n)) begin
                                col_q <= 5'd0;
                                if ((k_q + 5'd1) == 5'(active_k)) begin
                                    k_q <= 5'd0;
                                    state_q <= STATE_LOAD_WEIGHT;
                                end else begin
                                    k_q <= k_q + 5'd1;
                                    state_q <= STATE_LOAD_B_REQ;
                                end
                            end else begin
                                col_q <= col_q + 5'd1;
                                state_q <= STATE_LOAD_B_REQ;
                            end
                        end
                    end
                end

                STATE_LOAD_WEIGHT: begin
                    if ((k_q + 5'd1) == 5'(active_k)) begin
                        k_q <= 5'd0;
                        compute_cycle_q <= 6'd0;
                        state_q <= STATE_COMPUTE;
                    end else begin
                        k_q <= k_q + 5'd1;
                    end
                end

                STATE_COMPUTE: begin
                    if (compute_cycle_q == 6'(SYSTOLIC_CYCLES - 1)) begin
                        state_q <= STATE_DRAIN;
                    end else begin
                        compute_cycle_q <= compute_cycle_q + 6'd1;
                    end
                end

                STATE_DRAIN: begin
                    row_q <= 5'd0;
                    col_q <= 5'd0;
                    if (command_store) state_q <= STATE_STORE_REQ;
                    else complete_ok();
                end

                STATE_STORE_REQ: begin
                    if (data_wr.req_ready) state_q <= STATE_STORE_WAIT;
                end

                STATE_STORE_WAIT: begin
                    if (data_wr.resp_valid) begin
                        if (data_wr.resp_error) begin
                            complete_fault();
                        end else if ((col_q + 5'd1) == 5'(active_n)) begin
                            col_q <= 5'd0;
                            if ((row_q + 5'd1) == 5'(active_m)) begin
                                complete_ok();
                            end else begin
                                row_q <= row_q + 5'd1;
                                state_q <= STATE_STORE_REQ;
                            end
                        end else begin
                            col_q <= col_q + 5'd1;
                            state_q <= STATE_STORE_REQ;
                        end
                    end
                end

                STATE_EVENT: begin
                    if (result.ready) state_q <= STATE_IDLE;
                end

                default: complete_fault();
            endcase
        end
    end

    v2_matrix_issue_stable: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && !issue.ready |=> issue.valid && $stable(issue.data)
    );
    v2_matrix_store_only_after_valid_accumulator: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            data_wr.req_valid |-> accumulator_valid_q && command_store
    );
    v2_matrix_result_fault_has_code: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            result.valid && result.data[0] |-> (result.data[63:32] != NPU_V2_FAULT_NONE)
    );
    v2_matrix_clear_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            state_q == STATE_VALIDATE && command_valid && command_clear
    );
    v2_matrix_accumulate_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            state_q == STATE_VALIDATE && command_valid && command_accumulate
    );
    v2_matrix_store_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            data_wr.req_valid && data_wr.req_ready
    );

endmodule

/* verilator lint_on DECLFILENAME */
