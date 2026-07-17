/* verilator lint_off DECLFILENAME */

module npu_v2_vector_engine_core #(
    parameter int unsigned MAX_VL = 16,
    parameter int unsigned REG_COUNT = 16,
    parameter int unsigned PRED_COUNT = 1
) (
    input  logic                clk_i,
    input  logic                rst_ni,
    input  logic                soft_reset_i,
    input  logic [31:0]         local_mem_bytes_i,

    npu_vr_if.sink              issue,
    npu_vr_if.source            result,

    npu_v2_localmem_rd_if.master data_rd,
    npu_v2_localmem_wr_if.master data_wr
);

    import npu_v2_pkg::*;
    import npu_isa_pkg::*;

    localparam int unsigned PRED_INDEX_W = PRED_COUNT > 1 ? $clog2(PRED_COUNT) : 1;

    typedef enum logic [3:0] {
        STATE_IDLE,
        STATE_LOAD_REQ,
        STATE_LOAD_WAIT,
        STATE_STORE_REQ,
        STATE_STORE_WAIT,
        STATE_PRED_LOAD_REQ,
        STATE_PRED_LOAD_WAIT,
        STATE_QUANT_PARAM_REQ,
        STATE_QUANT_PARAM_WAIT,
        STATE_QUANT_EXEC,
        STATE_EVENT
    } state_e;

    state_e state_q;

    logic [31:0] vector_reg_q [REG_COUNT][MAX_VL];
    logic        predicate_reg_q [PRED_COUNT][MAX_VL];
    logic [15:0] vl_q;
    logic [1:0]  sew_q;
    logic        signed_q;
    logic        saturate_q;
    logic [31:0] inst_q;
    logic [15:0] lane_q;
    logic        event_fault_q;
    logic [31:0] event_fault_code_q;
    logic [2:0]  quant_word_index_q;
    logic [31:0] quant_word_q [6];

    logic [31:0] instruction_class;
    logic [3:0]  instruction_opcode;
    logic [3:0]  instruction_rd;
    logic [3:0]  instruction_rs1;
    logic [3:0]  instruction_rs2;
    logic [11:0] instruction_imm;
    logic        instruction_reserved_zero;

    logic [3:0]  active_rd;
    logic [3:0]  active_rs1;
    logic [PRED_INDEX_W-1:0] active_predicate;
    logic [11:0] active_imm;
    logic [31:0] active_lane_addr;
    logic [31:0] active_word_addr;
    logic [31:0] active_store_data;
    logic [3:0]  active_store_strb;
    logic [15:0] configured_vl;
    logic [1:0]  configured_sew;
    logic [1:0]  configured_rounding;
    logic [3:0]  instruction_predicate;
    logic        instruction_predicate_reserved_zero;
    logic        active_lane_enabled;
    logic [31:0] quant_param_addr;

    assign instruction_class = issue.data[31:0] & NPU_ISA_CLASS_MASK;
    assign instruction_opcode = 4'((issue.data[31:0] >> NPU_ISA_OPCODE_SHIFT) & NPU_ISA_FIELD_MASK);
    assign instruction_rd = 4'((issue.data[31:0] >> NPU_ISA_RD_SHIFT) & NPU_ISA_FIELD_MASK);
    assign instruction_rs1 = 4'((issue.data[31:0] >> NPU_ISA_RS1_SHIFT) & NPU_ISA_FIELD_MASK);
    assign instruction_rs2 = 4'((issue.data[31:0] >> NPU_ISA_RS2_SHIFT) & NPU_ISA_FIELD_MASK);
    assign instruction_imm = 12'(issue.data[31:0] & NPU_ISA_IMM_MASK);
    assign instruction_reserved_zero = (issue.data[127:32] == 96'd0);

    assign active_rd = 4'((inst_q >> NPU_ISA_RD_SHIFT) & NPU_ISA_FIELD_MASK);
    assign active_rs1 = 4'((inst_q >> NPU_ISA_RS1_SHIFT) & NPU_ISA_FIELD_MASK);
    assign active_predicate = PRED_INDEX_W'((inst_q >> NPU_ISA_RS2_SHIFT) & NPU_ISA_FIELD_MASK);
    assign active_imm = 12'(inst_q & NPU_ISA_IMM_MASK);
    assign active_lane_addr = {20'd0, active_imm} + (32'(lane_q) << sew_q);
    assign active_word_addr = {active_lane_addr[31:2], 2'b00};
    assign quant_param_addr = {20'd0, active_imm} + (32'(quant_word_index_q) * 32'd4);
    assign configured_vl = {10'd0, instruction_imm[5:0]} + 16'd1;
    assign configured_sew = 2'((32'(instruction_imm) & NPU_ISA_VTYPE_SEW_MASK) >>
                               NPU_ISA_VTYPE_SEW_SHIFT);
    assign configured_rounding = 2'((32'(instruction_imm) & NPU_ISA_VTYPE_ROUND_MASK) >>
                                    NPU_ISA_VTYPE_ROUND_SHIFT);
    assign instruction_predicate = 4'(32'(instruction_imm) & NPU_ISA_VECTOR_PREDICATE_MASK);
    assign instruction_predicate_reserved_zero =
        (32'(instruction_imm) & NPU_ISA_VECTOR_PREDICATE_RESERVED_MASK) == 32'd0;
    always_comb begin
        active_lane_enabled = 1'b0;
        if (predicate_ok(active_rs1) && (int'(lane_q) < MAX_VL)) begin
            active_lane_enabled = predicate_reg_q[PRED_INDEX_W'(active_rs1)]
                                                  [lane_q[$clog2(MAX_VL)-1:0]];
        end
    end

    assign issue.ready = (state_q == STATE_IDLE);
    assign result.valid = (state_q == STATE_EVENT);
    assign result.data = {event_fault_code_q, 31'd0, event_fault_q};

    assign data_rd.req_valid = ((state_q == STATE_LOAD_REQ) && active_lane_enabled) ||
                               (state_q == STATE_PRED_LOAD_REQ) ||
                               (state_q == STATE_QUANT_PARAM_REQ);
    assign data_rd.req_addr = (state_q == STATE_PRED_LOAD_REQ)
        ? {20'd0, active_imm}
        : (state_q == STATE_QUANT_PARAM_REQ) ? quant_param_addr : active_word_addr;

    assign data_wr.req_valid = (state_q == STATE_STORE_REQ) && active_lane_enabled;
    assign data_wr.req_addr = active_word_addr;
    assign data_wr.req_data = active_store_data;
    assign data_wr.req_strb = active_store_strb;

    always_comb begin
        active_store_data = 32'd0;
        active_store_strb = 4'd0;
        unique case (sew_q)
            2'd0: begin
                active_store_data = vector_reg_q[active_rd][lane_q[$clog2(MAX_VL)-1:0]] <<
                                    (active_lane_addr[1:0] * 8);
                active_store_strb = 4'b0001 << active_lane_addr[1:0];
            end
            2'd1: begin
                active_store_data = vector_reg_q[active_rd][lane_q[$clog2(MAX_VL)-1:0]] <<
                                    (active_lane_addr[1] * 16);
                active_store_strb = active_lane_addr[1] ? 4'b1100 : 4'b0011;
            end
            default: begin
                active_store_data = vector_reg_q[active_rd][lane_q[$clog2(MAX_VL)-1:0]];
                active_store_strb = 4'hF;
            end
        endcase
    end

    function automatic logic register_ok(input logic [3:0] index);
        register_ok = (int'(index) < REG_COUNT);
    endfunction

    function automatic logic predicate_ok(input logic [3:0] index);
        predicate_ok = (int'(index) < PRED_COUNT);
    endfunction

    function automatic logic vl_ok(input logic [15:0] vl);
        vl_ok = (vl != 16'd0) && (int'(vl) <= MAX_VL);
    endfunction

    function automatic logic predicate_load_range_ok(input logic [11:0] base);
        logic [31:0] end_addr;
        begin
            end_addr = {20'd0, base} + NPU_ISA_PREDICATE_WORD_BYTES;
            predicate_load_range_ok = (base[1:0] == 2'b00) &&
                                      (end_addr <= local_mem_bytes_i) &&
                                      (end_addr >= {20'd0, base});
        end
    endfunction

    function automatic logic local_range_ok(
        input logic [11:0] base,
        input logic [15:0] vl,
        input logic [1:0] sew
    );
        logic [31:0] byte_count;
        logic [31:0] end_addr;
        logic [2:0] alignment_mask;
        begin
            byte_count = 32'(vl) << sew;
            end_addr = {20'd0, base} + byte_count;
            alignment_mask = (3'd1 << sew) - 3'd1;
            local_range_ok = ((base[2:0] & alignment_mask) == 3'd0) &&
                             (end_addr <= local_mem_bytes_i) &&
                             (end_addr >= {20'd0, base});
        end
    endfunction

    function automatic logic quant_command_range_ok(input logic [11:0] base);
        logic [31:0] end_addr;
        begin
            end_addr = {20'd0, base} + NPU_ISA_QUANT_COMMAND_BYTES;
            quant_command_range_ok =
                ((32'(base) & (NPU_ISA_QUANT_COMMAND_ALIGN - 1)) == 32'd0) &&
                (end_addr <= local_mem_bytes_i) &&
                (end_addr >= {20'd0, base});
        end
    endfunction

    function automatic logic [31:0] canonicalize(
        input logic [31:0] value,
        input logic [1:0] sew,
        input logic is_signed
    );
        unique case (sew)
            2'd0: canonicalize = is_signed ? {{24{value[7]}}, value[7:0]} : {24'd0, value[7:0]};
            2'd1: canonicalize = is_signed ? {{16{value[15]}}, value[15:0]} : {16'd0, value[15:0]};
            default: canonicalize = value;
        endcase
    endfunction

    function automatic logic [31:0] loaded_element(
        input logic [31:0] word,
        input logic [1:0] byte_offset,
        input logic [1:0] sew,
        input logic is_signed
    );
        logic [31:0] shifted;
        begin
            shifted = word >> (byte_offset * 8);
            loaded_element = canonicalize(shifted, sew, is_signed);
        end
    endfunction

    function automatic logic [31:0] saturating_add_sub(
        input logic [3:0] op,
        input logic [31:0] lhs,
        input logic [31:0] rhs,
        input logic [1:0] sew,
        input logic is_signed
    );
        logic signed [32:0] lhs_s;
        logic signed [32:0] rhs_s;
        logic signed [32:0] result_s;
        logic signed [32:0] minimum_s;
        logic signed [32:0] maximum_s;
        logic [32:0] lhs_u;
        logic [32:0] rhs_u;
        logic [32:0] result_u;
        logic [32:0] maximum_u;
        begin
            lhs_s = {lhs[31], lhs};
            rhs_s = {rhs[31], rhs};
            lhs_u = {1'b0, lhs};
            rhs_u = {1'b0, rhs};
            unique case (sew)
                2'd0: begin
                    minimum_s = -33'sd128;
                    maximum_s = 33'sd127;
                    maximum_u = 33'h0_0000_00FF;
                end
                2'd1: begin
                    minimum_s = -33'sd32768;
                    maximum_s = 33'sd32767;
                    maximum_u = 33'h0_0000_FFFF;
                end
                default: begin
                    minimum_s = -33'sd2147483648;
                    maximum_s = 33'sd2147483647;
                    maximum_u = 33'h0_FFFF_FFFF;
                end
            endcase

            result_s = (op == NPU_ISA_OPCODE_VECTOR_ALU_SUB)
                ? lhs_s - rhs_s : lhs_s + rhs_s;
            if (op == NPU_ISA_OPCODE_VECTOR_ALU_SUB) begin
                result_u = (lhs_u < rhs_u) ? 33'd0 : lhs_u - rhs_u;
            end else begin
                result_u = lhs_u + rhs_u;
                if (result_u > maximum_u) begin
                    result_u = maximum_u;
                end
            end

            if (is_signed) begin
                if (result_s < minimum_s) begin
                    saturating_add_sub = minimum_s[31:0];
                end else if (result_s > maximum_s) begin
                    saturating_add_sub = maximum_s[31:0];
                end else begin
                    saturating_add_sub = result_s[31:0];
                end
            end else begin
                saturating_add_sub = result_u[31:0];
            end
        end
    endfunction

    function automatic logic [31:0] alu_result(
        input logic [3:0] op,
        input logic [31:0] lhs,
        input logic [31:0] rhs,
        input logic [1:0] sew,
        input logic is_signed,
        input logic saturate
    );
        logic signed [31:0] lhs_s;
        logic signed [31:0] rhs_s;
        logic [4:0] shift_amount;
        logic [31:0] element_mask;
        logic [31:0] raw_result;
        begin
            lhs_s = signed'(lhs);
            rhs_s = signed'(rhs);
            unique case (sew)
                2'd0: begin
                    shift_amount = rhs[4:0] & 5'd7;
                    element_mask = 32'h0000_00FF;
                end
                2'd1: begin
                    shift_amount = rhs[4:0] & 5'd15;
                    element_mask = 32'h0000_FFFF;
                end
                default: begin
                    shift_amount = rhs[4:0] & 5'd31;
                    element_mask = 32'hFFFF_FFFF;
                end
            endcase
            raw_result = 32'd0;
            unique case (op)
                NPU_ISA_OPCODE_VECTOR_ALU_ADD,
                NPU_ISA_OPCODE_VECTOR_ALU_SUB: raw_result = saturate
                    ? saturating_add_sub(op, lhs, rhs, sew, is_signed)
                    : ((op == NPU_ISA_OPCODE_VECTOR_ALU_ADD) ? lhs + rhs : lhs - rhs);
                NPU_ISA_OPCODE_VECTOR_ALU_MIN: raw_result = is_signed
                    ? ((lhs_s < rhs_s) ? lhs : rhs)
                    : ((lhs < rhs) ? lhs : rhs);
                NPU_ISA_OPCODE_VECTOR_ALU_MAX: raw_result = is_signed
                    ? ((lhs_s > rhs_s) ? lhs : rhs)
                    : ((lhs > rhs) ? lhs : rhs);
                NPU_ISA_OPCODE_VECTOR_ALU_EQ: raw_result = (lhs == rhs) ? 32'd1 : 32'd0;
                NPU_ISA_OPCODE_VECTOR_ALU_LT: raw_result = is_signed
                    ? ((lhs_s < rhs_s) ? 32'd1 : 32'd0)
                    : ((lhs < rhs) ? 32'd1 : 32'd0);
                NPU_ISA_OPCODE_VECTOR_ALU_SHL: raw_result = lhs << shift_amount;
                NPU_ISA_OPCODE_VECTOR_ALU_SRL: raw_result = (lhs & element_mask) >> shift_amount;
                NPU_ISA_OPCODE_VECTOR_ALU_SRA: raw_result = unsigned'(lhs_s >>> shift_amount);
                default: raw_result = 32'd0;
            endcase
            alu_result = canonicalize(raw_result, sew, is_signed);
        end
    endfunction

    function automatic logic vector_permute_opcode_ok(input logic [3:0] op);
        vector_permute_opcode_ok = (op == NPU_ISA_OPCODE_VECTOR_PERMUTE_GATHER) ||
                                   (op == NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_LO) ||
                                   (op == NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_HI) ||
                                   (op == NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_EVEN) ||
                                   (op == NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_ODD) ||
                                   (op == NPU_ISA_OPCODE_VECTOR_PERMUTE_TRANSPOSE4);
    endfunction

    function automatic logic vector_alu_opcode_ok(input logic [3:0] op);
        vector_alu_opcode_ok = (op == NPU_ISA_OPCODE_VECTOR_ALU_ADD) ||
                               (op == NPU_ISA_OPCODE_VECTOR_ALU_SUB) ||
                               (op == NPU_ISA_OPCODE_VECTOR_ALU_MIN) ||
                               (op == NPU_ISA_OPCODE_VECTOR_ALU_MAX) ||
                               (op == NPU_ISA_OPCODE_VECTOR_ALU_EQ) ||
                               (op == NPU_ISA_OPCODE_VECTOR_ALU_LT) ||
                               (op == NPU_ISA_OPCODE_VECTOR_ALU_SHL) ||
                               (op == NPU_ISA_OPCODE_VECTOR_ALU_SRL) ||
                               (op == NPU_ISA_OPCODE_VECTOR_ALU_SRA) ||
                               (op == NPU_ISA_OPCODE_VECTOR_ALU_SELECT);
    endfunction

    function automatic logic gather_indices_ok(
        input logic [3:0] index_reg,
        input logic [PRED_INDEX_W-1:0] predicate
    );
        logic indices_ok;
        begin
            indices_ok = 1'b1;
            for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                if ((lane_index < int'(vl_q)) &&
                    predicate_reg_q[predicate][lane_index] &&
                    (unsigned'(vector_reg_q[index_reg][lane_index]) >= 32'(vl_q))) begin
                    indices_ok = 1'b0;
                end
            end
            gather_indices_ok = indices_ok;
        end
    endfunction

    function automatic logic [31:0] reduction_result(
        input logic [3:0] op,
        input logic [3:0] source_reg,
        input logic [PRED_INDEX_W-1:0] predicate
    );
        logic [31:0] accumulator;
        logic [31:0] value;
        logic [31:0] unsigned_max;
        logic signed [31:0] accumulator_s;
        logic signed [31:0] value_s;
        begin
            unique case (sew_q)
                2'd0: unsigned_max = 32'h0000_00FF;
                2'd1: unsigned_max = 32'h0000_FFFF;
                default: unsigned_max = 32'hFFFF_FFFF;
            endcase
            if (op == NPU_ISA_OPCODE_VECTOR_REDUCTION_SUM) begin
                accumulator = 32'd0;
            end else if (op == NPU_ISA_OPCODE_VECTOR_REDUCTION_MIN) begin
                accumulator = signed_q ? (unsigned_max >> 1) : unsigned_max;
            end else begin
                accumulator = signed_q ? ~(unsigned_max >> 1) : 32'd0;
            end

            for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                if ((lane_index < int'(vl_q)) &&
                    predicate_reg_q[predicate][lane_index]) begin
                    value = vector_reg_q[source_reg][lane_index];
                    accumulator_s = signed'(accumulator);
                    value_s = signed'(value);
                    if (op == NPU_ISA_OPCODE_VECTOR_REDUCTION_SUM) begin
                        accumulator = canonicalize(accumulator + value, sew_q, signed_q);
                    end else if (op == NPU_ISA_OPCODE_VECTOR_REDUCTION_MIN) begin
                        accumulator = signed_q
                            ? ((value_s < accumulator_s) ? value : accumulator)
                            : ((value < accumulator) ? value : accumulator);
                    end else begin
                        accumulator = signed_q
                            ? ((value_s > accumulator_s) ? value : accumulator)
                            : ((value > accumulator) ? value : accumulator);
                    end
                end
            end
            reduction_result = canonicalize(accumulator, sew_q, signed_q);
        end
    endfunction

    function automatic logic [31:0] requantize_result(
        input logic [31:0] value,
        input logic [31:0] multiplier,
        input logic [4:0]  shift,
        input logic [31:0] zero_point,
        input logic [31:0] clamp_min,
        input logic [31:0] clamp_max,
        input logic [1:0]  sew,
        input logic        is_signed
    );
        logic signed [63:0] source_wide;
        logic signed [63:0] multiplier_wide;
        logic signed [63:0] product;
        logic signed [63:0] rounded;
        logic signed [63:0] adjusted;
        logic signed [63:0] zero_point_wide;
        logic signed [63:0] clamp_min_wide;
        logic signed [63:0] clamp_max_wide;
        logic signed [31:0] clamped;
        logic [63:0] magnitude;
        logic [63:0] quotient;
        logic [63:0] remainder_mask;
        logic [63:0] remainder;
        logic [63:0] halfway;
        begin
            source_wide = is_signed ? {{32{value[31]}}, value} : {32'd0, value};
            multiplier_wide = {{32{multiplier[31]}}, multiplier};
            zero_point_wide = {{32{zero_point[31]}}, zero_point};
            clamp_min_wide = {{32{clamp_min[31]}}, clamp_min};
            clamp_max_wide = {{32{clamp_max[31]}}, clamp_max};
            product = source_wide * multiplier_wide;
            if (shift == 5'd0) begin
                rounded = product;
            end else begin
                magnitude = product[63] ? unsigned'(-product) : unsigned'(product);
                quotient = magnitude >> shift;
                remainder_mask = (64'd1 << shift) - 64'd1;
                remainder = magnitude & remainder_mask;
                halfway = 64'd1 << (shift - 5'd1);
                if ((remainder > halfway) ||
                    ((remainder == halfway) && quotient[0])) begin
                    quotient = quotient + 64'd1;
                end
                rounded = product[63] ? -signed'(quotient) : signed'(quotient);
            end
            adjusted = rounded + zero_point_wide;
            if (adjusted < clamp_min_wide) begin
                clamped = signed'(clamp_min);
            end else if (adjusted > clamp_max_wide) begin
                clamped = signed'(clamp_max);
            end else begin
                clamped = adjusted[31:0];
            end
            requantize_result = canonicalize(clamped, sew, is_signed);
        end
    endfunction

    task automatic complete_ok();
        begin
            event_fault_q <= 1'b0;
            event_fault_code_q <= NPU_V2_FAULT_NONE;
            state_q <= STATE_EVENT;
        end
    endtask

    task automatic complete_fault(input logic [31:0] code);
        begin
            event_fault_q <= 1'b1;
            event_fault_code_q <= code;
            state_q <= STATE_EVENT;
        end
    endtask

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state_q <= STATE_IDLE;
            vl_q <= 16'd0;
            sew_q <= 2'd2;
            signed_q <= 1'b1;
            saturate_q <= 1'b0;
            inst_q <= 32'd0;
            lane_q <= 16'd0;
            quant_word_index_q <= 3'd0;
            event_fault_q <= 1'b0;
            event_fault_code_q <= NPU_V2_FAULT_NONE;
            for (int reg_index = 0; reg_index < REG_COUNT; reg_index++) begin
                for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                    vector_reg_q[reg_index][lane_index] <= 32'd0;
                end
            end
            for (int pred_index = 0; pred_index < PRED_COUNT; pred_index++) begin
                for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                    predicate_reg_q[pred_index][lane_index] <= 1'b1;
                end
            end
        end else if (soft_reset_i) begin
            state_q <= STATE_IDLE;
            vl_q <= 16'd0;
            sew_q <= 2'd2;
            signed_q <= 1'b1;
            saturate_q <= 1'b0;
            inst_q <= 32'd0;
            lane_q <= 16'd0;
            quant_word_index_q <= 3'd0;
            event_fault_q <= 1'b0;
            event_fault_code_q <= NPU_V2_FAULT_NONE;
            for (int pred_index = 0; pred_index < PRED_COUNT; pred_index++) begin
                for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                    predicate_reg_q[pred_index][lane_index] <= 1'b1;
                end
            end
        end else begin
            unique case (state_q)
                STATE_IDLE: begin
                    if (issue.valid && issue.ready) begin
                        inst_q <= issue.data[31:0];
                        lane_q <= 16'd0;
                        if (!instruction_reserved_zero) begin
                            complete_fault(NPU_V2_FAULT_ILLEGAL_INSTRUCTION);
                        end else if (instruction_class == NPU_ISA_CLASS_PREDICATE) begin
                            if (!predicate_ok(instruction_rd) ||
                                (instruction_rs1 != 4'd0) ||
                                (instruction_rs2 != 4'd0)) begin
                                complete_fault(NPU_V2_FAULT_ILLEGAL_INSTRUCTION);
                            end else if (instruction_opcode == NPU_ISA_OPCODE_PREDICATE_PTRUE) begin
                                if (!vl_ok(vl_q) || (instruction_imm != 12'd0)) begin
                                    complete_fault(NPU_V2_FAULT_VECTOR_CONFIG);
                                end else begin
                                    for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                                        predicate_reg_q[PRED_INDEX_W'(instruction_rd)][lane_index] <=
                                            lane_index < int'(vl_q);
                                    end
                                    complete_ok();
                                end
                            end else if (instruction_opcode == NPU_ISA_OPCODE_PREDICATE_LOAD) begin
                                if (!predicate_load_range_ok(instruction_imm)) begin
                                    complete_fault(NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);
                                end else begin
                                    state_q <= STATE_PRED_LOAD_REQ;
                                end
                            end else begin
                                complete_fault(NPU_V2_FAULT_ILLEGAL_INSTRUCTION);
                            end
                        end else if (instruction_class == NPU_ISA_CLASS_VECTOR_CONFIG) begin
                            if ((instruction_opcode == NPU_ISA_OPCODE_VECTOR_CONFIG_SET) &&
                                (instruction_rd == 4'd0) &&
                                (instruction_rs1 == 4'd0) &&
                                (instruction_rs2 == 4'd0) &&
                                vl_ok(configured_vl) &&
                                (configured_sew <= 2'(NPU_ISA_VTYPE_SEW_32)) &&
                                (configured_rounding == 2'(NPU_ISA_VTYPE_ROUND_RNE)) &&
                                ((32'(instruction_imm) &
                                  ~(NPU_ISA_VTYPE_VL_MINUS_ONE_MASK |
                                    NPU_ISA_VTYPE_SEW_MASK |
                                    NPU_ISA_VTYPE_SIGNED |
                                    NPU_ISA_VTYPE_ROUND_MASK |
                                    NPU_ISA_VTYPE_SATURATE)) == 32'd0)) begin
                                vl_q <= configured_vl;
                                sew_q <= configured_sew;
                                signed_q <= (32'(instruction_imm) & NPU_ISA_VTYPE_SIGNED) != 32'd0;
                                saturate_q <= (32'(instruction_imm) & NPU_ISA_VTYPE_SATURATE) != 32'd0;
                                complete_ok();
                            end else begin
                                complete_fault(NPU_V2_FAULT_VECTOR_CONFIG);
                            end
                        end else if (instruction_class == NPU_ISA_CLASS_VECTOR_ALU) begin
                            if (!vl_ok(vl_q) ||
                                !register_ok(instruction_rd) ||
                                !register_ok(instruction_rs1) ||
                                !register_ok(instruction_rs2) ||
                                !predicate_ok(instruction_predicate) ||
                                !instruction_predicate_reserved_zero ||
                                !vector_alu_opcode_ok(instruction_opcode)) begin
                                complete_fault(NPU_V2_FAULT_ILLEGAL_INSTRUCTION);
                            end else begin
                                for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                                    if ((instruction_opcode == NPU_ISA_OPCODE_VECTOR_ALU_SELECT) &&
                                        (lane_index < int'(vl_q))) begin
                                        vector_reg_q[instruction_rd][lane_index] <=
                                            predicate_reg_q[PRED_INDEX_W'(instruction_predicate)][lane_index]
                                            ? vector_reg_q[instruction_rs1][lane_index]
                                            : vector_reg_q[instruction_rs2][lane_index];
                                    end else if ((lane_index < int'(vl_q)) &&
                                        predicate_reg_q[PRED_INDEX_W'(instruction_predicate)][lane_index]) begin
                                        vector_reg_q[instruction_rd][lane_index] <= alu_result(
                                            instruction_opcode,
                                            vector_reg_q[instruction_rs1][lane_index],
                                            vector_reg_q[instruction_rs2][lane_index],
                                            sew_q,
                                            signed_q,
                                            saturate_q
                                        );
                                    end
                                end
                                complete_ok();
                            end
                        end else if (instruction_class == NPU_ISA_CLASS_VECTOR_PERMUTE) begin
                            if (!vector_permute_opcode_ok(instruction_opcode) ||
                                !vl_ok(vl_q) ||
                                !register_ok(instruction_rd) ||
                                !register_ok(instruction_rs1) ||
                                !register_ok(instruction_rs2) ||
                                !predicate_ok(instruction_predicate) ||
                                !instruction_predicate_reserved_zero ||
                                ((instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_GATHER) &&
                                 !gather_indices_ok(
                                     instruction_rs2,
                                     PRED_INDEX_W'(instruction_predicate)
                                 )) ||
                                (((instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_LO) ||
                                  (instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_HI) ||
                                  (instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_EVEN) ||
                                  (instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_ODD)) &&
                                 vl_q[0]) ||
                                ((instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_TRANSPOSE4) &&
                                 ((vl_q != 16'd16) || (instruction_rs2 != 4'd0)))) begin
                                complete_fault(NPU_V2_FAULT_VECTOR_CONFIG);
                            end else begin
                                for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                                    if ((lane_index < int'(vl_q)) &&
                                        predicate_reg_q[PRED_INDEX_W'(instruction_predicate)][lane_index]) begin
                                        unique case (instruction_opcode)
                                            NPU_ISA_OPCODE_VECTOR_PERMUTE_GATHER:
                                                vector_reg_q[instruction_rd][lane_index] <=
                                                    vector_reg_q[instruction_rs1]
                                                        [vector_reg_q[instruction_rs2][lane_index]
                                                            [$clog2(MAX_VL)-1:0]];
                                            NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_LO,
                                            NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_HI:
                                                vector_reg_q[instruction_rd][lane_index] <= lane_index[0]
                                                    ? vector_reg_q[instruction_rs2]
                                                        [(lane_index / 2) +
                                                         ((instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_HI)
                                                          ? (int'(vl_q) / 2) : 0)]
                                                    : vector_reg_q[instruction_rs1]
                                                        [(lane_index / 2) +
                                                         ((instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_HI)
                                                          ? (int'(vl_q) / 2) : 0)];
                                            NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_EVEN,
                                            NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_ODD:
                                                vector_reg_q[instruction_rd][lane_index] <=
                                                    (lane_index < (int'(vl_q) / 2))
                                                    ? vector_reg_q[instruction_rs1]
                                                        [(2 * lane_index) +
                                                         ((instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_ODD)
                                                          ? 1 : 0)]
                                                    : vector_reg_q[instruction_rs2]
                                                        [(2 * (lane_index - (int'(vl_q) / 2))) +
                                                         ((instruction_opcode == NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_ODD)
                                                          ? 1 : 0)];
                                            NPU_ISA_OPCODE_VECTOR_PERMUTE_TRANSPOSE4:
                                                vector_reg_q[instruction_rd][lane_index] <=
                                                    vector_reg_q[instruction_rs1]
                                                        [((lane_index % 4) * 4) + (lane_index / 4)];
                                            default: begin
                                            end
                                        endcase
                                    end
                                end
                                complete_ok();
                            end
                        end else if (instruction_class == NPU_ISA_CLASS_VECTOR_REDUCTION) begin
                            if (!vl_ok(vl_q) ||
                                !register_ok(instruction_rd) ||
                                !register_ok(instruction_rs1) ||
                                (instruction_rs2 != 4'd0) ||
                                !predicate_ok(instruction_predicate) ||
                                !instruction_predicate_reserved_zero ||
                                ((instruction_opcode != NPU_ISA_OPCODE_VECTOR_REDUCTION_SUM) &&
                                 (instruction_opcode != NPU_ISA_OPCODE_VECTOR_REDUCTION_MIN) &&
                                 (instruction_opcode != NPU_ISA_OPCODE_VECTOR_REDUCTION_MAX))) begin
                                complete_fault(NPU_V2_FAULT_VECTOR_CONFIG);
                            end else begin
                                vector_reg_q[instruction_rd][0] <= reduction_result(
                                    instruction_opcode,
                                    instruction_rs1,
                                    PRED_INDEX_W'(instruction_predicate)
                                );
                                complete_ok();
                            end
                        end else if (instruction_class == NPU_ISA_CLASS_QUANTIZATION) begin
                            if ((instruction_opcode != NPU_ISA_OPCODE_QUANTIZATION_REQUANTIZE) ||
                                !vl_ok(vl_q) ||
                                !register_ok(instruction_rd) ||
                                !register_ok(instruction_rs1) ||
                                !predicate_ok(instruction_rs2) ||
                                !quant_command_range_ok(instruction_imm)) begin
                                complete_fault(NPU_V2_FAULT_VECTOR_CONFIG);
                            end else begin
                                quant_word_index_q <= 3'd0;
                                state_q <= STATE_QUANT_PARAM_REQ;
                            end
                        end else if (instruction_class == NPU_ISA_CLASS_VECTOR_MEMORY) begin
                            if (!vl_ok(vl_q) ||
                                !register_ok(instruction_rd) ||
                                !predicate_ok(instruction_rs1) ||
                                (instruction_rs2 != 4'd0) ||
                                !local_range_ok(instruction_imm, vl_q, sew_q)) begin
                                complete_fault(NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);
                            end else if (instruction_opcode == NPU_ISA_OPCODE_VECTOR_MEMORY_LOAD) begin
                                state_q <= STATE_LOAD_REQ;
                            end else if (instruction_opcode == NPU_ISA_OPCODE_VECTOR_MEMORY_STORE) begin
                                state_q <= STATE_STORE_REQ;
                            end else begin
                                complete_fault(NPU_V2_FAULT_ILLEGAL_INSTRUCTION);
                            end
                        end else begin
                            complete_fault(NPU_V2_FAULT_ILLEGAL_INSTRUCTION);
                        end
                    end
                end

                STATE_LOAD_REQ: begin
                    if (!active_lane_enabled) begin
                        if ((lane_q + 16'd1) == vl_q) begin
                            complete_ok();
                        end else begin
                            lane_q <= lane_q + 16'd1;
                        end
                    end else if (data_rd.req_ready) begin
                        state_q <= STATE_LOAD_WAIT;
                    end
                end

                STATE_LOAD_WAIT: begin
                    if (data_rd.resp_valid) begin
                        if (data_rd.resp_error) begin
                            complete_fault(NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);
                        end else begin
                            vector_reg_q[active_rd][lane_q[$clog2(MAX_VL)-1:0]] <= loaded_element(
                                data_rd.resp_data,
                                active_lane_addr[1:0],
                                sew_q,
                                signed_q
                            );
                            if ((lane_q + 16'd1) == vl_q) begin
                                complete_ok();
                            end else begin
                                lane_q <= lane_q + 16'd1;
                                state_q <= STATE_LOAD_REQ;
                            end
                        end
                    end
                end

                STATE_STORE_REQ: begin
                    if (!active_lane_enabled) begin
                        if ((lane_q + 16'd1) == vl_q) begin
                            complete_ok();
                        end else begin
                            lane_q <= lane_q + 16'd1;
                        end
                    end else if (data_wr.req_ready) begin
                        state_q <= STATE_STORE_WAIT;
                    end
                end

                STATE_STORE_WAIT: begin
                    if (data_wr.resp_valid) begin
                        if (data_wr.resp_error) begin
                            complete_fault(NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);
                        end else if ((lane_q + 16'd1) == vl_q) begin
                            complete_ok();
                        end else begin
                            lane_q <= lane_q + 16'd1;
                            state_q <= STATE_STORE_REQ;
                        end
                    end
                end

                STATE_PRED_LOAD_REQ: begin
                    if (data_rd.req_ready) begin
                        state_q <= STATE_PRED_LOAD_WAIT;
                    end
                end

                STATE_PRED_LOAD_WAIT: begin
                    if (data_rd.resp_valid) begin
                        if (data_rd.resp_error) begin
                            complete_fault(NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);
                        end else begin
                            for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                                predicate_reg_q[PRED_INDEX_W'(active_rd)][lane_index] <=
                                    data_rd.resp_data[lane_index];
                            end
                            complete_ok();
                        end
                    end
                end

                STATE_QUANT_PARAM_REQ: begin
                    if (data_rd.req_ready) begin
                        state_q <= STATE_QUANT_PARAM_WAIT;
                    end
                end

                STATE_QUANT_PARAM_WAIT: begin
                    if (data_rd.resp_valid) begin
                        if (data_rd.resp_error) begin
                            complete_fault(NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);
                        end else begin
                            quant_word_q[quant_word_index_q] <= data_rd.resp_data;
                            if (quant_word_index_q == 3'd5) begin
                                state_q <= STATE_QUANT_EXEC;
                            end else begin
                                quant_word_index_q <= quant_word_index_q + 3'd1;
                                state_q <= STATE_QUANT_PARAM_REQ;
                            end
                        end
                    end
                end

                STATE_QUANT_EXEC: begin
                    if ((quant_word_q[1][31:5] != 27'd0) ||
                        (signed'(quant_word_q[3]) > signed'(quant_word_q[4])) ||
                        (quant_word_q[5] != 32'd0)) begin
                        complete_fault(NPU_V2_FAULT_VECTOR_CONFIG);
                    end else begin
                        for (int lane_index = 0; lane_index < MAX_VL; lane_index++) begin
                            if ((lane_index < int'(vl_q)) &&
                                predicate_reg_q[active_predicate][lane_index]) begin
                                vector_reg_q[active_rd][lane_index] <= requantize_result(
                                    vector_reg_q[active_rs1][lane_index],
                                    quant_word_q[0],
                                    quant_word_q[1][4:0],
                                    quant_word_q[2],
                                    quant_word_q[3],
                                    quant_word_q[4],
                                    sew_q,
                                    signed_q
                                );
                            end
                        end
                        complete_ok();
                    end
                end

                STATE_EVENT: begin
                    if (result.ready) begin
                        state_q <= STATE_IDLE;
                    end
                end

                default: begin
                    complete_fault(NPU_V2_FAULT_ILLEGAL_INSTRUCTION);
                end
            endcase
        end
    end

    v2_vector_event_fault_code_valid: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            result.valid && result.data[0] |-> (result.data[63:32] != NPU_V2_FAULT_NONE)
    );
    v2_vector_memory_aligned: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            (data_rd.req_valid |-> data_rd.req_addr[1:0] == 2'b00) and
            (data_wr.req_valid |->
                (data_wr.req_addr[1:0] == 2'b00) && (data_wr.req_strb != 4'h0))
    );
    v2_vector_config_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && issue.ready &&
            (instruction_class == NPU_ISA_CLASS_VECTOR_CONFIG) &&
            (instruction_opcode == NPU_ISA_OPCODE_VECTOR_CONFIG_SET)
    );
    v2_vector_load_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && issue.ready &&
            (instruction_class == NPU_ISA_CLASS_VECTOR_MEMORY) &&
            (instruction_opcode == NPU_ISA_OPCODE_VECTOR_MEMORY_LOAD)
    );
    v2_vector_store_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && issue.ready &&
            (instruction_class == NPU_ISA_CLASS_VECTOR_MEMORY) &&
            (instruction_opcode == NPU_ISA_OPCODE_VECTOR_MEMORY_STORE)
    );
    v2_vector_alu_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && issue.ready &&
            (instruction_class == NPU_ISA_CLASS_VECTOR_ALU) &&
            vector_alu_opcode_ok(instruction_opcode)
    );
    v2_vector_fault_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            result.valid && result.ready && result.data[0]
    );
    v2_predicate_ptrue_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && issue.ready &&
            (instruction_class == NPU_ISA_CLASS_PREDICATE) &&
            (instruction_opcode == NPU_ISA_OPCODE_PREDICATE_PTRUE)
    );
    v2_predicate_load_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && issue.ready &&
            (instruction_class == NPU_ISA_CLASS_PREDICATE) &&
            (instruction_opcode == NPU_ISA_OPCODE_PREDICATE_LOAD)
    );
    v2_vector_permute_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && issue.ready &&
            (instruction_class == NPU_ISA_CLASS_VECTOR_PERMUTE)
    );
    v2_vector_reduction_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && issue.ready &&
            (instruction_class == NPU_ISA_CLASS_VECTOR_REDUCTION)
    );
    v2_vector_quantization_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            issue.valid && issue.ready &&
            (instruction_class == NPU_ISA_CLASS_QUANTIZATION)
    );

endmodule

/* verilator lint_on DECLFILENAME */
