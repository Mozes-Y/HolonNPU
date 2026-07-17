/* verilator lint_off DECLFILENAME */

module npu_dma_fabric_core #(
    parameter int unsigned AXI_ADDR_W = 64,
    parameter int unsigned AXI_DATA_W = 128
) (
    npu_frontend_if.dma_engine     frontend,
    npu_localmem_wr_if.master   data_wr,
    npu_localmem_rd_if.master   data_rd,
    npu_axi4_if.master             m_axi
);

    import npu_pkg::*;

    localparam int unsigned WORD_BYTES = 4;
    localparam int unsigned WORD_SHIFT = 2;
    localparam int unsigned COPY_BURST_MAX_WORDS = 16;
    localparam logic [3:0] DMA_DIRECTION_LOAD = 4'h0;
    localparam logic [3:0] DMA_DIRECTION_STORE = 4'h1;

    typedef enum logic [3:0] {
        STATE_IDLE,
        STATE_LOAD_AR,
        STATE_LOAD_R,
        STATE_LOAD_WRITE_RESP,
        STATE_LOAD_DRAIN,
        STATE_STORE_PREP,
        STATE_STORE_AW,
        STATE_STORE_READ,
        STATE_STORE_W,
        STATE_STORE_B,
        STATE_DONE,
        STATE_FAULT
    } state_e;

    localparam logic [1:0] AXI_RESP_OKAY = 2'b00;
    localparam logic [1:0] AXI_RESP_EXOKAY = 2'b01;
    localparam logic [1:0] AXI_BURST_INCR = 2'b01;

    state_e state_q;
    logic [AXI_ADDR_W-1:0] system_addr_q;
    logic [31:0] local_addr_q;
    logic [31:0] bytes_remaining_q;
    logic [4:0] burst_words_q;
    logic [4:0] burst_seen_q;
    logic [31:0] fault_code_q;
    logic [31:0] store_buffer_q [COPY_BURST_MAX_WORDS];
    logic store_read_pending_q;

    logic [63:0] cmd_system_addr;
    logic [31:0] cmd_local_addr;
    logic [31:0] cmd_byte_count;
    logic [3:0]  cmd_direction;
    logic        cmd_format_valid;
    logic        cmd_direction_valid;
    logic        cmd_local_range_valid;
    logic        cmd_valid;
    logic        cmd_is_store;
    logic [4:0]  burst_words;
    logic        r_fire;
    logic        w_fire;
    logic        b_fire;
    logic        r_resp_ok;
    logic        b_resp_ok;
    logic        burst_last_expected;
    logic [31:0] copy_word;
    logic [AXI_DATA_W-1:0] store_wdata;
    logic [(AXI_DATA_W/8)-1:0] store_wstrb;
    logic [32:0] cmd_local_end;
    logic cmd_system_range_valid;
    logic [3:0] store_buffer_index;
    logic [10:0] page_words;

    assign cmd_system_addr = frontend.dma_issue_data[63:0];
    assign cmd_local_addr = frontend.dma_issue_data[95:64];
    assign cmd_byte_count = {4'h0, frontend.dma_issue_data[123:96]};
    assign cmd_direction = frontend.dma_issue_data[127:124];
    assign cmd_local_end = {1'b0, cmd_local_addr} + {1'b0, cmd_byte_count};
    assign cmd_system_range_valid = range_fits_u64(cmd_system_addr, cmd_byte_count);
    assign cmd_format_valid = (cmd_byte_count != 32'h0000_0000) &&
                              (cmd_system_addr[WORD_SHIFT-1:0] == '0) &&
                              (cmd_local_addr[WORD_SHIFT-1:0] == '0) &&
                              (cmd_byte_count[WORD_SHIFT-1:0] == '0) &&
                              cmd_system_range_valid;
    assign cmd_direction_valid = (cmd_direction == DMA_DIRECTION_LOAD) ||
                                 (cmd_direction == DMA_DIRECTION_STORE);
    assign cmd_local_range_valid = (cmd_local_end[32] == 1'b0) &&
                                   (cmd_local_end[31:0] <= frontend.local_mem_bytes);
    assign cmd_valid = cmd_format_valid && cmd_direction_valid && cmd_local_range_valid;
    assign cmd_is_store = (cmd_direction == DMA_DIRECTION_STORE);

    assign page_words = 11'((13'd4096 - {1'b0, system_addr_q[11:0]}) >> WORD_SHIFT);
    assign burst_words = ((bytes_remaining_q >> WORD_SHIFT) < 32'(COPY_BURST_MAX_WORDS))
        ? (((bytes_remaining_q >> WORD_SHIFT) < 32'(page_words))
            ? 5'(bytes_remaining_q >> WORD_SHIFT)
            : 5'(page_words))
        : ((page_words < 11'(COPY_BURST_MAX_WORDS))
            ? 5'(page_words)
            : 5'(COPY_BURST_MAX_WORDS));
    assign r_fire = m_axi.rvalid && m_axi.rready;
    assign w_fire = m_axi.wvalid && m_axi.wready;
    assign b_fire = m_axi.bvalid && m_axi.bready;
    assign r_resp_ok = (m_axi.rresp == AXI_RESP_OKAY) ||
                       (m_axi.rresp == AXI_RESP_EXOKAY);
    assign b_resp_ok = (m_axi.bresp == AXI_RESP_OKAY) ||
                       (m_axi.bresp == AXI_RESP_EXOKAY);
    assign burst_last_expected = (burst_seen_q == (burst_words_q - 5'd1));
    assign copy_word = select_rdata_word(m_axi.rdata, system_addr_q[3:2]);
    assign store_buffer_index = 4'(burst_seen_q);
    assign store_wdata = place_wdata_word(store_buffer_q[store_buffer_index], system_addr_q[3:2]);
    assign store_wstrb = place_wstrb_word(system_addr_q[3:2]);

    assign frontend.dma_issue_ready = (state_q == STATE_IDLE) && !frontend.soft_reset;
    assign frontend.dma_event_valid = (state_q == STATE_DONE);
    assign frontend.dma_fault_valid = (state_q == STATE_FAULT);
    assign frontend.dma_fault_code = fault_code_q;

    assign m_axi.arid = '0;
    assign m_axi.araddr = system_addr_q;
    assign m_axi.arlen = 8'(burst_words - 5'd1);
    assign m_axi.arsize = 3'(WORD_SHIFT);
    assign m_axi.arburst = AXI_BURST_INCR;
    assign m_axi.arvalid = (state_q == STATE_LOAD_AR);
    assign m_axi.rready = (state_q == STATE_LOAD_DRAIN) ||
                          ((state_q == STATE_LOAD_R) && data_wr.req_ready);

    assign m_axi.awid = '0;
    assign m_axi.awaddr = system_addr_q;
    assign m_axi.awlen = 8'(burst_words - 5'd1);
    assign m_axi.awsize = 3'(WORD_SHIFT);
    assign m_axi.awburst = AXI_BURST_INCR;
    assign m_axi.awvalid = (state_q == STATE_STORE_AW);
    assign m_axi.wdata = store_wdata;
    assign m_axi.wstrb = store_wstrb;
    assign m_axi.wlast = burst_last_expected;
    assign m_axi.wvalid = (state_q == STATE_STORE_W);
    assign m_axi.bready = (state_q == STATE_STORE_B);

    assign data_wr.req_valid = (state_q == STATE_LOAD_R) &&
                               m_axi.rvalid &&
                               r_resp_ok;
    assign data_wr.req_addr = local_addr_q;
    assign data_wr.req_data = copy_word;
    assign data_wr.req_strb = 4'hF;
    assign data_rd.req_valid = (state_q == STATE_STORE_READ) && !store_read_pending_q;
    assign data_rd.req_addr = local_addr_q;

    function automatic logic [31:0] select_rdata_word(
        input logic [AXI_DATA_W-1:0] data,
        input logic [1:0] lane
    );
        unique case (lane)
            2'd0: select_rdata_word = data[31:0];
            2'd1: select_rdata_word = data[63:32];
            2'd2: select_rdata_word = data[95:64];
            default: select_rdata_word = data[127:96];
        endcase
    endfunction

    function automatic logic [AXI_DATA_W-1:0] place_wdata_word(
        input logic [31:0] data,
        input logic [1:0] lane
    );
        place_wdata_word = '0;
        unique case (lane)
            2'd0: place_wdata_word[31:0] = data;
            2'd1: place_wdata_word[63:32] = data;
            2'd2: place_wdata_word[95:64] = data;
            default: place_wdata_word[127:96] = data;
        endcase
    endfunction

    function automatic logic [(AXI_DATA_W/8)-1:0] place_wstrb_word(
        input logic [1:0] lane
    );
        place_wstrb_word = '0;
        unique case (lane)
            2'd0: place_wstrb_word[3:0] = 4'hF;
            2'd1: place_wstrb_word[7:4] = 4'hF;
            2'd2: place_wstrb_word[11:8] = 4'hF;
            default: place_wstrb_word[15:12] = 4'hF;
        endcase
    endfunction

    function automatic logic range_fits_u64(
        input logic [63:0] start,
        input logic [31:0] byte_count
    );
        range_fits_u64 = 64'(byte_count) <= ~start;
    endfunction

    always_ff @(posedge frontend.clk_i or negedge frontend.rst_ni) begin
        if (!frontend.rst_ni) begin
            state_q <= STATE_IDLE;
            system_addr_q <= '0;
            local_addr_q <= 32'h0000_0000;
            bytes_remaining_q <= 32'h0000_0000;
            burst_words_q <= '0;
            burst_seen_q <= '0;
            fault_code_q <= NPU_FAULT_NONE;
            store_read_pending_q <= 1'b0;
        end else begin
            unique case (state_q)
                STATE_IDLE,
                STATE_DONE,
                STATE_FAULT: begin
                    if (frontend.soft_reset) begin
                        state_q <= STATE_IDLE;
                    end else if (frontend.dma_issue_valid) begin
                        if (!cmd_valid) begin
                            state_q <= STATE_FAULT;
                            fault_code_q <= cmd_format_valid && cmd_direction_valid
                                ? NPU_FAULT_LOCAL_MEMORY_BOUNDS
                                : NPU_FAULT_DMA_REQUEST;
                        end else begin
                            state_q <= cmd_is_store ? STATE_STORE_PREP : STATE_LOAD_AR;
                            system_addr_q <= AXI_ADDR_W'(cmd_system_addr);
                            local_addr_q <= cmd_local_addr;
                            bytes_remaining_q <= cmd_byte_count;
                            burst_words_q <= '0;
                            burst_seen_q <= '0;
                            fault_code_q <= NPU_FAULT_NONE;
                            store_read_pending_q <= 1'b0;
                        end
                    end else if (state_q == STATE_DONE || state_q == STATE_FAULT) begin
                        state_q <= STATE_IDLE;
                    end
                end

                STATE_LOAD_AR: begin
                    if (m_axi.arready) begin
                        state_q <= STATE_LOAD_R;
                        burst_words_q <= burst_words;
                        burst_seen_q <= '0;
                    end
                end

                STATE_LOAD_R: begin
                    if (r_fire) begin
                        if (!r_resp_ok) begin
                            fault_code_q <= NPU_FAULT_AXI_READ;
                            state_q <= m_axi.rlast ? STATE_FAULT : STATE_LOAD_DRAIN;
                        end else if (m_axi.rlast != burst_last_expected) begin
                            fault_code_q <= NPU_FAULT_AXI_READ;
                            state_q <= m_axi.rlast ? STATE_FAULT : STATE_LOAD_DRAIN;
                        end else begin
                            system_addr_q <= system_addr_q + AXI_ADDR_W'(WORD_BYTES);
                            local_addr_q <= local_addr_q + 32'(WORD_BYTES);
                            bytes_remaining_q <= bytes_remaining_q - 32'(WORD_BYTES);
                            burst_seen_q <= burst_seen_q + 5'd1;
                            state_q <= STATE_LOAD_WRITE_RESP;
                        end
                    end
                end

                STATE_LOAD_WRITE_RESP: begin
                    if (data_wr.resp_valid) begin
                        if (data_wr.resp_error) begin
                            fault_code_q <= NPU_FAULT_LOCAL_MEMORY_BOUNDS;
                            state_q <= STATE_FAULT;
                        end else if (bytes_remaining_q == 32'd0) begin
                            state_q <= STATE_DONE;
                            fault_code_q <= NPU_FAULT_NONE;
                        end else if (burst_seen_q == burst_words_q) begin
                            state_q <= STATE_LOAD_AR;
                        end else begin
                            state_q <= STATE_LOAD_R;
                        end
                    end
                end

                STATE_LOAD_DRAIN: begin
                    if (r_fire && m_axi.rlast) begin
                        state_q <= STATE_FAULT;
                    end
                end

                STATE_STORE_PREP: begin
                    state_q <= STATE_STORE_READ;
                    burst_words_q <= burst_words;
                    burst_seen_q <= '0;
                    store_read_pending_q <= 1'b0;
                end

                STATE_STORE_READ: begin
                    if (!store_read_pending_q) begin
                        if (data_rd.req_ready) begin
                            store_read_pending_q <= 1'b1;
                        end
                    end else if (data_rd.resp_valid) begin
                        store_read_pending_q <= 1'b0;
                        if (data_rd.resp_error) begin
                            fault_code_q <= NPU_FAULT_LOCAL_MEMORY_BOUNDS;
                            state_q <= STATE_FAULT;
                        end else begin
                            store_buffer_q[store_buffer_index] <= data_rd.resp_data;
                            local_addr_q <= local_addr_q + 32'(WORD_BYTES);
                            if (burst_last_expected) begin
                                state_q <= STATE_STORE_AW;
                                burst_seen_q <= '0;
                            end else begin
                                state_q <= STATE_STORE_READ;
                                burst_seen_q <= burst_seen_q + 5'd1;
                            end
                        end
                    end
                end

                STATE_STORE_AW: begin
                    if (m_axi.awready) begin
                        state_q <= STATE_STORE_W;
                        burst_words_q <= burst_words;
                        burst_seen_q <= '0;
                    end
                end

                STATE_STORE_W: begin
                    if (w_fire) begin
                        system_addr_q <= system_addr_q + AXI_ADDR_W'(WORD_BYTES);
                        bytes_remaining_q <= bytes_remaining_q - 32'(WORD_BYTES);
                        burst_seen_q <= burst_seen_q + 5'd1;
                        state_q <= burst_last_expected ? STATE_STORE_B : STATE_STORE_W;
                    end
                end

                STATE_STORE_B: begin
                    if (b_fire) begin
                        if (!b_resp_ok) begin
                            fault_code_q <= NPU_FAULT_AXI_WRITE;
                            state_q <= STATE_FAULT;
                        end else if (bytes_remaining_q == 32'h0000_0000) begin
                            fault_code_q <= NPU_FAULT_NONE;
                            state_q <= STATE_DONE;
                        end else begin
                            state_q <= STATE_STORE_PREP;
                        end
                    end
                end

                default: begin
                    state_q <= STATE_FAULT;
                    fault_code_q <= NPU_FAULT_DMA_REQUEST;
                end
            endcase
        end
    end

     dma_terminal_exclusive: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            !(frontend.dma_event_valid && frontend.dma_fault_valid)
    );
     dma_read_burst_profile: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            m_axi.arvalid |->
                (m_axi.araddr[WORD_SHIFT-1:0] == '0) &&
                (m_axi.arlen <= 8'(COPY_BURST_MAX_WORDS - 1)) &&
                (m_axi.arsize == 3'(WORD_SHIFT)) &&
                (m_axi.arburst == AXI_BURST_INCR)
    );
     dma_read_burst_within_4k: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            m_axi.arvalid |->
                ({1'b0, m_axi.araddr[11:0]} +
                 ((13'(m_axi.arlen) + 13'd1) << m_axi.arsize)) <= 13'd4096
    );
     dma_write_burst_profile: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            m_axi.awvalid |->
                (m_axi.awaddr[WORD_SHIFT-1:0] == '0) &&
                (m_axi.awlen <= 8'(COPY_BURST_MAX_WORDS - 1)) &&
                (m_axi.awsize == 3'(WORD_SHIFT)) &&
                (m_axi.awburst == AXI_BURST_INCR)
    );
     dma_write_burst_within_4k: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            m_axi.awvalid |->
                ({1'b0, m_axi.awaddr[11:0]} +
                 ((13'(m_axi.awlen) + 13'd1) << m_axi.awsize)) <= 13'd4096
    );
     dma_local_write_in_bounds: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            data_wr.req_valid |-> (data_wr.req_addr < frontend.local_mem_bytes)
    );
     dma_local_read_in_bounds: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            data_rd.req_valid |-> (data_rd.req_addr < frontend.local_mem_bytes)
    );
     dma_store_uses_word_strobe: assert property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            m_axi.wvalid |-> (m_axi.wstrb != '0)
    );
     dma_success_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            frontend.dma_event_valid
    );
     dma_fault_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            frontend.dma_fault_valid
    );
     dma_read_page_split_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            m_axi.arvalid &&
            (page_words < 11'(COPY_BURST_MAX_WORDS)) &&
            (32'(page_words) < (bytes_remaining_q >> WORD_SHIFT))
    );
     dma_write_page_split_seen: cover property (
        @(posedge frontend.clk_i) disable iff (!frontend.rst_ni)
            m_axi.awvalid &&
            (page_words < 11'(COPY_BURST_MAX_WORDS)) &&
            (32'(page_words) < (bytes_remaining_q >> WORD_SHIFT))
    );

endmodule

/* verilator lint_on DECLFILENAME */
