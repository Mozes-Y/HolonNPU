/* verilator lint_off DECLFILENAME */

module npu_program_loader_core #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic                    soft_reset_i,

    input  logic                    start_i,
    input  logic [ADDR_W-1:0]       desc_addr_i,
    output logic                    busy_o,
    output logic                    done_o,
    output logic                    fault_o,
    output logic [31:0]             fault_code_o,

    output logic [7:0]              program_format_o,
    output logic [15:0]             holon_isa_major_o,
    output logic [15:0]             holon_isa_minor_o,
    output logic [63:0]             required_caps_o,
    output logic [63:0]             required_op_classes_o,
    output logic [63:0]             code_addr_o,
    output logic [31:0]             code_size_bytes_o,
    output logic [31:0]             entry_pc_o,
    output logic [63:0]             arg_addr_o,
    output logic [31:0]             arg_size_bytes_o,
    output logic [31:0]             local_mem_bytes_o,
    output logic [31:0]             program_mem_bytes_o,
    output logic [31:0]             stack_bytes_o,
    output logic [63:0]             completion_addr_o,
    output logic [31:0]             flags_o,

    npu_localmem_wr_if.master    program_wr,
    npu_localmem_wr_if.master    data_wr,

    npu_axi4_if.read_master         m_axi
);

    import npu_pkg::*;

    localparam int unsigned BEAT_BYTES = DATA_W / 8;
    localparam int unsigned BEAT_SHIFT = $clog2(BEAT_BYTES);
    localparam int unsigned WORD_BYTES = 4;
    localparam int unsigned WORD_SHIFT = 2;
    localparam int unsigned COPY_BURST_MAX_WORDS = 16;
    localparam int unsigned DESC_BEATS = NPU_PROGRAM_DESC_SIZE / BEAT_BYTES;
    localparam int unsigned DESC_BEAT_W = $clog2(DESC_BEATS);

    typedef enum logic [2:0] {
        STATE_IDLE  = 3'd0,
        STATE_AR    = 3'd1,
        STATE_R     = 3'd2,
        STATE_DRAIN = 3'd3,
        STATE_CHECK = 3'd4,
        STATE_DONE  = 3'd5,
        STATE_FAULT = 3'd6,
        STATE_WRITE_RESP = 3'd7
    } state_e;

    typedef enum logic [1:0] {
        PHASE_DESC = 2'd0,
        PHASE_CODE = 2'd1,
        PHASE_ARG  = 2'd2
    } phase_e;

    localparam logic [1:0] AXI_RESP_OKAY = 2'b00;
    localparam logic [1:0] AXI_RESP_EXOKAY = 2'b01;
    localparam logic [1:0] AXI_BURST_INCR = 2'b01;
    localparam logic [63:0] IMPLEMENTED_CAPS = {
        NPU_RESET_CAP0_HI,
        NPU_RESET_CAP0_LO
    };
    localparam logic [63:0] IMPLEMENTED_OP_CLASSES = {
        NPU_RESET_OP_CLASS_HI,
        NPU_RESET_OP_CLASS_LO
    };
    localparam logic [15:0] IMPLEMENTED_ISA_MAJOR = 16'(NPU_RESET_ISA_VERSION >> 16);
    localparam logic [15:0] IMPLEMENTED_ISA_MINOR = 16'(NPU_RESET_ISA_VERSION);

    state_e state_q;
    phase_e phase_q;
    logic [ADDR_W-1:0] desc_addr_q;
    logic [DESC_BEAT_W-1:0] desc_beat_q;
    logic [4:0] desc_burst_beats_q;
    logic [4:0] desc_burst_seen_q;
    logic [DATA_W-1:0] desc_q [DESC_BEATS];
    logic [31:0] fault_code_q;
    logic [ADDR_W-1:0] copy_addr_q;
    logic [31:0] copy_local_offset_q;
    logic [31:0] copy_words_remaining_q;
    logic [4:0] copy_burst_words_q;
    logic [4:0] copy_burst_seen_q;

    logic r_fire;
    logic r_resp_ok;
    logic final_desc_beat;
    logic copy_phase;
    logic copy_write_ready;
    logic copy_write_resp_valid;
    logic copy_write_resp_error;
    logic copy_burst_last_expected;
    logic desc_addr_aligned;
    logic desc_addr_range_valid;
    logic validation_fault;
    logic [31:0] validation_fault_code;
    logic reserved_tail_nonzero;
    logic [4:0] copy_ar_words;
    logic [4:0] desc_ar_beats;
    logic [8:0] desc_beats_remaining;
    logic [8:0] desc_page_beats;
    logic [10:0] copy_page_words;
    logic [31:0] copy_word;

    logic [15:0] desc_size;
    logic [7:0] desc_version;
    logic [7:0] desc_program_format;
    logic [15:0] desc_holon_isa_major;
    logic [15:0] desc_holon_isa_minor;
    logic [63:0] desc_required_caps;
    logic [63:0] desc_required_op_classes;
    logic [63:0] desc_code_addr;
    logic [31:0] desc_code_size_bytes;
    logic [31:0] desc_entry_pc;
    logic [63:0] desc_arg_addr;
    logic [31:0] desc_arg_size_bytes;
    logic [31:0] desc_local_mem_bytes;
    logic [31:0] desc_program_mem_bytes;
    logic [31:0] desc_stack_bytes;
    logic [63:0] desc_completion_addr;
    logic [31:0] desc_flags;
    logic [31:0] desc_reserved_4c;

    assign busy_o = (state_q == STATE_AR) || (state_q == STATE_R) ||
                    (state_q == STATE_DRAIN) || (state_q == STATE_CHECK) ||
                    (state_q == STATE_WRITE_RESP);
    assign done_o = (state_q == STATE_DONE);
    assign fault_o = (state_q == STATE_FAULT);
    assign fault_code_o = fault_code_q;

    assign r_fire = m_axi.rvalid && m_axi.rready;
    assign r_resp_ok = (m_axi.rresp == AXI_RESP_OKAY) ||
                       (m_axi.rresp == AXI_RESP_EXOKAY);
    assign final_desc_beat = (desc_beat_q == DESC_BEAT_W'(DESC_BEATS - 1));
    assign copy_phase = (phase_q == PHASE_CODE) || (phase_q == PHASE_ARG);
    assign copy_write_ready = (phase_q == PHASE_CODE)
        ? program_wr.req_ready
        : data_wr.req_ready;
    assign copy_write_resp_valid = (phase_q == PHASE_CODE)
        ? program_wr.resp_valid
        : data_wr.resp_valid;
    assign copy_write_resp_error = (phase_q == PHASE_CODE)
        ? program_wr.resp_error
        : data_wr.resp_error;
    assign copy_burst_last_expected = (copy_burst_seen_q == (copy_burst_words_q - 5'd1));
    assign desc_addr_aligned = desc_addr_i[BEAT_SHIFT-1:0] == '0;
    assign desc_addr_range_valid = range_fits_addr(
        desc_addr_i,
        32'(NPU_PROGRAM_DESC_SIZE)
    );
    assign desc_beats_remaining = 9'(DESC_BEATS) - 9'(desc_beat_q);
    assign desc_page_beats = 9'((13'd4096 - {1'b0, desc_addr_q[11:0]}) >> BEAT_SHIFT);
    assign desc_ar_beats = (desc_beats_remaining < desc_page_beats)
        ? 5'(desc_beats_remaining)
        : 5'(desc_page_beats);
    assign copy_page_words = 11'((13'd4096 - {1'b0, copy_addr_q[11:0]}) >> WORD_SHIFT);
    assign copy_ar_words = (copy_words_remaining_q < 32'(COPY_BURST_MAX_WORDS))
        ? ((copy_words_remaining_q < 32'(copy_page_words))
            ? 5'(copy_words_remaining_q)
            : 5'(copy_page_words))
        : ((copy_page_words < 11'(COPY_BURST_MAX_WORDS))
            ? 5'(copy_page_words)
            : 5'(COPY_BURST_MAX_WORDS));
    assign copy_word = select_rdata_word(m_axi.rdata, copy_addr_q[3:2]);

    assign m_axi.arid = '0;
    assign m_axi.araddr = (phase_q == PHASE_DESC) ? desc_addr_q : copy_addr_q;
    assign m_axi.arlen = (phase_q == PHASE_DESC)
        ? 8'(desc_ar_beats - 5'd1)
        : 8'(copy_ar_words - 5'd1);
    assign m_axi.arsize = (phase_q == PHASE_DESC) ? 3'(BEAT_SHIFT) : 3'(WORD_SHIFT);
    assign m_axi.arburst = AXI_BURST_INCR;
    assign m_axi.arvalid = (state_q == STATE_AR);
    assign m_axi.rready = (state_q == STATE_DRAIN) ||
                           ((state_q == STATE_R) &&
                            ((phase_q == PHASE_DESC) || !r_resp_ok || copy_write_ready));

    assign program_wr.req_valid = (state_q == STATE_R) && (phase_q == PHASE_CODE) &&
                                  m_axi.rvalid && r_resp_ok;
    assign program_wr.req_addr = copy_local_offset_q;
    assign program_wr.req_data = copy_word;
    assign program_wr.req_strb = 4'hF;
    assign data_wr.req_valid = (state_q == STATE_R) && (phase_q == PHASE_ARG) &&
                               m_axi.rvalid && r_resp_ok;
    assign data_wr.req_addr = copy_local_offset_q;
    assign data_wr.req_data = copy_word;
    assign data_wr.req_strb = 4'hF;

    assign program_format_o = desc_program_format;
    assign holon_isa_major_o = desc_holon_isa_major;
    assign holon_isa_minor_o = desc_holon_isa_minor;
    assign required_caps_o = desc_required_caps;
    assign required_op_classes_o = desc_required_op_classes;
    assign code_addr_o = desc_code_addr;
    assign code_size_bytes_o = desc_code_size_bytes;
    assign entry_pc_o = desc_entry_pc;
    assign arg_addr_o = desc_arg_addr;
    assign arg_size_bytes_o = desc_arg_size_bytes;
    assign local_mem_bytes_o = desc_local_mem_bytes;
    assign program_mem_bytes_o = desc_program_mem_bytes;
    assign stack_bytes_o = desc_stack_bytes;
    assign completion_addr_o = desc_completion_addr;
    assign flags_o = desc_flags;

    function automatic logic [7:0] desc_byte(input int unsigned byte_offset);
        logic [DESC_BEAT_W-1:0] beat;
        logic [3:0] byte_in_beat;
        begin
            beat = DESC_BEAT_W'(byte_offset >> BEAT_SHIFT);
            byte_in_beat = 4'(byte_offset & (BEAT_BYTES - 1));
            desc_byte = desc_q[beat][(byte_in_beat * 8) +: 8];
        end
    endfunction

    function automatic logic [15:0] desc_u16(input int unsigned byte_offset);
        desc_u16 = {desc_byte(byte_offset + 1), desc_byte(byte_offset)};
    endfunction

    function automatic logic [31:0] desc_u32(input int unsigned byte_offset);
        desc_u32 = {
            desc_byte(byte_offset + 3),
            desc_byte(byte_offset + 2),
            desc_byte(byte_offset + 1),
            desc_byte(byte_offset)
        };
    endfunction

    function automatic logic [63:0] desc_u64(input int unsigned byte_offset);
        desc_u64 = {
            desc_byte(byte_offset + 7),
            desc_byte(byte_offset + 6),
            desc_byte(byte_offset + 5),
            desc_byte(byte_offset + 4),
            desc_byte(byte_offset + 3),
            desc_byte(byte_offset + 2),
            desc_byte(byte_offset + 1),
            desc_byte(byte_offset)
        };
    endfunction

    function automatic logic [31:0] select_rdata_word(
        input logic [DATA_W-1:0] data,
        input logic [1:0] lane
    );
        unique case (lane)
            2'd0: select_rdata_word = data[31:0];
            2'd1: select_rdata_word = data[63:32];
            2'd2: select_rdata_word = data[95:64];
            default: select_rdata_word = data[127:96];
        endcase
    endfunction

    function automatic logic aligned_to(input logic [63:0] value, input int unsigned alignment);
        aligned_to = (value & 64'(alignment - 1)) == 64'h0000_0000_0000_0000;
    endfunction

    function automatic logic range_fits_addr(
        input logic [ADDR_W-1:0] start,
        input logic [31:0] byte_count
    );
        range_fits_addr = ADDR_W'(byte_count) <= ~start;
    endfunction

    function automatic logic range_fits_u64(
        input logic [63:0] start,
        input logic [31:0] byte_count
    );
        range_fits_u64 = 64'(byte_count) <= ~start;
    endfunction

    always_comb begin
        desc_size = desc_u16(NPU_PROGRAM_DESC_OFF_SIZE_BYTES);
        desc_version = desc_byte(NPU_PROGRAM_DESC_OFF_VERSION);
        desc_program_format = desc_byte(NPU_PROGRAM_DESC_OFF_PROGRAM_FORMAT);
        desc_holon_isa_major = desc_u16(NPU_PROGRAM_DESC_OFF_HOLON_ISA_MAJOR);
        desc_holon_isa_minor = desc_u16(NPU_PROGRAM_DESC_OFF_HOLON_ISA_MINOR);
        desc_required_caps = desc_u64(NPU_PROGRAM_DESC_OFF_REQUIRED_CAPS);
        desc_required_op_classes = desc_u64(NPU_PROGRAM_DESC_OFF_REQUIRED_OP_CLASSES);
        desc_code_addr = desc_u64(NPU_PROGRAM_DESC_OFF_CODE_ADDR);
        desc_code_size_bytes = desc_u32(NPU_PROGRAM_DESC_OFF_CODE_SIZE);
        desc_entry_pc = desc_u32(NPU_PROGRAM_DESC_OFF_ENTRY_PC);
        desc_arg_addr = desc_u64(NPU_PROGRAM_DESC_OFF_ARG_ADDR);
        desc_arg_size_bytes = desc_u32(NPU_PROGRAM_DESC_OFF_ARG_SIZE);
        desc_local_mem_bytes = desc_u32(NPU_PROGRAM_DESC_OFF_LOCAL_MEM_BYTES);
        desc_program_mem_bytes = desc_u32(NPU_PROGRAM_DESC_OFF_PROGRAM_MEM_BYTES);
        desc_stack_bytes = desc_u32(NPU_PROGRAM_DESC_OFF_STACK_BYTES);
        desc_completion_addr = desc_u64(NPU_PROGRAM_DESC_OFF_COMPLETION_ADDR);
        desc_flags = desc_u32(NPU_PROGRAM_DESC_OFF_FLAGS);
        desc_reserved_4c = desc_u32(NPU_PROGRAM_DESC_OFF_RESERVED_4C);

        reserved_tail_nonzero = 1'b0;
        for (
            int unsigned offset = NPU_PROGRAM_DESC_OFF_RESERVED_TAIL;
            offset < NPU_PROGRAM_DESC_SIZE;
            offset++
        ) begin
            reserved_tail_nonzero |= desc_byte(offset) != 8'h00;
        end

        validation_fault = 1'b0;
        validation_fault_code = NPU_FAULT_NONE;
        if ((desc_size != 16'(NPU_PROGRAM_DESC_SIZE)) ||
            (desc_reserved_4c != 32'h0000_0000) ||
            reserved_tail_nonzero ||
            ((desc_flags & ~NPU_PROGRAM_FLAG_VALID_MASK) != 32'h0000_0000)) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_FAULT_INVALID_PROGRAM_DESCRIPTOR;
        end else if ((desc_version != 8'(NPU_ABI_MAJOR)) ||
                     (desc_holon_isa_major != IMPLEMENTED_ISA_MAJOR) ||
                     (desc_holon_isa_minor > IMPLEMENTED_ISA_MINOR)) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_FAULT_UNSUPPORTED_ABI_OR_ISA;
        end else if (desc_program_format != 8'(NPU_PROGRAM_FORMAT_HOLON)) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_FAULT_UNSUPPORTED_PROGRAM_FORMAT;
        end else if ((desc_required_caps & ~IMPLEMENTED_CAPS) != 64'h0000_0000_0000_0000) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_FAULT_UNSUPPORTED_CAPABILITY;
        end else if ((desc_required_op_classes & ~IMPLEMENTED_OP_CLASSES) != 64'h0000_0000_0000_0000) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_FAULT_UNSUPPORTED_OPERATION_CLASS;
        end else if (!range_fits_u64(desc_code_addr, desc_code_size_bytes) ||
                     !range_fits_u64(desc_arg_addr, desc_arg_size_bytes) ||
                     ((desc_completion_addr != 64'h0000_0000_0000_0000) &&
                      !range_fits_u64(
                          desc_completion_addr,
                          32'(NPU_COMPLETION_RECORD_SIZE)
                      ))) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_FAULT_INVALID_PROGRAM_DESCRIPTOR;
        end else if (!aligned_to(desc_code_addr, NPU_PROGRAM_IMAGE_ALIGN) ||
                     !aligned_to(64'(desc_code_size_bytes), NPU_PROGRAM_IMAGE_ALIGN) ||
                     !aligned_to(64'(desc_entry_pc), NPU_PROGRAM_IMAGE_ALIGN) ||
                     !aligned_to(desc_arg_addr, NPU_PROGRAM_ARGUMENT_ALIGN) ||
                     !aligned_to(64'(desc_arg_size_bytes), NPU_PROGRAM_ARGUMENT_ALIGN) ||
                     ((desc_completion_addr != 64'h0000_0000_0000_0000) &&
                      !aligned_to(desc_completion_addr, NPU_PROGRAM_COMPLETION_ALIGN))) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_FAULT_ALIGNMENT;
        end else if ((desc_code_size_bytes == 32'h0000_0000) ||
                     (desc_program_mem_bytes < desc_code_size_bytes) ||
                     (desc_local_mem_bytes < desc_arg_size_bytes) ||
                     (desc_program_mem_bytes > 32'(NPU_PROGRAM_MEM_MAX_BYTES)) ||
                     (desc_local_mem_bytes > 32'(NPU_LOCAL_MEM_MAX_BYTES)) ||
                     (desc_stack_bytes > 32'(NPU_PROGRAM_STACK_MAX_BYTES)) ||
                     (({1'b0, desc_arg_size_bytes} + {1'b0, desc_stack_bytes}) >
                      {1'b0, desc_local_mem_bytes}) ||
                     (desc_entry_pc >= desc_code_size_bytes)) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_FAULT_LOCAL_MEMORY_BOUNDS;
        end
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state_q <= STATE_IDLE;
            phase_q <= PHASE_DESC;
            desc_addr_q <= '0;
            desc_beat_q <= '0;
            desc_burst_beats_q <= '0;
            desc_burst_seen_q <= '0;
            fault_code_q <= NPU_FAULT_NONE;
            copy_addr_q <= '0;
            copy_local_offset_q <= '0;
            copy_words_remaining_q <= '0;
            copy_burst_words_q <= '0;
            copy_burst_seen_q <= '0;
            for (int unsigned i = 0; i < DESC_BEATS; i++) begin
                desc_q[i] <= '0;
            end
        end else if (soft_reset_i) begin
            state_q <= STATE_IDLE;
            phase_q <= PHASE_DESC;
            desc_addr_q <= '0;
            desc_beat_q <= '0;
            desc_burst_beats_q <= '0;
            desc_burst_seen_q <= '0;
            fault_code_q <= NPU_FAULT_NONE;
            copy_addr_q <= '0;
            copy_local_offset_q <= '0;
            copy_words_remaining_q <= '0;
            copy_burst_words_q <= '0;
            copy_burst_seen_q <= '0;
            for (int unsigned i = 0; i < DESC_BEATS; i++) begin
                desc_q[i] <= '0;
            end
        end else begin
            unique case (state_q)
                STATE_IDLE,
                STATE_DONE,
                STATE_FAULT: begin
                    if (start_i) begin
                        fault_code_q <= NPU_FAULT_NONE;
                        phase_q <= PHASE_DESC;
                        desc_beat_q <= '0;
                        desc_burst_beats_q <= '0;
                        desc_burst_seen_q <= '0;
                        copy_addr_q <= '0;
                        copy_local_offset_q <= '0;
                        copy_words_remaining_q <= '0;
                        copy_burst_words_q <= '0;
                        copy_burst_seen_q <= '0;
                        for (int unsigned i = 0; i < DESC_BEATS; i++) begin
                            desc_q[i] <= '0;
                        end
                        if (!desc_addr_aligned) begin
                            state_q <= STATE_FAULT;
                            fault_code_q <= NPU_FAULT_ALIGNMENT;
                        end else if (!desc_addr_range_valid) begin
                            state_q <= STATE_FAULT;
                            fault_code_q <= NPU_FAULT_AXI_READ;
                        end else begin
                            state_q <= STATE_AR;
                            desc_addr_q <= desc_addr_i;
                        end
                    end
                end

                STATE_AR: begin
                    if (m_axi.arready) begin
                        state_q <= STATE_R;
                        if (phase_q == PHASE_DESC) begin
                            desc_burst_beats_q <= desc_ar_beats;
                            desc_burst_seen_q <= '0;
                        end else if (copy_phase) begin
                            copy_burst_words_q <= copy_ar_words;
                            copy_burst_seen_q <= '0;
                        end
                    end
                end

                STATE_R: begin
                    if (r_fire) begin
                        if (!r_resp_ok) begin
                            fault_code_q <= NPU_FAULT_AXI_READ;
                            state_q <= m_axi.rlast ? STATE_FAULT : STATE_DRAIN;
                        end else if (phase_q == PHASE_DESC) begin
                            desc_q[desc_beat_q] <= m_axi.rdata;
                            desc_addr_q <= desc_addr_q + ADDR_W'(BEAT_BYTES);
                            if (m_axi.rlast !=
                                (desc_burst_seen_q == (desc_burst_beats_q - 5'd1))) begin
                                state_q <= m_axi.rlast ? STATE_FAULT : STATE_DRAIN;
                                fault_code_q <= NPU_FAULT_AXI_READ;
                            end else if (m_axi.rlast) begin
                                if (final_desc_beat) begin
                                    state_q <= STATE_CHECK;
                                end else begin
                                    desc_beat_q <= desc_beat_q + DESC_BEAT_W'(1);
                                    state_q <= STATE_AR;
                                end
                            end else begin
                                desc_beat_q <= desc_beat_q + DESC_BEAT_W'(1);
                                desc_burst_seen_q <= desc_burst_seen_q + 5'd1;
                            end
                        end else if (m_axi.rlast != copy_burst_last_expected) begin
                            fault_code_q <= NPU_FAULT_AXI_READ;
                            state_q <= m_axi.rlast ? STATE_FAULT : STATE_DRAIN;
                        end else begin
                            copy_addr_q <= copy_addr_q + ADDR_W'(WORD_BYTES);
                            copy_local_offset_q <= copy_local_offset_q + 32'(WORD_BYTES);
                            copy_words_remaining_q <= copy_words_remaining_q - 32'd1;
                            copy_burst_seen_q <= copy_burst_seen_q + 5'd1;
                            state_q <= STATE_WRITE_RESP;
                        end
                    end
                end

                STATE_WRITE_RESP: begin
                    if (copy_write_resp_valid) begin
                        if (copy_write_resp_error) begin
                            state_q <= STATE_FAULT;
                            fault_code_q <= NPU_FAULT_LOCAL_MEMORY_BOUNDS;
                        end else if (copy_words_remaining_q == 32'd0) begin
                            if ((phase_q == PHASE_CODE) &&
                                (desc_arg_size_bytes != 32'h0000_0000)) begin
                                phase_q <= PHASE_ARG;
                                state_q <= STATE_AR;
                                copy_addr_q <= ADDR_W'(desc_arg_addr);
                                copy_local_offset_q <= '0;
                                copy_words_remaining_q <= desc_arg_size_bytes >> WORD_SHIFT;
                            end else begin
                                state_q <= STATE_DONE;
                                fault_code_q <= NPU_FAULT_NONE;
                            end
                        end else if (copy_burst_seen_q == copy_burst_words_q) begin
                            state_q <= STATE_AR;
                        end else begin
                            state_q <= STATE_R;
                        end
                    end
                end

                STATE_DRAIN: begin
                    if (r_fire && m_axi.rlast) begin
                        state_q <= STATE_FAULT;
                    end
                end

                STATE_CHECK: begin
                    if (validation_fault) begin
                        state_q <= STATE_FAULT;
                        fault_code_q <= validation_fault_code;
                    end else begin
                        phase_q <= PHASE_CODE;
                        state_q <= STATE_AR;
                        fault_code_q <= NPU_FAULT_NONE;
                        copy_addr_q <= ADDR_W'(desc_code_addr);
                        copy_local_offset_q <= '0;
                        copy_words_remaining_q <= desc_code_size_bytes >> WORD_SHIFT;
                    end
                end

                default: begin
                    state_q <= STATE_FAULT;
                    fault_code_q <= NPU_FAULT_INVALID_PROGRAM_DESCRIPTOR;
                end
            endcase
        end
    end

     loader_terminal_states_exclusive: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            !(done_o && fault_o)
    );
     loader_descriptor_burst_profile: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            (m_axi.arvalid && (phase_q == PHASE_DESC)) |->
                (m_axi.araddr[BEAT_SHIFT-1:0] == '0) &&
                (m_axi.arlen <= 8'(DESC_BEATS - 1)) &&
                (m_axi.arsize == 3'(BEAT_SHIFT)) &&
                (m_axi.arburst == AXI_BURST_INCR)
    );
     loader_descriptor_burst_within_4k: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            (m_axi.arvalid && (phase_q == PHASE_DESC)) |->
                ({1'b0, m_axi.araddr[11:0]} +
                 ((13'(m_axi.arlen) + 13'd1) << m_axi.arsize)) <= 13'd4096
    );
     loader_copy_burst_profile: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            (m_axi.arvalid && copy_phase) |->
                (m_axi.araddr[WORD_SHIFT-1:0] == '0) &&
                (m_axi.arlen <= 8'(COPY_BURST_MAX_WORDS - 1)) &&
                (m_axi.arsize == 3'(WORD_SHIFT)) &&
                (m_axi.arburst == AXI_BURST_INCR)
    );
     loader_copy_burst_within_4k: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            (m_axi.arvalid && copy_phase) |->
                ({1'b0, m_axi.araddr[11:0]} +
                 ((13'(m_axi.arlen) + 13'd1) << m_axi.arsize)) <= 13'd4096
    );
     loader_descriptor_page_split_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni)
            m_axi.arvalid && (phase_q == PHASE_DESC) &&
            (desc_beat_q != '0)
    );
     loader_copy_page_split_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni)
            m_axi.arvalid && copy_phase && (copy_addr_q[11:0] == 12'h000) &&
            (copy_local_offset_q != 32'd0)
    );
     loader_fault_has_code: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            fault_o |-> (fault_code_o != NPU_FAULT_NONE)
    );
     loader_done_has_valid_descriptor: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            done_o |-> !validation_fault
    );
     loader_program_write_in_bounds: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            program_wr.req_valid |-> (program_wr.req_addr < desc_program_mem_bytes)
    );
     loader_data_write_in_bounds: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            data_wr.req_valid |-> (data_wr.req_addr < desc_local_mem_bytes)
    );

endmodule

/* verilator lint_on DECLFILENAME */
