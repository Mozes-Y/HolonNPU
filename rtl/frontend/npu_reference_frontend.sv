/* verilator lint_off DECLFILENAME */

module npu_reference_frontend_core (
    npu_frontend_if.frontend       frontend,
    npu_v2_localmem_rd_if.master  data_rd,
    npu_v2_localmem_wr_if.master  data_wr
);

    import npu_v2_pkg::*;
    import npu_isa_pkg::*;

    typedef enum logic [4:0] {
        STATE_IDLE,
        STATE_FETCH,
        STATE_WAIT_RESPONSE,
        STATE_DMA_ISSUE,
        STATE_DMA_WAIT,
        STATE_VECTOR_ISSUE,
        STATE_VECTOR_WAIT,
        STATE_MATRIX_ISSUE,
        STATE_MATRIX_WAIT,
        STATE_SYNC_ISSUE,
        STATE_SCALAR_LOAD_REQ,
        STATE_SCALAR_LOAD_WAIT,
        STATE_SCALAR_STORE_REQ,
        STATE_SCALAR_STORE_WAIT,
        STATE_HALTED,
        STATE_DONE,
        STATE_FAULT
    } frontend_state_e;

    frontend_state_e state_q;
    logic [31:0] pc_q;
    logic [31:0] program_size_bytes_q;
    logic [31:0] fault_code_q;
    logic [63:0] instret_q;
    logic [127:0] dma_issue_data_q;
    logic [127:0] vector_issue_data_q;
    logic [127:0] matrix_issue_data_q;
    logic [63:0] sync_issue_data_q;
    logic [31:0] scalar_reg_q [NPU_ISA_SCALAR_REGISTER_COUNT];
    logic [3:0]  scalar_rd_q;
    logic [31:0] scalar_mem_addr_q;
    logic [31:0] scalar_store_data_q;
    logic        step_active_q;

    logic        fetch_in_range;
    logic [31:0] instruction_class;
    logic [3:0]  instruction_opcode;
    logic [3:0]  instruction_rd;
    logic [3:0]  instruction_rs1;
    logic [3:0]  instruction_rs2;
    logic [11:0] instruction_imm;
    logic        dma_instruction_valid;
    logic [63:0] dma_instruction_system_addr;
    logic [31:0] dma_instruction_local_addr;
    logic [27:0] dma_instruction_byte_count;
    logic        dma_instruction_is_load;
    logic        dma_instruction_is_store;
    logic        sync_instruction_valid;
    logic        sync_instruction_is_wait_dma;
    logic        sync_instruction_is_fence_local;
    logic        sync_instruction_is_fence_dma;
    logic signed [31:0] instruction_simm;
    logic signed [32:0] scalar_address_wide;
    logic [32:0] scalar_address_end;
    logic        scalar_address_valid;
    logic signed [32:0] branch_target_wide;
    logic [32:0] branch_target_end;
    logic        branch_target_valid;
    logic        scalar_branch_equal;
    logic        scalar_branch_taken;
    logic        csr_instruction_valid;
    logic [31:0] csr_read_data;
    frontend_state_e retire_state;

    assign fetch_in_range = (program_size_bytes_q >= NPU_ISA_INSTRUCTION_BYTES) &&
                            (pc_q[1:0] == 2'b00) &&
                            (pc_q <= (program_size_bytes_q - NPU_ISA_INSTRUCTION_BYTES));
    assign instruction_class = frontend.program_rd_resp_data & NPU_ISA_CLASS_MASK;
    assign instruction_opcode = 4'((frontend.program_rd_resp_data >> NPU_ISA_OPCODE_SHIFT) &
                                   NPU_ISA_FIELD_MASK);
    assign instruction_rd = 4'((frontend.program_rd_resp_data >> NPU_ISA_RD_SHIFT) &
                               NPU_ISA_FIELD_MASK);
    assign instruction_rs1 = 4'((frontend.program_rd_resp_data >> NPU_ISA_RS1_SHIFT) &
                                NPU_ISA_FIELD_MASK);
    assign instruction_rs2 = 4'((frontend.program_rd_resp_data >> NPU_ISA_RS2_SHIFT) &
                                NPU_ISA_FIELD_MASK);
    assign instruction_imm = 12'(frontend.program_rd_resp_data & NPU_ISA_IMM_MASK);
    assign dma_instruction_system_addr = {
        scalar_reg_q[instruction_rs1],
        scalar_reg_q[instruction_rd]
    };
    assign dma_instruction_local_addr = scalar_reg_q[instruction_rs2];
    assign dma_instruction_byte_count = ({16'd0, instruction_imm} + 28'd1) << 2;
    assign dma_instruction_is_load = (instruction_opcode == NPU_ISA_OPCODE_DMA_LOAD);
    assign dma_instruction_is_store = (instruction_opcode == NPU_ISA_OPCODE_DMA_STORE);
    assign dma_instruction_valid = dma_instruction_is_load || dma_instruction_is_store;
    assign sync_instruction_is_wait_dma = (instruction_opcode == NPU_ISA_OPCODE_SYNC_WAIT_DMA);
    assign sync_instruction_is_fence_local = (instruction_opcode == NPU_ISA_OPCODE_SYNC_FENCE_LOCAL);
    assign sync_instruction_is_fence_dma = (instruction_opcode == NPU_ISA_OPCODE_SYNC_FENCE_DMA);
    assign sync_instruction_valid = (sync_instruction_is_wait_dma ||
                                     sync_instruction_is_fence_local ||
                                     sync_instruction_is_fence_dma) &&
                                    (instruction_rd == 4'h0) &&
                                    (instruction_rs1 == 4'h0) &&
                                    (instruction_rs2 == 4'h0) &&
                                    (instruction_imm == 12'h000);
    assign instruction_simm = {{20{instruction_imm[11]}}, instruction_imm};
    assign scalar_address_wide = $signed({1'b0, scalar_reg_q[instruction_rs1]}) +
                                 $signed({instruction_simm[31], instruction_simm});
    assign scalar_address_end = unsigned'(scalar_address_wide) + 33'd4;
    assign scalar_address_valid = !scalar_address_wide[32] &&
                                  (scalar_address_wide[1:0] == 2'b00) &&
                                  (scalar_address_end <= {1'b0, frontend.local_mem_bytes});
    assign branch_target_wide = $signed({1'b0, pc_q}) +
                                ($signed({instruction_simm[31], instruction_simm}) *
                                 signed'(NPU_ISA_SCALAR_BRANCH_SCALE));
    assign branch_target_end = unsigned'(branch_target_wide) + NPU_ISA_INSTRUCTION_BYTES;
    assign branch_target_valid = !branch_target_wide[32] &&
                                 (branch_target_wide[1:0] == 2'b00) &&
                                 (branch_target_end <= {1'b0, program_size_bytes_q});
    assign scalar_branch_equal = scalar_reg_q[instruction_rs1] == scalar_reg_q[instruction_rs2];
    assign scalar_branch_taken =
        ((instruction_opcode == NPU_ISA_OPCODE_FRONTEND_CONTROL_BEQ) && scalar_branch_equal) ||
        ((instruction_opcode == NPU_ISA_OPCODE_FRONTEND_CONTROL_BNE) && !scalar_branch_equal);
    assign retire_state = step_active_q ? STATE_HALTED : STATE_FETCH;

    always_comb begin
        csr_instruction_valid = (instruction_opcode == NPU_ISA_OPCODE_CSR_DEBUG_READ) &&
                                (instruction_rs1 == 4'd0) &&
                                (instruction_rs2 == 4'd0);
        csr_read_data = 32'd0;
        unique case (instruction_imm)
            12'(NPU_ISA_CSR_PC): csr_read_data = pc_q;
            12'(NPU_ISA_CSR_INSTRET_LO): csr_read_data = instret_q[31:0];
            12'(NPU_ISA_CSR_INSTRET_HI): csr_read_data = instret_q[63:32];
            12'(NPU_ISA_CSR_PROGRAM_SIZE_BYTES): csr_read_data = program_size_bytes_q;
            12'(NPU_ISA_CSR_LOCAL_MEM_BYTES): csr_read_data = frontend.local_mem_bytes;
            default: csr_instruction_valid = 1'b0;
        endcase
    end

    assign frontend.running = (state_q == STATE_FETCH) ||
                              (state_q == STATE_WAIT_RESPONSE) ||
                              (state_q == STATE_DMA_ISSUE) ||
                              (state_q == STATE_DMA_WAIT) ||
                              (state_q == STATE_VECTOR_ISSUE) ||
                              (state_q == STATE_VECTOR_WAIT) ||
                              (state_q == STATE_MATRIX_ISSUE) ||
                              (state_q == STATE_MATRIX_WAIT) ||
                              (state_q == STATE_SYNC_ISSUE) ||
                              (state_q == STATE_SCALAR_LOAD_REQ) ||
                              (state_q == STATE_SCALAR_LOAD_WAIT) ||
                              (state_q == STATE_SCALAR_STORE_REQ) ||
                              (state_q == STATE_SCALAR_STORE_WAIT);
    assign frontend.halted = (state_q == STATE_HALTED);
    assign frontend.done = (state_q == STATE_DONE);
    assign frontend.fault = (state_q == STATE_FAULT);
    assign frontend.fault_code = fault_code_q;
    assign frontend.debug_pc = pc_q;
    assign frontend.instret = instret_q;
    assign frontend.program_rd_valid = (state_q == STATE_FETCH) &&
                                       !frontend.halt_request &&
                                       fetch_in_range;
    assign frontend.program_rd_addr = pc_q;

    assign frontend.dma_issue_valid = (state_q == STATE_DMA_ISSUE);
    assign frontend.dma_issue_data = dma_issue_data_q;
    assign frontend.vector_issue_valid = (state_q == STATE_VECTOR_ISSUE);
    assign frontend.vector_issue_data = vector_issue_data_q;
    assign frontend.vector_result_ready = (state_q == STATE_VECTOR_WAIT);
    assign frontend.matrix_issue_valid = (state_q == STATE_MATRIX_ISSUE);
    assign frontend.matrix_issue_data = matrix_issue_data_q;
    assign frontend.matrix_result_ready = (state_q == STATE_MATRIX_WAIT);
    assign frontend.sync_issue_valid = (state_q == STATE_SYNC_ISSUE);
    assign frontend.sync_issue_data = sync_issue_data_q;
    assign data_rd.req_valid = (state_q == STATE_SCALAR_LOAD_REQ);
    assign data_rd.req_addr = scalar_mem_addr_q;
    assign data_wr.req_valid = (state_q == STATE_SCALAR_STORE_REQ);
    assign data_wr.req_addr = scalar_mem_addr_q;
    assign data_wr.req_data = scalar_store_data_q;
    assign data_wr.req_strb = 4'hF;

    always_ff @(posedge frontend.clk_i or negedge frontend.rst_ni) begin
        if (!frontend.rst_ni) begin
            state_q <= STATE_IDLE;
            pc_q <= 32'h0000_0000;
            program_size_bytes_q <= 32'h0000_0000;
            fault_code_q <= NPU_V2_FAULT_NONE;
            instret_q <= 64'h0000_0000_0000_0000;
            dma_issue_data_q <= '0;
            vector_issue_data_q <= '0;
            matrix_issue_data_q <= '0;
            sync_issue_data_q <= '0;
            scalar_rd_q <= 4'd0;
            scalar_mem_addr_q <= 32'd0;
            scalar_store_data_q <= 32'd0;
            step_active_q <= 1'b0;
            for (int reg_index = 0; reg_index < NPU_ISA_SCALAR_REGISTER_COUNT; reg_index++) begin
                scalar_reg_q[reg_index] <= 32'd0;
            end
        end else if (frontend.soft_reset) begin
            state_q <= STATE_IDLE;
            pc_q <= 32'h0000_0000;
            program_size_bytes_q <= 32'h0000_0000;
            fault_code_q <= NPU_V2_FAULT_NONE;
            instret_q <= 64'h0000_0000_0000_0000;
            dma_issue_data_q <= '0;
            vector_issue_data_q <= '0;
            matrix_issue_data_q <= '0;
            sync_issue_data_q <= '0;
            scalar_rd_q <= 4'd0;
            scalar_mem_addr_q <= 32'd0;
            scalar_store_data_q <= 32'd0;
            step_active_q <= 1'b0;
            for (int reg_index = 0; reg_index < NPU_ISA_SCALAR_REGISTER_COUNT; reg_index++) begin
                scalar_reg_q[reg_index] <= 32'd0;
            end
        end else begin
            scalar_reg_q[0] <= 32'd0;
            unique case (state_q)
                STATE_IDLE: begin
                    if (frontend.start) begin
                        pc_q <= frontend.entry_pc;
                        program_size_bytes_q <= frontend.program_size_bytes;
                        fault_code_q <= NPU_V2_FAULT_NONE;
                        instret_q <= 64'h0000_0000_0000_0000;
                        step_active_q <= 1'b0;
                        for (int reg_index = 0; reg_index < NPU_ISA_SCALAR_REGISTER_COUNT; reg_index++) begin
                            scalar_reg_q[reg_index] <= 32'd0;
                        end
                        if ((frontend.entry_pc[1:0] != 2'b00) ||
                            (frontend.program_size_bytes < NPU_ISA_INSTRUCTION_BYTES) ||
                            (frontend.entry_pc > (frontend.program_size_bytes - NPU_ISA_INSTRUCTION_BYTES))) begin
                            fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                            state_q <= STATE_FAULT;
                        end else begin
                            state_q <= STATE_FETCH;
                        end
                    end
                end

                STATE_FETCH: begin
                    if (frontend.halt_request) begin
                        state_q <= STATE_HALTED;
                    end else if (!fetch_in_range) begin
                        fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                        state_q <= STATE_FAULT;
                    end else begin
                        state_q <= STATE_WAIT_RESPONSE;
                    end
                end

                STATE_WAIT_RESPONSE: begin
                    if (frontend.program_rd_resp_valid) begin
                        if (frontend.program_rd_resp_error) begin
                            fault_code_q <= NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS;
                            state_q <= STATE_FAULT;
                        end else if (instruction_class == NPU_ISA_CLASS_FRONTEND_CONTROL) begin
                            unique case (instruction_opcode)
                                NPU_ISA_OPCODE_FRONTEND_CONTROL_MOVI: begin
                                    if ((instruction_rs1 != 4'd0) || (instruction_rs2 != 4'd0)) begin
                                        fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                                        state_q <= STATE_FAULT;
                                    end else begin
                                        if (instruction_rd != 4'd0) begin
                                            scalar_reg_q[instruction_rd] <= instruction_simm;
                                        end
                                        pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                                        instret_q <= instret_q + 64'd1;
                                        step_active_q <= 1'b0;
                                        state_q <= retire_state;
                                    end
                                end

                                NPU_ISA_OPCODE_FRONTEND_CONTROL_ADD: begin
                                    if (instruction_imm != 12'd0) begin
                                        fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                                        state_q <= STATE_FAULT;
                                    end else begin
                                        if (instruction_rd != 4'd0) begin
                                            scalar_reg_q[instruction_rd] <=
                                                scalar_reg_q[instruction_rs1] + scalar_reg_q[instruction_rs2];
                                        end
                                        pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                                        instret_q <= instret_q + 64'd1;
                                        step_active_q <= 1'b0;
                                        state_q <= retire_state;
                                    end
                                end

                                NPU_ISA_OPCODE_FRONTEND_CONTROL_ADDI: begin
                                    if (instruction_rs2 != 4'd0) begin
                                        fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                                        state_q <= STATE_FAULT;
                                    end else begin
                                        if (instruction_rd != 4'd0) begin
                                            scalar_reg_q[instruction_rd] <=
                                                scalar_reg_q[instruction_rs1] + instruction_simm;
                                        end
                                        pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                                        instret_q <= instret_q + 64'd1;
                                        step_active_q <= 1'b0;
                                        state_q <= retire_state;
                                    end
                                end

                                NPU_ISA_OPCODE_FRONTEND_CONTROL_LOAD: begin
                                    if ((instruction_rs2 != 4'd0) || !scalar_address_valid) begin
                                        fault_code_q <= (instruction_rs2 != 4'd0)
                                            ? NPU_V2_FAULT_ILLEGAL_INSTRUCTION
                                            : NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS;
                                        state_q <= STATE_FAULT;
                                    end else begin
                                        scalar_rd_q <= instruction_rd;
                                        scalar_mem_addr_q <= scalar_address_wide[31:0];
                                        state_q <= STATE_SCALAR_LOAD_REQ;
                                    end
                                end

                                NPU_ISA_OPCODE_FRONTEND_CONTROL_STORE: begin
                                    if ((instruction_rd != 4'd0) || !scalar_address_valid) begin
                                        fault_code_q <= (instruction_rd != 4'd0)
                                            ? NPU_V2_FAULT_ILLEGAL_INSTRUCTION
                                            : NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS;
                                        state_q <= STATE_FAULT;
                                    end else begin
                                        scalar_mem_addr_q <= scalar_address_wide[31:0];
                                        scalar_store_data_q <= scalar_reg_q[instruction_rs2];
                                        state_q <= STATE_SCALAR_STORE_REQ;
                                    end
                                end

                                NPU_ISA_OPCODE_FRONTEND_CONTROL_BEQ,
                                NPU_ISA_OPCODE_FRONTEND_CONTROL_BNE: begin
                                    if ((instruction_rd != 4'd0) ||
                                        (scalar_branch_taken && !branch_target_valid)) begin
                                        fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                                        state_q <= STATE_FAULT;
                                    end else begin
                                        pc_q <= scalar_branch_taken
                                            ? branch_target_wide[31:0]
                                            : pc_q + NPU_ISA_INSTRUCTION_BYTES;
                                        instret_q <= instret_q + 64'd1;
                                        step_active_q <= 1'b0;
                                        state_q <= retire_state;
                                    end
                                end

                                default: begin
                                    fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                                    state_q <= STATE_FAULT;
                                end
                            endcase
                        end else if (instruction_class == NPU_ISA_CLASS_DMA) begin
                            if (dma_instruction_valid) begin
                                dma_issue_data_q <= {
                                    dma_instruction_is_store ? 4'h1 : 4'h0,
                                    dma_instruction_byte_count,
                                    dma_instruction_local_addr,
                                    dma_instruction_system_addr
                                };
                                state_q <= STATE_DMA_ISSUE;
                            end else begin
                                fault_code_q <= NPU_V2_FAULT_DMA_REQUEST;
                                state_q <= STATE_FAULT;
                            end
                        end else if (instruction_class == NPU_ISA_CLASS_SYNC) begin
                            if (sync_instruction_valid) begin
                                sync_issue_data_q <= {32'd0, frontend.program_rd_resp_data};
                                state_q <= STATE_SYNC_ISSUE;
                            end else begin
                                fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                                state_q <= STATE_FAULT;
                            end
                        end else if ((instruction_class == NPU_ISA_CLASS_PREDICATE) ||
                                     (instruction_class == NPU_ISA_CLASS_VECTOR_CONFIG) ||
                                     (instruction_class == NPU_ISA_CLASS_VECTOR_MEMORY) ||
                                     (instruction_class == NPU_ISA_CLASS_VECTOR_ALU) ||
                                     (instruction_class == NPU_ISA_CLASS_VECTOR_PERMUTE) ||
                                     (instruction_class == NPU_ISA_CLASS_VECTOR_REDUCTION) ||
                                     (instruction_class == NPU_ISA_CLASS_QUANTIZATION)) begin
                            vector_issue_data_q <= {96'd0, frontend.program_rd_resp_data};
                            state_q <= STATE_VECTOR_ISSUE;
                        end else if (instruction_class == NPU_ISA_CLASS_MATRIX) begin
                            matrix_issue_data_q <= {96'd0, frontend.program_rd_resp_data};
                            state_q <= STATE_MATRIX_ISSUE;
                        end else if (instruction_class == NPU_ISA_CLASS_CSR_DEBUG) begin
                            if (!csr_instruction_valid) begin
                                fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                                state_q <= STATE_FAULT;
                            end else begin
                                if (instruction_rd != 4'd0) begin
                                    scalar_reg_q[instruction_rd] <= csr_read_data;
                                end
                                pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                                instret_q <= instret_q + 64'd1;
                                step_active_q <= 1'b0;
                                state_q <= retire_state;
                            end
                        end else if (instruction_class == NPU_ISA_CLASS_SYSTEM) begin
                            unique case (instruction_opcode)
                                NPU_ISA_OPCODE_SYSTEM_EXIT: begin
                                    pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                                    instret_q <= instret_q + 64'd1;
                                    fault_code_q <= NPU_V2_FAULT_NONE;
                                    state_q <= STATE_DONE;
                                end

                                NPU_ISA_OPCODE_SYSTEM_FAULT: begin
                                    fault_code_q <= 32'(instruction_imm);
                                    state_q <= STATE_FAULT;
                                end

                                default: begin
                                    fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                                    state_q <= STATE_FAULT;
                                end
                            endcase
                        end else begin
                            fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                            state_q <= STATE_FAULT;
                        end

                    end
                end

                STATE_DMA_ISSUE: begin
                    if (frontend.dma_issue_ready) begin
                        state_q <= STATE_DMA_WAIT;
                    end
                end

                STATE_DMA_WAIT: begin
                    if (frontend.dma_fault_valid) begin
                        fault_code_q <= frontend.dma_fault_code;
                        state_q <= STATE_FAULT;
                    end else if (frontend.dma_event_valid) begin
                        pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                        instret_q <= instret_q + 64'd1;
                        step_active_q <= 1'b0;
                        state_q <= retire_state;
                    end
                end

                STATE_VECTOR_ISSUE: begin
                    if (frontend.vector_issue_ready) begin
                        state_q <= STATE_VECTOR_WAIT;
                    end
                end

                STATE_VECTOR_WAIT: begin
                    if (frontend.vector_result_valid) begin
                        if (frontend.vector_result_data[0]) begin
                            fault_code_q <= frontend.vector_result_data[63:32];
                            state_q <= STATE_FAULT;
                        end else begin
                            pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                            instret_q <= instret_q + 64'd1;
                            step_active_q <= 1'b0;
                            state_q <= retire_state;
                        end
                    end
                end

                STATE_MATRIX_ISSUE: begin
                    if (frontend.matrix_issue_ready) begin
                        state_q <= STATE_MATRIX_WAIT;
                    end
                end

                STATE_MATRIX_WAIT: begin
                    if (frontend.matrix_result_valid) begin
                        if (frontend.matrix_result_data[0]) begin
                            fault_code_q <= frontend.matrix_result_data[63:32];
                            state_q <= STATE_FAULT;
                        end else begin
                            pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                            instret_q <= instret_q + 64'd1;
                            step_active_q <= 1'b0;
                            state_q <= retire_state;
                        end
                    end
                end

                STATE_SYNC_ISSUE: begin
                    if (frontend.sync_issue_ready) begin
                        pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                        instret_q <= instret_q + 64'd1;
                        step_active_q <= 1'b0;
                        state_q <= retire_state;
                    end
                end

                STATE_SCALAR_LOAD_REQ: begin
                    if (data_rd.req_ready) begin
                        state_q <= STATE_SCALAR_LOAD_WAIT;
                    end
                end

                STATE_SCALAR_LOAD_WAIT: begin
                    if (data_rd.resp_valid) begin
                        if (data_rd.resp_error) begin
                            fault_code_q <= NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS;
                            state_q <= STATE_FAULT;
                        end else begin
                            if (scalar_rd_q != 4'd0) begin
                                scalar_reg_q[scalar_rd_q] <= data_rd.resp_data;
                            end
                            pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                            instret_q <= instret_q + 64'd1;
                            step_active_q <= 1'b0;
                            state_q <= retire_state;
                        end
                    end
                end

                STATE_SCALAR_STORE_REQ: begin
                    if (data_wr.req_ready) begin
                        state_q <= STATE_SCALAR_STORE_WAIT;
                    end
                end

                STATE_SCALAR_STORE_WAIT: begin
                    if (data_wr.resp_valid) begin
                        if (data_wr.resp_error) begin
                            fault_code_q <= NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS;
                            state_q <= STATE_FAULT;
                        end else begin
                            pc_q <= pc_q + NPU_ISA_INSTRUCTION_BYTES;
                            instret_q <= instret_q + 64'd1;
                            step_active_q <= 1'b0;
                            state_q <= retire_state;
                        end
                    end
                end

                STATE_HALTED: begin
                    if (frontend.resume) begin
                        step_active_q <= 1'b0;
                        state_q <= STATE_FETCH;
                    end else if (frontend.debug_step) begin
                        if (!fetch_in_range) begin
                            fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                            state_q <= STATE_FAULT;
                        end else begin
                            step_active_q <= 1'b1;
                            state_q <= STATE_FETCH;
                        end
                    end
                end

                STATE_DONE,
                STATE_FAULT: begin
                    if (frontend.start) begin
                        pc_q <= frontend.entry_pc;
                        program_size_bytes_q <= frontend.program_size_bytes;
                        fault_code_q <= NPU_V2_FAULT_NONE;
                        instret_q <= 64'h0000_0000_0000_0000;
                        step_active_q <= 1'b0;
                        for (int reg_index = 0; reg_index < NPU_ISA_SCALAR_REGISTER_COUNT; reg_index++) begin
                            scalar_reg_q[reg_index] <= 32'd0;
                        end
                        if ((frontend.entry_pc[1:0] != 2'b00) ||
                            (frontend.program_size_bytes < NPU_ISA_INSTRUCTION_BYTES) ||
                            (frontend.entry_pc > (frontend.program_size_bytes - NPU_ISA_INSTRUCTION_BYTES))) begin
                            fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                            state_q <= STATE_FAULT;
                        end else begin
                            state_q <= STATE_FETCH;
                        end
                    end
                end

                default: begin
                    fault_code_q <= NPU_V2_FAULT_ILLEGAL_INSTRUCTION;
                    state_q <= STATE_FAULT;
                end
            endcase
        end
    end

    v2_frontend_terminal_state_stable: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            (state_q == STATE_DONE || state_q == STATE_FAULT) && !frontend.start |=> state_q == $past(state_q)
    );
    v2_frontend_fault_has_code: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.fault |-> (frontend.fault_code != NPU_V2_FAULT_NONE)
    );
    v2_frontend_fetch_aligned: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.program_rd_valid |-> (frontend.program_rd_addr[1:0] == 2'b00)
    );
    v2_frontend_scalar_memory_aligned: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            (data_rd.req_valid |-> data_rd.req_addr[1:0] == 2'b00) and
            (data_wr.req_valid |-> data_wr.req_addr[1:0] == 2'b00 && data_wr.req_strb == 4'hF)
    );
    v2_frontend_debug_step_returns_halted: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            step_active_q && ((state_q == STATE_WAIT_RESPONSE) ||
                              (state_q == STATE_DMA_WAIT) ||
                              (state_q == STATE_VECTOR_WAIT) ||
                              (state_q == STATE_MATRIX_WAIT) ||
                              (state_q == STATE_SCALAR_LOAD_WAIT) ||
                              (state_q == STATE_SCALAR_STORE_WAIT))
            |-> !frontend.done
    );
    v2_frontend_system_exit_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.program_rd_resp_valid &&
            (instruction_class == NPU_ISA_CLASS_SYSTEM) &&
            (instruction_opcode == NPU_ISA_OPCODE_SYSTEM_EXIT)
    );
    v2_frontend_system_fault_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.program_rd_resp_valid &&
            (instruction_class == NPU_ISA_CLASS_SYSTEM) &&
            (instruction_opcode == NPU_ISA_OPCODE_SYSTEM_FAULT)
    );
    v2_frontend_dma_load_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.program_rd_resp_valid &&
            (instruction_class == NPU_ISA_CLASS_DMA) &&
            (instruction_opcode == NPU_ISA_OPCODE_DMA_LOAD)
    );
    v2_frontend_dma_store_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.program_rd_resp_valid &&
            (instruction_class == NPU_ISA_CLASS_DMA) &&
            (instruction_opcode == NPU_ISA_OPCODE_DMA_STORE)
    );
    v2_frontend_sync_wait_dma_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.program_rd_resp_valid &&
            (instruction_class == NPU_ISA_CLASS_SYNC) &&
            (instruction_opcode == NPU_ISA_OPCODE_SYNC_WAIT_DMA)
    );
    v2_frontend_sync_fence_local_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.program_rd_resp_valid &&
            (instruction_class == NPU_ISA_CLASS_SYNC) &&
            (instruction_opcode == NPU_ISA_OPCODE_SYNC_FENCE_LOCAL)
    );
    v2_frontend_sync_fence_dma_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.program_rd_resp_valid &&
            (instruction_class == NPU_ISA_CLASS_SYNC) &&
            (instruction_opcode == NPU_ISA_OPCODE_SYNC_FENCE_DMA)
    );
    v2_frontend_vector_issue_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.vector_issue_valid && frontend.vector_issue_ready
    );
    v2_frontend_vector_success_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.vector_result_valid && frontend.vector_result_ready &&
            !frontend.vector_result_data[0]
    );
    v2_frontend_matrix_issue_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.matrix_issue_valid && frontend.matrix_issue_ready
    );
    v2_frontend_matrix_success_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.matrix_result_valid && !frontend.matrix_result_data[0]
    );
    v2_frontend_sync_issue_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.sync_issue_valid && frontend.sync_issue_ready
    );
    v2_frontend_vector_fault_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni || frontend.soft_reset)
            frontend.vector_result_valid && frontend.vector_result_ready &&
            frontend.vector_result_data[0]
    );

endmodule

/* verilator lint_on DECLFILENAME */
