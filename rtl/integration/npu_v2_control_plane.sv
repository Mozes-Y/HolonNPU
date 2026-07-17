/* verilator lint_off DECLFILENAME */

module npu_v2_control_plane_core #(
    parameter int unsigned AXI_ADDR_W = 64,
    parameter int unsigned AXI_DATA_W = 128
) (
    npu_axi_lite_if.slave         s_axil,

    input  logic                  frontend_done_i,
    input  logic                  frontend_fault_i,
    input  logic [31:0]           frontend_fault_code_i,
    input  logic                  frontend_halted_i,
    input  logic [31:0]           frontend_debug_pc_i,
    input  logic [63:0]           frontend_instret_i,

    output logic                  soft_reset_o,
    output logic                  halt_request_o,
    output logic                  resume_o,
    output logic                  debug_step_o,
    output logic                  clear_perf_o,
    output logic [63:0]           perf_cycle_o,
    output logic                  irq_o,

    output logic [7:0]            program_format_o,
    output logic [15:0]           holon_isa_major_o,
    output logic [15:0]           holon_isa_minor_o,
    output logic [63:0]           required_caps_o,
    output logic [63:0]           required_op_classes_o,
    output logic [63:0]           code_addr_o,
    output logic [31:0]           code_size_bytes_o,
    output logic [31:0]           entry_pc_o,
    output logic [63:0]           arg_addr_o,
    output logic [31:0]           arg_size_bytes_o,
    output logic [31:0]           local_mem_bytes_o,
    output logic [31:0]           program_mem_bytes_o,
    output logic [31:0]           stack_bytes_o,
    output logic [63:0]           completion_addr_o,
    output logic [31:0]           flags_o,

    input  logic                  program_rd_valid_i,
    input  logic [31:0]           program_rd_addr_i,
    output logic                  program_rd_valid_o,
    output logic [31:0]           program_rd_data_o,
    output logic                  program_rd_error_o,

    input  logic                  data_rd_valid_i,
    output logic                  data_rd_ready_o,
    input  logic [31:0]           data_rd_addr_i,
    output logic                  data_rd_valid_o,
    output logic [31:0]           data_rd_data_o,
    output logic                  data_rd_error_o,

    npu_v2_localmem_wr_if.slave   data_client_wr,
    npu_v2_localmem_rd_if.slave   data_client_rd,

    output logic                  loader_busy_o,
    output logic                  loader_done_o,
    output logic                  loader_fault_o,
    output logic [31:0]           loader_fault_code_o,

    npu_axi4_if.read_master       m_axi
);

    import npu_v2_pkg::*;

    logic        loader_start;
    logic [63:0] loader_desc_addr;
    logic        control_soft_reset;
    logic        loader_done_raw;
    logic        loader_fault_raw;
    logic [31:0] loader_fault_code_raw;
    logic        loader_active_q;
    logic        loader_done_q;
    logic        loader_fault_q;
    logic [31:0] loader_fault_code_q;
    logic        loader_done_for_control;
    logic        loader_fault_for_control;

    logic        merged_fault;
    logic [31:0] merged_fault_code;
    logic [31:0] merged_debug_pc;
    logic        local_memory_fault;
    logic        frontend_event_uses_program_flags;
    logic        irq_on_done;
    logic        irq_on_fault;
    logic        debug_snapshot_on_fault;
    logic        clear_perf_on_start;

    npu_v2_localmem_wr_if program_wr_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_v2_localmem_wr_if data_wr_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_v2_localmem_wr_if data_arb_wr_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_v2_localmem_rd_if program_rd_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_v2_localmem_rd_if host_data_rd_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_v2_localmem_rd_if memory_data_rd_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    assign program_rd_if.req_valid = program_rd_valid_i;
    assign program_rd_if.req_addr = program_rd_addr_i;
    assign program_rd_valid_o = program_rd_if.resp_valid;
    assign program_rd_data_o = program_rd_if.resp_data;
    assign program_rd_error_o = program_rd_if.resp_error;

    assign host_data_rd_if.req_valid = data_rd_valid_i;
    assign data_rd_ready_o = host_data_rd_if.req_ready;
    assign host_data_rd_if.req_addr = data_rd_addr_i;
    assign data_rd_valid_o = host_data_rd_if.resp_valid;
    assign data_rd_data_o = host_data_rd_if.resp_data;
    assign data_rd_error_o = host_data_rd_if.resp_error;

    assign local_memory_fault = (program_wr_if.resp_valid && program_wr_if.resp_error) ||
                                (data_arb_wr_if.resp_valid && data_arb_wr_if.resp_error);
    assign loader_done_for_control = loader_done_q && !loader_start;
    assign loader_fault_for_control = loader_fault_q && !loader_start;
    assign merged_fault = loader_fault_for_control || local_memory_fault || frontend_fault_i;
    assign merged_fault_code = loader_fault_for_control ? loader_fault_code_q :
                               local_memory_fault ? NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS :
                               frontend_fault_code_i;
    assign merged_debug_pc = (loader_fault_for_control || local_memory_fault) ? 32'h0000_0000 :
                             frontend_debug_pc_i;
    assign frontend_event_uses_program_flags = !loader_fault_for_control &&
                                               !local_memory_fault &&
                                               (frontend_done_i || frontend_fault_i);
    assign irq_on_done = (flags_o & NPU_V2_PROGRAM_FLAG_IRQ_ON_DONE) != 32'd0;
    assign irq_on_fault = !frontend_event_uses_program_flags ||
                          ((flags_o & NPU_V2_PROGRAM_FLAG_IRQ_ON_FAULT) != 32'd0);
    assign debug_snapshot_on_fault = frontend_event_uses_program_flags &&
        ((flags_o & NPU_V2_PROGRAM_FLAG_DEBUG_SNAPSHOT_ON_FAULT) != 32'd0);
    assign clear_perf_on_start =
        (flags_o & NPU_V2_PROGRAM_FLAG_CLEAR_PERF_ON_START) != 32'd0;

    assign loader_done_o = loader_done_q;
    assign loader_fault_o = loader_fault_q;
    assign loader_fault_code_o = loader_fault_code_q;
    assign soft_reset_o = control_soft_reset;

    always_ff @(posedge s_axil.aclk_i or negedge s_axil.aresetn_i) begin
        if (!s_axil.aresetn_i) begin
            loader_active_q <= 1'b0;
            loader_done_q <= 1'b0;
            loader_fault_q <= 1'b0;
            loader_fault_code_q <= NPU_V2_FAULT_NONE;
        end else if (control_soft_reset) begin
            loader_active_q <= 1'b0;
            loader_done_q <= 1'b0;
            loader_fault_q <= 1'b0;
            loader_fault_code_q <= NPU_V2_FAULT_NONE;
        end else begin
            if (loader_start) begin
                loader_active_q <= 1'b1;
                loader_done_q <= 1'b0;
                loader_fault_q <= 1'b0;
                loader_fault_code_q <= NPU_V2_FAULT_NONE;
            end else if (loader_active_q && loader_done_raw) begin
                loader_active_q <= 1'b0;
                loader_done_q <= 1'b1;
                loader_fault_q <= 1'b0;
                loader_fault_code_q <= NPU_V2_FAULT_NONE;
            end else if (loader_active_q && loader_fault_raw) begin
                loader_active_q <= 1'b0;
                loader_done_q <= 1'b0;
                loader_fault_q <= 1'b1;
                loader_fault_code_q <= loader_fault_code_raw;
            end
        end
    end

    npu_v2_control_regs_core u_control (
        .s_axil(s_axil),
        .loader_done_i(loader_done_for_control),
        .frontend_done_i(frontend_done_i),
        .frontend_fault_i(merged_fault),
        .frontend_fault_code_i(merged_fault_code),
        .frontend_halted_i(frontend_halted_i),
        .frontend_debug_pc_i(merged_debug_pc),
        .frontend_instret_i(frontend_instret_i),
        .irq_on_done_i(irq_on_done),
        .irq_on_fault_i(irq_on_fault),
        .debug_snapshot_on_fault_i(debug_snapshot_on_fault),
        .clear_perf_on_start_i(clear_perf_on_start),
        .program_start_o(loader_start),
        .program_desc_addr_o(loader_desc_addr),
        .soft_reset_o(control_soft_reset),
        .halt_request_o(halt_request_o),
        .resume_o(resume_o),
        .debug_step_o(debug_step_o),
        .clear_perf_o(clear_perf_o),
        .perf_cycle_o(perf_cycle_o),
        .irq_o(irq_o)
    );

    npu_v2_program_loader_core #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W)
    ) u_loader (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i),
        .soft_reset_i(control_soft_reset),
        .start_i(loader_start),
        .desc_addr_i(AXI_ADDR_W'(loader_desc_addr)),
        .busy_o(loader_busy_o),
        .done_o(loader_done_raw),
        .fault_o(loader_fault_raw),
        .fault_code_o(loader_fault_code_raw),
        .program_format_o(program_format_o),
        .holon_isa_major_o(holon_isa_major_o),
        .holon_isa_minor_o(holon_isa_minor_o),
        .required_caps_o(required_caps_o),
        .required_op_classes_o(required_op_classes_o),
        .code_addr_o(code_addr_o),
        .code_size_bytes_o(code_size_bytes_o),
        .entry_pc_o(entry_pc_o),
        .arg_addr_o(arg_addr_o),
        .arg_size_bytes_o(arg_size_bytes_o),
        .local_mem_bytes_o(local_mem_bytes_o),
        .program_mem_bytes_o(program_mem_bytes_o),
        .stack_bytes_o(stack_bytes_o),
        .completion_addr_o(completion_addr_o),
        .flags_o(flags_o),
        .program_wr(program_wr_if),
        .data_wr(data_wr_if),
        .m_axi(m_axi)
    );

    npu_v2_data_port_arbiter_core u_data_arbiter (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i),
        .soft_reset_i(control_soft_reset),
        .loader_wr(data_wr_if),
        .client_wr(data_client_wr),
        .memory_wr(data_arb_wr_if),
        .host_rd(host_data_rd_if),
        .client_rd(data_client_rd),
        .memory_rd(memory_data_rd_if)
    );

    npu_v2_local_memory_core u_local_memory (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i),
        .soft_reset_i(control_soft_reset),
        .program_wr(program_wr_if),
        .data_wr(data_arb_wr_if),
        .program_rd(program_rd_if),
        .data_rd(memory_data_rd_if)
    );

    v2_control_plane_loader_start_profile: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            loader_start |-> !loader_busy_o
    );
    v2_control_plane_loader_fault_has_code: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            loader_fault_q |-> (loader_fault_code_q != NPU_V2_FAULT_NONE)
    );
    v2_control_plane_local_memory_fault_has_code: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            local_memory_fault |-> (merged_fault_code == NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS)
    );

endmodule

/* verilator lint_on DECLFILENAME */
