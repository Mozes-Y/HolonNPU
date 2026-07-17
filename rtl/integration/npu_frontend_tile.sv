/* verilator lint_off DECLFILENAME */

module npu_frontend_tile_core #(
    parameter int unsigned AXI_ADDR_W = 64,
    parameter int unsigned AXI_DATA_W = 128
) (
    npu_axi_lite_if.slave         s_axil,
    npu_axi4_if.master            m_axi,

    output logic                  irq_o,
    output logic                  loader_busy_o,
    output logic                  loader_done_o,
    output logic                  loader_fault_o,
    output logic [31:0]           loader_fault_code_o,
    output logic                  frontend_running_o,
    output logic                  frontend_halted_o,
    output logic                  frontend_done_o,
    output logic                  frontend_fault_o,
    output logic [31:0]           frontend_fault_code_o,
    output logic [31:0]           frontend_debug_pc_o,
    output logic [63:0]           frontend_instret_o
);

    import npu_pkg::*;
    import npu_isa_pkg::*;

    logic        control_soft_reset;
    logic        control_reset_clear;
    logic        reset_quiescent;
    logic        halt_request;
    logic        resume;
    logic        debug_step;
    logic [31:0] code_size_bytes;
    logic [31:0] entry_pc;
    logic [31:0] local_mem_bytes;
    logic [63:0] completion_addr;
    logic [31:0] program_flags;
    logic [63:0] perf_cycle;
    logic        loader_done_q;
    logic        frontend_start;
    logic        frontend_active_q;
    logic        completion_start_q;
    logic        completion_pending_q;
    logic        terminal_fault_q;
    logic [31:0] terminal_fault_code_q;
    logic [31:0] terminal_debug_pc_q;
    logic [63:0] terminal_cycle_q;
    logic [63:0] terminal_instret_q;
    logic        completion_busy;
    logic        completion_done;
    logic        completion_fault;
    logic [31:0] completion_fault_code;
    logic        control_frontend_done;
    logic        control_frontend_fault;
    logic [31:0] control_frontend_fault_code;
    logic [31:0] control_frontend_debug_pc;
    logic [63:0] control_frontend_instret;

    typedef enum logic [1:0] {
        READ_OWNER_NONE,
        READ_OWNER_LOADER,
        READ_OWNER_DMA
    } read_owner_e;

    read_owner_e read_owner_q;
    logic select_loader_ar;
    logic select_dma_ar;

    npu_frontend_if frontend_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W),
        .ID_W(1)
    ) loader_axi_if (
        .aclk_i(s_axil.aclk_i),
        .aresetn_i(s_axil.aresetn_i)
    );

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W),
        .ID_W(1)
    ) dma_axi_if (
        .aclk_i(s_axil.aclk_i),
        .aresetn_i(s_axil.aresetn_i)
    );

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W),
        .ID_W(1)
    ) completion_axi_if (
        .aclk_i(s_axil.aclk_i),
        .aresetn_i(s_axil.aresetn_i)
    );

    npu_axi4_if #(
        .ADDR_W(AXI_ADDR_W),
        .DATA_W(AXI_DATA_W),
        .ID_W(1)
    ) write_axi_if (
        .aclk_i(s_axil.aclk_i),
        .aresetn_i(s_axil.aresetn_i)
    );

    npu_localmem_wr_if dma_data_wr_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_localmem_wr_if vector_data_wr_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_localmem_wr_if matrix_data_wr_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_localmem_wr_if scalar_data_wr_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_localmem_wr_if engine_data_wr_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_vr_if #(.DATA_W(128)) vector_issue_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_vr_if #(.DATA_W(64)) vector_result_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_vr_if #(.DATA_W(128)) matrix_issue_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_vr_if #(.DATA_W(64)) matrix_result_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_localmem_rd_if dma_data_rd_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_localmem_rd_if vector_data_rd_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_localmem_rd_if matrix_data_rd_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_localmem_rd_if scalar_data_rd_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    npu_localmem_rd_if engine_data_rd_if (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i)
    );

    assign frontend_start = loader_done_o && !loader_done_q && !loader_fault_o;
    assign select_loader_ar = (read_owner_q == READ_OWNER_NONE) && loader_axi_if.arvalid;
    assign select_dma_ar = (read_owner_q == READ_OWNER_NONE) && !loader_axi_if.arvalid && dma_axi_if.arvalid;

    assign frontend_if.soft_reset = control_soft_reset;
    assign frontend_if.start = frontend_start;
    assign frontend_if.halt_request = halt_request;
    assign frontend_if.resume = resume;
    assign frontend_if.debug_step = debug_step;
    assign frontend_if.entry_pc = entry_pc;
    assign frontend_if.program_size_bytes = code_size_bytes;
    assign frontend_if.local_mem_bytes = local_mem_bytes;
    assign control_frontend_done =
        (frontend_active_q && frontend_if.done && (completion_addr == 64'd0)) ||
        (completion_done && !terminal_fault_q);
    assign control_frontend_fault =
        (frontend_active_q && frontend_if.fault && (completion_addr == 64'd0)) ||
        completion_fault || (completion_done && terminal_fault_q);
    assign control_frontend_fault_code = completion_fault ? completion_fault_code :
                                         (completion_done ? terminal_fault_code_q :
                                          frontend_if.fault_code);
    assign control_frontend_debug_pc = (completion_done || completion_fault)
        ? terminal_debug_pc_q : frontend_if.debug_pc;
    assign control_frontend_instret = (completion_done || completion_fault)
        ? terminal_instret_q : frontend_if.instret;
    assign vector_issue_if.valid = frontend_if.vector_issue_valid;
    assign vector_issue_if.data = frontend_if.vector_issue_data;
    assign frontend_if.vector_issue_ready = vector_issue_if.ready;
    assign frontend_if.vector_result_valid = vector_result_if.valid;
    assign frontend_if.vector_result_data = vector_result_if.data;
    assign vector_result_if.ready = frontend_if.vector_result_ready;
    assign matrix_issue_if.valid = frontend_if.matrix_issue_valid;
    assign matrix_issue_if.data = frontend_if.matrix_issue_data;
    assign frontend_if.matrix_issue_ready = matrix_issue_if.ready;
    assign frontend_if.matrix_result_valid = matrix_result_if.valid;
    assign frontend_if.matrix_result_data = matrix_result_if.data;
    assign matrix_result_if.ready = frontend_if.matrix_result_ready;
    assign frontend_if.sync_issue_ready = 1'b1;

    assign frontend_running_o = frontend_if.running;
    assign frontend_halted_o = frontend_if.halted;
    assign frontend_done_o = frontend_if.done;
    assign frontend_fault_o = frontend_if.fault;
    assign frontend_fault_code_o = frontend_if.fault_code;
    assign frontend_debug_pc_o = frontend_if.debug_pc;
    assign frontend_instret_o = frontend_if.instret;
    assign reset_quiescent = frontend_if.quiescent &&
                             !loader_busy_o &&
                             !completion_busy &&
                             !completion_pending_q &&
                             (read_owner_q == READ_OWNER_NONE) &&
                             !loader_axi_if.arvalid &&
                             !dma_axi_if.arvalid &&
                             !write_axi_if.awvalid &&
                             !write_axi_if.wvalid &&
                             !write_axi_if.bready;

    always_ff @(posedge s_axil.aclk_i or negedge s_axil.aresetn_i) begin
        if (!s_axil.aresetn_i) begin
            loader_done_q <= 1'b0;
            frontend_active_q <= 1'b0;
            completion_start_q <= 1'b0;
            completion_pending_q <= 1'b0;
            terminal_fault_q <= 1'b0;
            terminal_fault_code_q <= NPU_FAULT_NONE;
            terminal_debug_pc_q <= 32'd0;
            terminal_cycle_q <= 64'd0;
            terminal_instret_q <= 64'd0;
            read_owner_q <= READ_OWNER_NONE;
        end else if (control_reset_clear) begin
            loader_done_q <= 1'b0;
            frontend_active_q <= 1'b0;
            completion_start_q <= 1'b0;
            completion_pending_q <= 1'b0;
            terminal_fault_q <= 1'b0;
            terminal_fault_code_q <= NPU_FAULT_NONE;
            terminal_debug_pc_q <= 32'd0;
            terminal_cycle_q <= 64'd0;
            terminal_instret_q <= 64'd0;
            read_owner_q <= READ_OWNER_NONE;
        end else begin
            completion_start_q <= 1'b0;
            loader_done_q <= loader_done_o;
            if (frontend_start) begin
                frontend_active_q <= 1'b1;
                completion_pending_q <= 1'b0;
            end else if (frontend_active_q && (frontend_if.done || frontend_if.fault) &&
                         (completion_addr == 64'd0)) begin
                frontend_active_q <= 1'b0;
            end else if (frontend_active_q && (frontend_if.done || frontend_if.fault) &&
                         !completion_pending_q) begin
                completion_start_q <= 1'b1;
                completion_pending_q <= 1'b1;
                terminal_fault_q <= frontend_if.fault;
                terminal_fault_code_q <= frontend_if.fault ? frontend_if.fault_code :
                                         NPU_FAULT_NONE;
                terminal_debug_pc_q <= frontend_if.fault &&
                    ((program_flags & NPU_PROGRAM_FLAG_DEBUG_SNAPSHOT_ON_FAULT) == 32'd0)
                    ? 32'd0 : frontend_if.debug_pc;
                terminal_cycle_q <= perf_cycle;
                terminal_instret_q <= frontend_if.instret;
            end else if (completion_pending_q && (completion_done || completion_fault)) begin
                frontend_active_q <= 1'b0;
                completion_pending_q <= 1'b0;
            end

            if ((read_owner_q == READ_OWNER_NONE) && m_axi.arvalid && m_axi.arready) begin
                read_owner_q <= select_loader_ar ? READ_OWNER_LOADER : READ_OWNER_DMA;
            end else if ((read_owner_q != READ_OWNER_NONE) &&
                         m_axi.rvalid && m_axi.rready && m_axi.rlast) begin
                read_owner_q <= READ_OWNER_NONE;
            end
        end
    end

    assign m_axi.arid = select_loader_ar ? loader_axi_if.arid : dma_axi_if.arid;
    assign m_axi.araddr = select_loader_ar ? loader_axi_if.araddr : dma_axi_if.araddr;
    assign m_axi.arlen = select_loader_ar ? loader_axi_if.arlen : dma_axi_if.arlen;
    assign m_axi.arsize = select_loader_ar ? loader_axi_if.arsize : dma_axi_if.arsize;
    assign m_axi.arburst = select_loader_ar ? loader_axi_if.arburst : dma_axi_if.arburst;
    assign m_axi.arvalid = select_loader_ar || select_dma_ar;
    assign loader_axi_if.arready = select_loader_ar && m_axi.arready;
    assign dma_axi_if.arready = select_dma_ar && m_axi.arready;

    assign loader_axi_if.rid = m_axi.rid;
    assign loader_axi_if.rdata = m_axi.rdata;
    assign loader_axi_if.rresp = m_axi.rresp;
    assign loader_axi_if.rlast = m_axi.rlast;
    assign loader_axi_if.rvalid = (read_owner_q == READ_OWNER_LOADER) && m_axi.rvalid;
    assign dma_axi_if.rid = m_axi.rid;
    assign dma_axi_if.rdata = m_axi.rdata;
    assign dma_axi_if.rresp = m_axi.rresp;
    assign dma_axi_if.rlast = m_axi.rlast;
    assign dma_axi_if.rvalid = (read_owner_q == READ_OWNER_DMA) && m_axi.rvalid;
    assign m_axi.rready = ((read_owner_q == READ_OWNER_LOADER) && loader_axi_if.rready) ||
                          ((read_owner_q == READ_OWNER_DMA) && dma_axi_if.rready);
    assign m_axi.awid = write_axi_if.awid;
    assign m_axi.awaddr = write_axi_if.awaddr;
    assign m_axi.awlen = write_axi_if.awlen;
    assign m_axi.awsize = write_axi_if.awsize;
    assign m_axi.awburst = write_axi_if.awburst;
    assign m_axi.awvalid = write_axi_if.awvalid;
    assign write_axi_if.awready = m_axi.awready;
    assign m_axi.wdata = write_axi_if.wdata;
    assign m_axi.wstrb = write_axi_if.wstrb;
    assign m_axi.wlast = write_axi_if.wlast;
    assign m_axi.wvalid = write_axi_if.wvalid;
    assign write_axi_if.wready = m_axi.wready;
    assign write_axi_if.bid = m_axi.bid;
    assign write_axi_if.bresp = m_axi.bresp;
    assign write_axi_if.bvalid = m_axi.bvalid;
    assign m_axi.bready = write_axi_if.bready;
    /* verilator lint_off PINCONNECTEMPTY */
    npu_control_plane_core #(
        .AXI_ADDR_W(AXI_ADDR_W),
        .AXI_DATA_W(AXI_DATA_W)
    ) u_control_plane (
        .s_axil(s_axil),
        .frontend_done_i(control_frontend_done),
        .frontend_fault_i(control_frontend_fault),
        .frontend_fault_code_i(control_frontend_fault_code),
        .frontend_halted_i(frontend_active_q && frontend_if.halted),
        .frontend_debug_pc_i(frontend_active_q ? control_frontend_debug_pc : 32'h0000_0000),
        .frontend_instret_i(frontend_active_q ? control_frontend_instret : 64'h0000_0000_0000_0000),
        .reset_quiescent_i(reset_quiescent),
        .soft_reset_o(control_soft_reset),
        .reset_clear_o(control_reset_clear),
        .halt_request_o(halt_request),
        .resume_o(resume),
        .debug_step_o(debug_step),
        .clear_perf_o(),
        .perf_cycle_o(perf_cycle),
        .irq_o(irq_o),
        .program_format_o(),
        .holon_isa_major_o(),
        .holon_isa_minor_o(),
        .required_caps_o(),
        .required_op_classes_o(),
        .code_addr_o(),
        .code_size_bytes_o(code_size_bytes),
        .entry_pc_o(entry_pc),
        .arg_addr_o(),
        .arg_size_bytes_o(),
        .local_mem_bytes_o(local_mem_bytes),
        .program_mem_bytes_o(),
        .stack_bytes_o(),
        .completion_addr_o(completion_addr),
        .flags_o(program_flags),
        .program_rd_valid_i(frontend_if.program_rd_valid),
        .program_rd_addr_i(frontend_if.program_rd_addr),
        .program_rd_valid_o(frontend_if.program_rd_resp_valid),
        .program_rd_data_o(frontend_if.program_rd_resp_data),
        .program_rd_error_o(frontend_if.program_rd_resp_error),
        .data_client_wr(engine_data_wr_if),
        .data_client_rd(engine_data_rd_if),
        .loader_busy_o(loader_busy_o),
        .loader_done_o(loader_done_o),
        .loader_fault_o(loader_fault_o),
        .loader_fault_code_o(loader_fault_code_o),
        .m_axi(loader_axi_if)
    );
    /* verilator lint_on PINCONNECTEMPTY */

    npu_completion_writer_core #(
        .AXI_ADDR_W(AXI_ADDR_W),
        .AXI_DATA_W(AXI_DATA_W)
    ) u_completion_writer (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i),
        .soft_reset_i(control_reset_clear),
        .start_i(completion_start_q),
        .completion_addr_i(AXI_ADDR_W'(completion_addr)),
        .terminal_fault_i(terminal_fault_q),
        .fault_code_i(terminal_fault_code_q),
        .debug_pc_i(terminal_debug_pc_q),
        .cycle_count_i(terminal_cycle_q),
        .instret_i(terminal_instret_q),
        .busy_o(completion_busy),
        .done_o(completion_done),
        .fault_o(completion_fault),
        .fault_code_o(completion_fault_code),
        .m_axi(completion_axi_if)
    );

    npu_axi_write_arbiter_core u_write_arbiter (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i),
        .soft_reset_i(control_reset_clear),
        .dma(dma_axi_if),
        .completion(completion_axi_if),
        .m_axi(write_axi_if)
    );

     completion_precedes_control_terminal: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            !control_soft_reset && (completion_addr != 64'd0) &&
            (control_frontend_done || control_frontend_fault)
            |-> completion_done || completion_fault
    );
     completion_writer_not_restarted_while_pending: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            completion_start_q |-> !completion_busy
    );

    npu_dma_fabric_core #(
        .AXI_ADDR_W(AXI_ADDR_W),
        .AXI_DATA_W(AXI_DATA_W)
    ) u_dma_fabric (
        .frontend(frontend_if),
        .data_wr(dma_data_wr_if),
        .data_rd(dma_data_rd_if),
        .m_axi(dma_axi_if)
    );

    npu_vector_engine_core u_vector_engine (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i),
        .soft_reset_i(control_reset_clear),
        .local_mem_bytes_i(local_mem_bytes),
        .issue(vector_issue_if),
        .result(vector_result_if),
        .data_rd(vector_data_rd_if),
        .data_wr(vector_data_wr_if)
    );

    npu_matrix_engine_core u_matrix_engine (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i),
        .soft_reset_i(control_reset_clear),
        .local_mem_bytes_i(local_mem_bytes),
        .issue(matrix_issue_if),
        .result(matrix_result_if),
        .data_rd(matrix_data_rd_if),
        .data_wr(matrix_data_wr_if)
    );

    npu_engine_data_arbiter_core u_engine_data_arbiter (
        .clk_i(s_axil.aclk_i),
        .rst_ni(s_axil.aresetn_i),
        .soft_reset_i(control_reset_clear),
        .dma_wr(dma_data_wr_if),
        .vector_wr(vector_data_wr_if),
        .matrix_wr(matrix_data_wr_if),
        .scalar_wr(scalar_data_wr_if),
        .memory_wr(engine_data_wr_if),
        .dma_rd(dma_data_rd_if),
        .vector_rd(vector_data_rd_if),
        .matrix_rd(matrix_data_rd_if),
        .scalar_rd(scalar_data_rd_if),
        .memory_rd(engine_data_rd_if)
    );

    npu_reference_frontend_core u_frontend (
        .frontend(frontend_if),
        .data_rd(scalar_data_rd_if),
        .data_wr(scalar_data_wr_if)
    );

     frontend_tile_starts_after_loader_done: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            frontend_start |-> loader_done_o && !loader_fault_o
    );
     frontend_tile_no_loader_frontend_double_fault: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            loader_fault_o |-> !frontend_start
    );
     frontend_tile_program_fetch_after_start: cover property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            frontend_if.program_rd_valid
    );
     frontend_tile_read_owner_stable: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            (read_owner_q != READ_OWNER_NONE) && !(m_axi.rvalid && m_axi.rready && m_axi.rlast)
            |=> read_owner_q == $past(read_owner_q)
    );
     frontend_tile_dma_write_seen: cover property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            dma_data_wr_if.req_valid && dma_data_wr_if.req_ready
    );
     frontend_tile_vector_issue_seen: cover property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            vector_issue_if.valid && vector_issue_if.ready
    );
     frontend_tile_vector_result_seen: cover property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            vector_result_if.valid && vector_result_if.ready
    );
     frontend_tile_matrix_result_seen: cover property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            matrix_result_if.valid && matrix_result_if.ready
    );
     frontend_tile_scalar_memory_seen: cover property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            (scalar_data_rd_if.req_valid && scalar_data_rd_if.req_ready) ||
            (scalar_data_wr_if.req_valid && scalar_data_wr_if.req_ready)
    );
     frontend_tile_sync_issue_valid: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            frontend_if.sync_issue_valid |->
                (frontend_if.sync_issue_data[63:32] == 32'd0) &&
                ((frontend_if.sync_issue_data[31:0] & NPU_ISA_CLASS_MASK) ==
                 NPU_ISA_CLASS_SYNC)
    );
     frontend_tile_reset_only_clears_when_quiescent: assert property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            control_reset_clear |-> reset_quiescent
    );
     frontend_tile_reset_quiesce_seen: cover property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            control_soft_reset && reset_quiescent
    );
     frontend_tile_sync_issue_seen: cover property (
        @(posedge s_axil.aclk_i) disable iff (!s_axil.aresetn_i)
            frontend_if.sync_issue_valid && frontend_if.sync_issue_ready
    );

endmodule

/* verilator lint_on DECLFILENAME */
