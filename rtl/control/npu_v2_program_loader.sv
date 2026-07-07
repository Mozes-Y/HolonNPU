/* verilator lint_off DECLFILENAME */

module npu_v2_program_loader_core #(
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

    npu_axi4_if.read_master         m_axi
);

    import npu_v2_pkg::*;

    localparam int unsigned BEAT_BYTES = DATA_W / 8;
    localparam int unsigned BEAT_SHIFT = $clog2(BEAT_BYTES);
    localparam int unsigned DESC_BEATS = NPU_V2_PROGRAM_DESC_SIZE / BEAT_BYTES;
    localparam int unsigned DESC_BEAT_W = $clog2(DESC_BEATS);

    typedef enum logic [2:0] {
        STATE_IDLE  = 3'd0,
        STATE_AR    = 3'd1,
        STATE_R     = 3'd2,
        STATE_DRAIN = 3'd3,
        STATE_CHECK = 3'd4,
        STATE_DONE  = 3'd5,
        STATE_FAULT = 3'd6
    } state_e;

    localparam logic [1:0] AXI_RESP_OKAY = 2'b00;
    localparam logic [1:0] AXI_RESP_EXOKAY = 2'b01;
    localparam logic [1:0] AXI_BURST_INCR = 2'b01;
    localparam logic [63:0] IMPLEMENTED_CAPS = {
        NPU_V2_RESET_CAP0_HI,
        NPU_V2_RESET_CAP0_LO
    };
    localparam logic [63:0] IMPLEMENTED_OP_CLASSES = {
        NPU_V2_RESET_OP_CLASS_HI,
        NPU_V2_RESET_OP_CLASS_LO
    };
    localparam logic [15:0] IMPLEMENTED_ISA_MAJOR = 16'(NPU_V2_RESET_ISA_VERSION >> 16);
    localparam logic [15:0] IMPLEMENTED_ISA_MINOR = 16'(NPU_V2_RESET_ISA_VERSION);

    state_e state_q;
    logic [ADDR_W-1:0] desc_addr_q;
    logic [DESC_BEAT_W-1:0] desc_beat_q;
    logic [DATA_W-1:0] desc_q [DESC_BEATS];
    logic [31:0] fault_code_q;

    logic r_fire;
    logic r_resp_ok;
    logic final_desc_beat;
    logic desc_addr_aligned;
    logic validation_fault;
    logic [31:0] validation_fault_code;
    logic reserved_tail_nonzero;

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
                    (state_q == STATE_DRAIN) || (state_q == STATE_CHECK);
    assign done_o = (state_q == STATE_DONE);
    assign fault_o = (state_q == STATE_FAULT);
    assign fault_code_o = fault_code_q;

    assign r_fire = m_axi.rvalid && m_axi.rready;
    assign r_resp_ok = (m_axi.rresp == AXI_RESP_OKAY) ||
                       (m_axi.rresp == AXI_RESP_EXOKAY);
    assign final_desc_beat = (desc_beat_q == DESC_BEAT_W'(DESC_BEATS - 1));
    assign desc_addr_aligned = desc_addr_i[BEAT_SHIFT-1:0] == '0;

    assign m_axi.arid = '0;
    assign m_axi.araddr = desc_addr_q;
    assign m_axi.arlen = 8'(DESC_BEATS - 1);
    assign m_axi.arsize = 3'(BEAT_SHIFT);
    assign m_axi.arburst = AXI_BURST_INCR;
    assign m_axi.arvalid = (state_q == STATE_AR);
    assign m_axi.rready = (state_q == STATE_R) || (state_q == STATE_DRAIN);

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

    function automatic logic aligned_to(input logic [63:0] value, input int unsigned alignment);
        aligned_to = (value & 64'(alignment - 1)) == 64'h0000_0000_0000_0000;
    endfunction

    always_comb begin
        desc_size = desc_u16(NPU_V2_PROGRAM_DESC_OFF_SIZE_BYTES);
        desc_version = desc_byte(NPU_V2_PROGRAM_DESC_OFF_VERSION);
        desc_program_format = desc_byte(NPU_V2_PROGRAM_DESC_OFF_PROGRAM_FORMAT);
        desc_holon_isa_major = desc_u16(NPU_V2_PROGRAM_DESC_OFF_HOLON_ISA_MAJOR);
        desc_holon_isa_minor = desc_u16(NPU_V2_PROGRAM_DESC_OFF_HOLON_ISA_MINOR);
        desc_required_caps = desc_u64(NPU_V2_PROGRAM_DESC_OFF_REQUIRED_CAPS);
        desc_required_op_classes = desc_u64(NPU_V2_PROGRAM_DESC_OFF_REQUIRED_OP_CLASSES);
        desc_code_addr = desc_u64(NPU_V2_PROGRAM_DESC_OFF_CODE_ADDR);
        desc_code_size_bytes = desc_u32(NPU_V2_PROGRAM_DESC_OFF_CODE_SIZE);
        desc_entry_pc = desc_u32(NPU_V2_PROGRAM_DESC_OFF_ENTRY_PC);
        desc_arg_addr = desc_u64(NPU_V2_PROGRAM_DESC_OFF_ARG_ADDR);
        desc_arg_size_bytes = desc_u32(NPU_V2_PROGRAM_DESC_OFF_ARG_SIZE);
        desc_local_mem_bytes = desc_u32(NPU_V2_PROGRAM_DESC_OFF_LOCAL_MEM_BYTES);
        desc_program_mem_bytes = desc_u32(NPU_V2_PROGRAM_DESC_OFF_PROGRAM_MEM_BYTES);
        desc_stack_bytes = desc_u32(NPU_V2_PROGRAM_DESC_OFF_STACK_BYTES);
        desc_completion_addr = desc_u64(NPU_V2_PROGRAM_DESC_OFF_COMPLETION_ADDR);
        desc_flags = desc_u32(NPU_V2_PROGRAM_DESC_OFF_FLAGS);
        desc_reserved_4c = desc_u32(NPU_V2_PROGRAM_DESC_OFF_RESERVED_4C);

        reserved_tail_nonzero = 1'b0;
        for (
            int unsigned offset = NPU_V2_PROGRAM_DESC_OFF_RESERVED_TAIL;
            offset < NPU_V2_PROGRAM_DESC_SIZE;
            offset++
        ) begin
            reserved_tail_nonzero |= desc_byte(offset) != 8'h00;
        end

        validation_fault = 1'b0;
        validation_fault_code = NPU_V2_FAULT_NONE;
        if ((desc_size != 16'(NPU_V2_PROGRAM_DESC_SIZE)) ||
            (desc_reserved_4c != 32'h0000_0000) ||
            reserved_tail_nonzero ||
            ((desc_flags & ~NPU_V2_PROGRAM_FLAG_VALID_MASK) != 32'h0000_0000)) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_V2_FAULT_INVALID_PROGRAM_DESCRIPTOR;
        end else if ((desc_version != 8'(NPU_V2_ABI_MAJOR)) ||
                     (desc_holon_isa_major != IMPLEMENTED_ISA_MAJOR) ||
                     (desc_holon_isa_minor > IMPLEMENTED_ISA_MINOR)) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_V2_FAULT_UNSUPPORTED_ABI_OR_ISA;
        end else if (desc_program_format != 8'(NPU_V2_PROGRAM_FORMAT_HOLON_V2)) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_V2_FAULT_UNSUPPORTED_PROGRAM_FORMAT;
        end else if ((desc_required_caps & ~IMPLEMENTED_CAPS) != 64'h0000_0000_0000_0000) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_V2_FAULT_UNSUPPORTED_CAPABILITY;
        end else if ((desc_required_op_classes & ~IMPLEMENTED_OP_CLASSES) != 64'h0000_0000_0000_0000) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_V2_FAULT_UNSUPPORTED_OPERATION_CLASS;
        end else if (!aligned_to(desc_code_addr, NPU_V2_PROGRAM_IMAGE_ALIGN) ||
                     !aligned_to(64'(desc_code_size_bytes), NPU_V2_PROGRAM_IMAGE_ALIGN) ||
                     !aligned_to(64'(desc_entry_pc), NPU_V2_PROGRAM_IMAGE_ALIGN) ||
                     !aligned_to(desc_arg_addr, NPU_V2_PROGRAM_ARGUMENT_ALIGN) ||
                     !aligned_to(64'(desc_arg_size_bytes), NPU_V2_PROGRAM_ARGUMENT_ALIGN) ||
                     ((desc_completion_addr != 64'h0000_0000_0000_0000) &&
                      !aligned_to(desc_completion_addr, NPU_V2_PROGRAM_COMPLETION_ALIGN))) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_V2_FAULT_ALIGNMENT;
        end else if ((desc_code_size_bytes == 32'h0000_0000) ||
                     (desc_program_mem_bytes > 32'(NPU_V2_PROGRAM_MEM_MAX_BYTES)) ||
                     (desc_local_mem_bytes > 32'(NPU_V2_LOCAL_MEM_MAX_BYTES)) ||
                     (desc_stack_bytes > 32'(NPU_V2_PROGRAM_STACK_MAX_BYTES)) ||
                     (desc_entry_pc >= desc_code_size_bytes)) begin
            validation_fault = 1'b1;
            validation_fault_code = NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS;
        end
    end

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            state_q <= STATE_IDLE;
            desc_addr_q <= '0;
            desc_beat_q <= '0;
            fault_code_q <= NPU_V2_FAULT_NONE;
            for (int unsigned i = 0; i < DESC_BEATS; i++) begin
                desc_q[i] <= '0;
            end
        end else if (soft_reset_i) begin
            state_q <= STATE_IDLE;
            desc_addr_q <= '0;
            desc_beat_q <= '0;
            fault_code_q <= NPU_V2_FAULT_NONE;
            for (int unsigned i = 0; i < DESC_BEATS; i++) begin
                desc_q[i] <= '0;
            end
        end else begin
            unique case (state_q)
                STATE_IDLE,
                STATE_DONE,
                STATE_FAULT: begin
                    if (start_i) begin
                        fault_code_q <= NPU_V2_FAULT_NONE;
                        desc_beat_q <= '0;
                        for (int unsigned i = 0; i < DESC_BEATS; i++) begin
                            desc_q[i] <= '0;
                        end
                        if (!desc_addr_aligned) begin
                            state_q <= STATE_FAULT;
                            fault_code_q <= NPU_V2_FAULT_ALIGNMENT;
                        end else begin
                            state_q <= STATE_AR;
                            desc_addr_q <= desc_addr_i;
                        end
                    end
                end

                STATE_AR: begin
                    if (m_axi.arready) begin
                        state_q <= STATE_R;
                    end
                end

                STATE_R: begin
                    if (r_fire) begin
                        if (!r_resp_ok) begin
                            fault_code_q <= NPU_V2_FAULT_AXI_READ;
                            state_q <= m_axi.rlast ? STATE_FAULT : STATE_DRAIN;
                        end else begin
                            desc_q[desc_beat_q] <= m_axi.rdata;
                            if (m_axi.rlast) begin
                                if (final_desc_beat) begin
                                    state_q <= STATE_CHECK;
                                end else begin
                                    state_q <= STATE_FAULT;
                                    fault_code_q <= NPU_V2_FAULT_AXI_READ;
                                end
                            end else if (final_desc_beat) begin
                                state_q <= STATE_DRAIN;
                                fault_code_q <= NPU_V2_FAULT_AXI_READ;
                            end else begin
                                desc_beat_q <= desc_beat_q + DESC_BEAT_W'(1);
                            end
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
                        state_q <= STATE_DONE;
                        fault_code_q <= NPU_V2_FAULT_NONE;
                    end
                end

                default: begin
                    state_q <= STATE_FAULT;
                    fault_code_q <= NPU_V2_FAULT_INVALID_PROGRAM_DESCRIPTOR;
                end
            endcase
        end
    end

    v2_loader_terminal_states_exclusive: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            !(done_o && fault_o)
    );
    v2_loader_single_descriptor_burst: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            m_axi.arvalid |->
                (m_axi.araddr[BEAT_SHIFT-1:0] == '0) &&
                (m_axi.arlen == 8'(DESC_BEATS - 1)) &&
                (m_axi.arsize == 3'(BEAT_SHIFT)) &&
                (m_axi.arburst == AXI_BURST_INCR)
    );
    v2_loader_fault_has_code: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            fault_o |-> (fault_code_o != NPU_V2_FAULT_NONE)
    );
    v2_loader_done_has_valid_descriptor: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            done_o |-> !validation_fault
    );

endmodule

/* verilator lint_on DECLFILENAME */
