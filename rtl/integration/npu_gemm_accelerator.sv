module npu_gemm_accelerator #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128,
    localparam int unsigned STRB_W = DATA_W / 8
) (
    input  logic                    clk_i,
    input  logic                    rst_ni,
    input  logic                    soft_reset_i,
    input  logic                    clear_perf_i,

    input  logic                    command_valid_i,
    output logic                    command_ready_o,
    input  logic [31:0]             command_m_i,
    input  logic [31:0]             command_n_i,
    input  logic [31:0]             command_k_i,
    input  logic [ADDR_W-1:0]       command_a_addr_i,
    input  logic [ADDR_W-1:0]       command_b_addr_i,
    input  logic [ADDR_W-1:0]       command_c_addr_i,
    input  logic [31:0]             command_a_stride_i,
    input  logic [31:0]             command_b_stride_i,
    input  logic [31:0]             command_c_stride_i,
    input  logic                    command_irq_on_done_i,
    input  logic                    command_irq_on_error_i,
    input  logic                    command_clear_perf_on_start_i,

    output logic                    busy_o,
    output logic                    done_o,
    output logic                    error_o,
    output logic [31:0]             error_code_o,
    output logic                    irq_o,
    output logic [3:0]              stage_o,
    output logic [63:0]             perf_cycles_o,
    output logic [63:0]             perf_busy_cycles_o,
    output logic [31:0]             perf_desc_count_o,
    output logic [31:0]             perf_error_count_o,

    output logic [ADDR_W-1:0]       m_axi_araddr_o,
    output logic [7:0]              m_axi_arlen_o,
    output logic [2:0]              m_axi_arsize_o,
    output logic [1:0]              m_axi_arburst_o,
    output logic                    m_axi_arvalid_o,
    input  logic                    m_axi_arready_i,
    input  logic [63:0]             m_axi_rdata_lo_i,
    input  logic [63:0]             m_axi_rdata_hi_i,
    input  logic [1:0]              m_axi_rresp_i,
    input  logic                    m_axi_rlast_i,
    input  logic                    m_axi_rvalid_i,
    output logic                    m_axi_rready_o,

    output logic [ADDR_W-1:0]       m_axi_awaddr_o,
    output logic [7:0]              m_axi_awlen_o,
    output logic [2:0]              m_axi_awsize_o,
    output logic [1:0]              m_axi_awburst_o,
    output logic                    m_axi_awvalid_o,
    input  logic                    m_axi_awready_i,
    output logic [63:0]             m_axi_wdata_lo_o,
    output logic [63:0]             m_axi_wdata_hi_o,
    output logic [STRB_W-1:0]       m_axi_wstrb_o,
    output logic                    m_axi_wlast_o,
    output logic                    m_axi_wvalid_o,
    input  logic                    m_axi_wready_i,
    input  logic [1:0]              m_axi_bresp_i,
    input  logic                    m_axi_bvalid_i,
    output logic                    m_axi_bready_o
);

    import npu_pkg::*;

    localparam int unsigned TILE_M = 16;
    localparam int unsigned TILE_N = 16;
    localparam int unsigned TILE_K = 16;
    localparam int unsigned INPUT_W = 8;
    localparam int unsigned ACC_W = 32;
    localparam int unsigned SYSTOLIC_CYCLES = TILE_K + TILE_M + TILE_N - 1;
    localparam logic [31:0] DMA_BEAT_BYTES = 32'd16;

    localparam logic [3:0] STAGE_IDLE    = 4'd0;
    localparam logic [3:0] STAGE_LOAD_A  = 4'd1;
    localparam logic [3:0] STAGE_LOAD_B  = 4'd2;
    localparam logic [3:0] STAGE_COMPUTE = 4'd3;
    localparam logic [3:0] STAGE_STORE   = 4'd4;
    localparam logic [3:0] STAGE_DONE    = 4'd5;
    localparam logic [3:0] STAGE_ERROR   = 4'd6;

    typedef enum logic [3:0] {
        STATE_IDLE        = 4'd0,
        STATE_TILE_CLEAR  = 4'd1,
        STATE_LOAD_A_PREP = 4'd2,
        STATE_LOAD_A_WAIT = 4'd3,
        STATE_LOAD_B_PREP = 4'd4,
        STATE_LOAD_B_WAIT = 4'd5,
        STATE_COMPUTE     = 4'd6,
        STATE_K_ADVANCE   = 4'd7,
        STATE_STORE_PREP  = 4'd8,
        STATE_STORE_WAIT  = 4'd9,
        STATE_NEXT_TILE   = 4'd10,
        STATE_DONE        = 4'd11,
        STATE_ERR         = 4'd12
    } state_e;

    state_e state_q;

    logic [31:0] m_q;
    logic [31:0] n_q;
    logic [31:0] k_q;
    logic [ADDR_W-1:0] a_addr_q;
    logic [ADDR_W-1:0] b_addr_q;
    logic [ADDR_W-1:0] c_addr_q;
    logic [31:0] a_stride_q;
    logic [31:0] b_stride_q;
    logic [31:0] c_stride_q;
    logic irq_on_done_q;
    logic irq_on_error_q;

    logic [31:0] m_base_q;
    logic [31:0] n_base_q;
    logic [31:0] k_base_q;
    logic [4:0]  load_row_q;
    logic [4:0]  load_k_q;
    logic [5:0]  compute_k_q;
    logic [4:0]  store_row_q;
    logic [2:0]  store_chunk_q;

    logic [31:0] error_code_q;
    logic [63:0] perf_cycles_q;
    logic [63:0] perf_busy_cycles_q;
    logic [31:0] perf_desc_count_q;
    logic [31:0] perf_error_count_q;

    logic signed [INPUT_W-1:0] a_vec [TILE_M];
    logic signed [INPUT_W-1:0] b_vec [TILE_N];
    logic [TILE_M-1:0] row_mask;
    logic [TILE_N-1:0] col_mask;
    logic [TILE_M-1:0] a_valid_vec;
    logic [TILE_N-1:0] b_valid_vec;
    logic [TILE_N-1:0] c_valid [TILE_M];
    logic signed [ACC_W-1:0] c_data [TILE_M][TILE_N];

    logic array_clear;
    logic array_step_valid;

    logic read_start_q;
    logic dma_rst_ni;
    logic [ADDR_W-1:0] read_addr_q;
    logic read_dma_busy;
    logic read_dma_done;
    logic read_dma_error;
    logic [31:0] read_dma_error_code;
    logic read_out_valid;
    logic read_out_ready;
    logic [DATA_W-1:0] read_out_data;
    logic read_out_last;
    logic [DATA_W-1:0] read_dma_rdata;
    logic scratch_a_wr_valid;
    logic scratch_b_wr_valid;

    logic write_start_q;
    logic [ADDR_W-1:0] write_addr_q;
    logic write_dma_busy;
    logic write_dma_done;
    logic write_dma_error;
    logic [31:0] write_dma_error_code;
    logic write_in_valid;
    logic write_in_ready;
    logic [DATA_W-1:0] write_in_data;
    logic [DATA_W-1:0] write_dma_wdata;

    logic command_fire;
    logic busy_state;
    logic command_dimension_zero;
    logic command_dimension_unsupported;
    logic command_alignment_error;
    logic [31:0] command_c_stride_min;
    logic [2:0] store_chunk_count;

    assign command_fire = command_valid_i && command_ready_o;
    assign busy_state = (state_q != STATE_IDLE) && (state_q != STATE_DONE) && (state_q != STATE_ERR);

    assign command_ready_o = (state_q == STATE_IDLE) || (state_q == STATE_DONE) || (state_q == STATE_ERR);
    assign busy_o = busy_state || read_dma_busy || write_dma_busy ||
                    (((state_q == STATE_LOAD_A_WAIT) || (state_q == STATE_LOAD_B_WAIT)) && read_dma_done) ||
                    ((state_q == STATE_STORE_WAIT) && write_in_ready);
    assign done_o = (state_q == STATE_DONE) && !command_fire;
    assign error_o = (state_q == STATE_ERR) && !command_fire;
    assign error_code_o = error_code_q;
    assign irq_o = (done_o && irq_on_done_q) || (error_o && irq_on_error_q);
    assign perf_cycles_o = perf_cycles_q;
    assign perf_busy_cycles_o = perf_busy_cycles_q;
    assign perf_desc_count_o = perf_desc_count_q;
    assign perf_error_count_o = perf_error_count_q;

    assign read_dma_rdata = {m_axi_rdata_hi_i, m_axi_rdata_lo_i};
    assign read_out_ready = (state_q == STATE_LOAD_A_WAIT) || (state_q == STATE_LOAD_B_WAIT);
    assign dma_rst_ni = rst_ni && !soft_reset_i;
    assign scratch_a_wr_valid = (state_q == STATE_LOAD_A_WAIT) && read_out_valid && read_out_last;
    assign scratch_b_wr_valid = (state_q == STATE_LOAD_B_WAIT) && read_out_valid && read_out_last;

    assign write_in_valid = (state_q == STATE_STORE_WAIT);
    assign write_in_data = pack_store_beat(store_row_q, store_chunk_q);
    assign m_axi_wdata_lo_o = write_dma_wdata[63:0];
    assign m_axi_wdata_hi_o = write_dma_wdata[127:64];

    assign array_clear = (state_q == STATE_TILE_CLEAR);
    assign array_step_valid = (state_q == STATE_COMPUTE);

    assign command_c_stride_min = command_n_i << 2;
    assign command_dimension_zero = (command_m_i == 32'd0) ||
                                    (command_n_i == 32'd0) ||
                                    (command_k_i == 32'd0);
    assign command_dimension_unsupported = (command_m_i > 32'd65535) ||
                                           (command_n_i > 32'd65535) ||
                                           (command_k_i > 32'd65535);
    assign command_alignment_error = (command_a_addr_i[3:0] != 4'h0) ||
                                     (command_b_addr_i[3:0] != 4'h0) ||
                                     (command_c_addr_i[3:0] != 4'h0) ||
                                     (command_a_stride_i[3:0] != 4'h0) ||
                                     (command_b_stride_i[3:0] != 4'h0) ||
                                     (command_c_stride_i[3:0] != 4'h0) ||
                                     (command_a_stride_i < command_k_i) ||
                                     (command_b_stride_i < command_n_i) ||
                                     (command_c_stride_i < command_c_stride_min);

    assign store_chunk_count = active_store_chunks();

    initial begin
        if (NPU_ABI_MAJOR != 1) $fatal("Unexpected NPU_ABI_MAJOR");
        if (NPU_ABI_MINOR != 0) $fatal("Unexpected NPU_ABI_MINOR");
        if (NPU_DESC_SIZE_BYTES != 128) $fatal("Unexpected NPU_DESC_SIZE_BYTES");
        if (NPU_DESC_ALIGN_BYTES != 16) $fatal("Unexpected NPU_DESC_ALIGN_BYTES");
        if (NPU_TENSOR_ALIGN_BYTES != 16) $fatal("Unexpected NPU_TENSOR_ALIGN_BYTES");
        if (NPU_ARRAY_M != 16) $fatal("Unexpected NPU_ARRAY_M");
        if (NPU_ARRAY_N != 16) $fatal("Unexpected NPU_ARRAY_N");
        if (NPU_INPUT_BITS != 8) $fatal("Unexpected NPU_INPUT_BITS");
        if (NPU_ACC_BITS != 32) $fatal("Unexpected NPU_ACC_BITS");
    end

    function automatic logic active_m_index(input logic [4:0] index);
        active_m_index = ((m_base_q + 32'(index)) < m_q);
    endfunction

    function automatic logic active_n_index(input logic [4:0] index);
        active_n_index = ((n_base_q + 32'(index)) < n_q);
    endfunction

    function automatic logic active_k_index(input logic [4:0] index);
        active_k_index = ((k_base_q + 32'(index)) < k_q);
    endfunction

    function automatic logic [ADDR_W-1:0] a_row_addr(input logic [4:0] row_index);
        a_row_addr = a_addr_q +
            ADDR_W'((64'(m_base_q) + 64'(row_index)) * 64'(a_stride_q)) +
            ADDR_W'(k_base_q);
    endfunction

    function automatic logic [ADDR_W-1:0] b_row_addr(input logic [4:0] k_index);
        b_row_addr = b_addr_q +
            ADDR_W'((64'(k_base_q) + 64'(k_index)) * 64'(b_stride_q)) +
            ADDR_W'(n_base_q);
    endfunction

    function automatic logic [ADDR_W-1:0] c_chunk_addr(
        input logic [4:0] row_index,
        input logic [2:0] chunk_index
    );
        c_chunk_addr = c_addr_q +
            ADDR_W'((64'(m_base_q) + 64'(row_index)) * 64'(c_stride_q)) +
            ADDR_W'((64'(n_base_q) + (64'(chunk_index) * 64'd4)) * 64'd4);
    endfunction

    function automatic logic [2:0] active_store_chunks();
        logic [31:0] remaining_cols;
        logic [31:0] active_cols;
        begin
            if (n_base_q >= n_q) begin
                active_store_chunks = 3'd0;
            end else begin
                remaining_cols = n_q - n_base_q;
                active_cols = (remaining_cols > 32'd16) ? 32'd16 : remaining_cols;
                active_store_chunks = 3'((active_cols + 32'd3) >> 2);
            end
        end
    endfunction

    function automatic logic [DATA_W-1:0] pack_store_beat(
        input logic [4:0] row_index,
        input logic [2:0] chunk_index
    );
        int unsigned col_index;
        begin
            pack_store_beat = '0;
            for (int unsigned lane = 0; lane < 4; lane++) begin
                col_index = (int'(chunk_index) * 4) + lane;
                if ((col_index < TILE_N) && active_m_index(row_index) &&
                    c_valid[int'(row_index[3:0])][col_index] &&
                    ((n_base_q + 32'(col_index)) < n_q)) begin
                    pack_store_beat[(lane * 32) +: 32] = c_data[int'(row_index[3:0])][col_index];
                end
            end
        end
    endfunction

    npu_gemm_tile_scratchpad #(
        .TILE_M(TILE_M),
        .TILE_N(TILE_N),
        .TILE_K(TILE_K),
        .INPUT_W(INPUT_W),
        .DATA_W(DATA_W)
    ) u_tile_scratchpad (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .clear_i(array_clear || soft_reset_i),
        .a_wr_valid_i(scratch_a_wr_valid),
        .a_wr_row_i(load_row_q[3:0]),
        .a_wr_data_i(read_out_data),
        .b_wr_valid_i(scratch_b_wr_valid),
        .b_wr_k_i(load_k_q[3:0]),
        .b_wr_data_i(read_out_data),
        .m_i(m_q),
        .n_i(n_q),
        .k_i(k_q),
        .m_base_i(m_base_q),
        .n_base_i(n_base_q),
        .k_base_i(k_base_q),
        .compute_cycle_i(compute_k_q),
        .row_mask_o(row_mask),
        .col_mask_o(col_mask),
        .a_valid_o(a_valid_vec),
        .b_valid_o(b_valid_vec),
        .a_o(a_vec),
        .b_o(b_vec)
    );

    always_comb begin
        unique case (state_q)
            STATE_IDLE: stage_o = STAGE_IDLE;
            STATE_LOAD_A_PREP,
            STATE_LOAD_A_WAIT: stage_o = STAGE_LOAD_A;
            STATE_LOAD_B_PREP,
            STATE_LOAD_B_WAIT: stage_o = STAGE_LOAD_B;
            STATE_TILE_CLEAR,
            STATE_COMPUTE,
            STATE_K_ADVANCE: stage_o = STAGE_COMPUTE;
            STATE_STORE_PREP,
            STATE_STORE_WAIT,
            STATE_NEXT_TILE: stage_o = STAGE_STORE;
            STATE_DONE: stage_o = STAGE_DONE;
            STATE_ERR: stage_o = STAGE_ERROR;
            default: stage_o = STAGE_ERROR;
        endcase
    end

    npu_axi4_read_dma #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) u_read_dma (
        .clk_i(clk_i),
        .rst_ni(dma_rst_ni),
        .start_i(read_start_q),
        .addr_i(read_addr_q),
        .bytes_i(DMA_BEAT_BYTES),
        .busy_o(read_dma_busy),
        .done_o(read_dma_done),
        .error_o(read_dma_error),
        .error_code_o(read_dma_error_code),
        .m_axi_araddr_o(m_axi_araddr_o),
        .m_axi_arlen_o(m_axi_arlen_o),
        .m_axi_arsize_o(m_axi_arsize_o),
        .m_axi_arburst_o(m_axi_arburst_o),
        .m_axi_arvalid_o(m_axi_arvalid_o),
        .m_axi_arready_i(m_axi_arready_i),
        .m_axi_rdata_i(read_dma_rdata),
        .m_axi_rresp_i(m_axi_rresp_i),
        .m_axi_rlast_i(m_axi_rlast_i),
        .m_axi_rvalid_i(m_axi_rvalid_i),
        .m_axi_rready_o(m_axi_rready_o),
        .out_valid_o(read_out_valid),
        .out_ready_i(read_out_ready),
        .out_data_o(read_out_data),
        .out_last_o(read_out_last)
    );

    npu_axi4_write_dma #(
        .ADDR_W(ADDR_W),
        .DATA_W(DATA_W)
    ) u_write_dma (
        .clk_i(clk_i),
        .rst_ni(dma_rst_ni),
        .start_i(write_start_q),
        .addr_i(write_addr_q),
        .bytes_i(DMA_BEAT_BYTES),
        .busy_o(write_dma_busy),
        .done_o(write_dma_done),
        .error_o(write_dma_error),
        .error_code_o(write_dma_error_code),
        .m_axi_awaddr_o(m_axi_awaddr_o),
        .m_axi_awlen_o(m_axi_awlen_o),
        .m_axi_awsize_o(m_axi_awsize_o),
        .m_axi_awburst_o(m_axi_awburst_o),
        .m_axi_awvalid_o(m_axi_awvalid_o),
        .m_axi_awready_i(m_axi_awready_i),
        .m_axi_wdata_o(write_dma_wdata),
        .m_axi_wstrb_o(m_axi_wstrb_o),
        .m_axi_wlast_o(m_axi_wlast_o),
        .m_axi_wvalid_o(m_axi_wvalid_o),
        .m_axi_wready_i(m_axi_wready_i),
        .m_axi_bresp_i(m_axi_bresp_i),
        .m_axi_bvalid_i(m_axi_bvalid_i),
        .m_axi_bready_o(m_axi_bready_o),
        .in_valid_i(write_in_valid),
        .in_ready_o(write_in_ready),
        .in_data_i(write_in_data)
    );

    npu_systolic_array #(
        .ARRAY_M(TILE_M),
        .ARRAY_N(TILE_N),
        .INPUT_W(INPUT_W),
        .ACC_W(ACC_W)
    ) u_array (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .clear_i(array_clear),
        .step_valid_i(array_step_valid),
        .row_mask_i(row_mask),
        .col_mask_i(col_mask),
        .a_valid_i(a_valid_vec),
        .b_valid_i(b_valid_vec),
        .a_i(a_vec),
        .b_i(b_vec),
        .c_valid_o(c_valid),
        .c_o(c_data)
    );

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni || soft_reset_i) begin
            state_q <= STATE_IDLE;
            m_q <= 32'd0;
            n_q <= 32'd0;
            k_q <= 32'd0;
            a_addr_q <= '0;
            b_addr_q <= '0;
            c_addr_q <= '0;
            a_stride_q <= 32'd0;
            b_stride_q <= 32'd0;
            c_stride_q <= 32'd0;
            irq_on_done_q <= 1'b0;
            irq_on_error_q <= 1'b0;
            m_base_q <= 32'd0;
            n_base_q <= 32'd0;
            k_base_q <= 32'd0;
            load_row_q <= 5'd0;
            load_k_q <= 5'd0;
            compute_k_q <= 6'd0;
            store_row_q <= 5'd0;
            store_chunk_q <= 3'd0;
            error_code_q <= 32'(NPU_ERR_NONE);
            perf_cycles_q <= 64'd0;
            perf_busy_cycles_q <= 64'd0;
            perf_desc_count_q <= 32'd0;
            perf_error_count_q <= 32'd0;
            read_start_q <= 1'b0;
            read_addr_q <= '0;
            write_start_q <= 1'b0;
            write_addr_q <= '0;
        end else begin
            read_start_q <= 1'b0;
            write_start_q <= 1'b0;

            if (clear_perf_i) begin
                perf_cycles_q <= 64'd0;
                perf_busy_cycles_q <= 64'd0;
                perf_desc_count_q <= 32'd0;
                perf_error_count_q <= 32'd0;
            end else if (busy_state) begin
                perf_cycles_q <= perf_cycles_q + 64'd1;
                perf_busy_cycles_q <= perf_busy_cycles_q + 64'd1;
            end

            unique case (state_q)
                STATE_IDLE,
                STATE_DONE,
                STATE_ERR: begin
                    if (command_fire) begin
                        if (command_clear_perf_on_start_i) begin
                            perf_cycles_q <= 64'd0;
                            perf_busy_cycles_q <= 64'd0;
                            perf_desc_count_q <= 32'd0;
                            perf_error_count_q <= 32'd0;
                        end

                        irq_on_done_q <= command_irq_on_done_i;
                        irq_on_error_q <= command_irq_on_error_i;
                        error_code_q <= 32'(NPU_ERR_NONE);

                        if (command_dimension_zero) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_DIMENSION_ZERO);
                            perf_error_count_q <= command_clear_perf_on_start_i ? 32'd1 : perf_error_count_q + 32'd1;
                        end else if (command_dimension_unsupported) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_DIMENSION_UNSUPPORTED);
                            perf_error_count_q <= command_clear_perf_on_start_i ? 32'd1 : perf_error_count_q + 32'd1;
                        end else if (command_alignment_error) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_UNSUPPORTED_ALIGNMENT);
                            perf_error_count_q <= command_clear_perf_on_start_i ? 32'd1 : perf_error_count_q + 32'd1;
                        end else begin
                            m_q <= command_m_i;
                            n_q <= command_n_i;
                            k_q <= command_k_i;
                            a_addr_q <= command_a_addr_i;
                            b_addr_q <= command_b_addr_i;
                            c_addr_q <= command_c_addr_i;
                            a_stride_q <= command_a_stride_i;
                            b_stride_q <= command_b_stride_i;
                            c_stride_q <= command_c_stride_i;
                            m_base_q <= 32'd0;
                            n_base_q <= 32'd0;
                            k_base_q <= 32'd0;
                            load_row_q <= 5'd0;
                            load_k_q <= 5'd0;
                            compute_k_q <= 6'd0;
                            store_row_q <= 5'd0;
                            store_chunk_q <= 3'd0;
                            state_q <= STATE_TILE_CLEAR;
                        end
                    end
                end

                STATE_TILE_CLEAR: begin
                    k_base_q <= 32'd0;
                    load_row_q <= 5'd0;
                    load_k_q <= 5'd0;
                    compute_k_q <= 6'd0;
                    store_row_q <= 5'd0;
                    store_chunk_q <= 3'd0;
                    state_q <= STATE_LOAD_A_PREP;
                end

                STATE_LOAD_A_PREP: begin
                    if (load_row_q == 5'd16) begin
                        load_k_q <= 5'd0;
                        state_q <= STATE_LOAD_B_PREP;
                    end else if (!active_m_index(load_row_q)) begin
                        load_row_q <= load_row_q + 5'd1;
                    end else begin
                        read_addr_q <= a_row_addr(load_row_q);
                        read_start_q <= 1'b1;
                        state_q <= STATE_LOAD_A_WAIT;
                    end
                end

                STATE_LOAD_A_WAIT: begin
                    if (read_dma_error) begin
                        state_q <= STATE_ERR;
                        error_code_q <= read_dma_error_code;
                        perf_error_count_q <= perf_error_count_q + 32'd1;
                    end else if (read_out_valid) begin
                        if (!read_out_last) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_INTERNAL_PROTOCOL);
                            perf_error_count_q <= perf_error_count_q + 32'd1;
                        end else begin
                            load_row_q <= load_row_q + 5'd1;
                            state_q <= STATE_LOAD_A_PREP;
                        end
                    end
                end

                STATE_LOAD_B_PREP: begin
                    if (load_k_q == 5'd16) begin
                        compute_k_q <= 6'd0;
                        state_q <= STATE_COMPUTE;
                    end else if (!active_k_index(load_k_q)) begin
                        load_k_q <= load_k_q + 5'd1;
                    end else begin
                        read_addr_q <= b_row_addr(load_k_q);
                        read_start_q <= 1'b1;
                        state_q <= STATE_LOAD_B_WAIT;
                    end
                end

                STATE_LOAD_B_WAIT: begin
                    if (read_dma_error) begin
                        state_q <= STATE_ERR;
                        error_code_q <= read_dma_error_code;
                        perf_error_count_q <= perf_error_count_q + 32'd1;
                    end else if (read_out_valid) begin
                        if (!read_out_last) begin
                            state_q <= STATE_ERR;
                            error_code_q <= 32'(NPU_ERR_INTERNAL_PROTOCOL);
                            perf_error_count_q <= perf_error_count_q + 32'd1;
                        end else begin
                            load_k_q <= load_k_q + 5'd1;
                            state_q <= STATE_LOAD_B_PREP;
                        end
                    end
                end

                STATE_COMPUTE: begin
                    if (compute_k_q == 6'(SYSTOLIC_CYCLES - 1)) begin
                        state_q <= STATE_K_ADVANCE;
                    end else begin
                        compute_k_q <= compute_k_q + 6'd1;
                    end
                end

                STATE_K_ADVANCE: begin
                    if ((k_base_q + 32'd16) < k_q) begin
                        k_base_q <= k_base_q + 32'd16;
                        load_row_q <= 5'd0;
                        load_k_q <= 5'd0;
                        compute_k_q <= 6'd0;
                        state_q <= STATE_LOAD_A_PREP;
                    end else begin
                        store_row_q <= 5'd0;
                        store_chunk_q <= 3'd0;
                        state_q <= STATE_STORE_PREP;
                    end
                end

                STATE_STORE_PREP: begin
                    if (store_row_q == 5'd16) begin
                        state_q <= STATE_NEXT_TILE;
                    end else if (!active_m_index(store_row_q)) begin
                        store_row_q <= store_row_q + 5'd1;
                        store_chunk_q <= 3'd0;
                    end else if (store_chunk_q >= store_chunk_count) begin
                        store_row_q <= store_row_q + 5'd1;
                        store_chunk_q <= 3'd0;
                    end else begin
                        write_addr_q <= c_chunk_addr(store_row_q, store_chunk_q);
                        write_start_q <= 1'b1;
                        state_q <= STATE_STORE_WAIT;
                    end
                end

                STATE_STORE_WAIT: begin
                    if (write_dma_error) begin
                        state_q <= STATE_ERR;
                        error_code_q <= write_dma_error_code;
                        perf_error_count_q <= perf_error_count_q + 32'd1;
                    end else if (write_dma_done && !write_start_q) begin
                        if ((store_chunk_q + 3'd1) >= store_chunk_count) begin
                            store_row_q <= store_row_q + 5'd1;
                            store_chunk_q <= 3'd0;
                        end else begin
                            store_chunk_q <= store_chunk_q + 3'd1;
                        end
                        state_q <= STATE_STORE_PREP;
                    end
                end

                STATE_NEXT_TILE: begin
                    if ((n_base_q + 32'd16) < n_q) begin
                        n_base_q <= n_base_q + 32'd16;
                        k_base_q <= 32'd0;
                        state_q <= STATE_TILE_CLEAR;
                    end else if ((m_base_q + 32'd16) < m_q) begin
                        m_base_q <= m_base_q + 32'd16;
                        n_base_q <= 32'd0;
                        k_base_q <= 32'd0;
                        state_q <= STATE_TILE_CLEAR;
                    end else begin
                        state_q <= STATE_DONE;
                        error_code_q <= 32'(NPU_ERR_NONE);
                        perf_desc_count_q <= perf_desc_count_q + 32'd1;
                    end
                end

                default: begin
                    state_q <= STATE_ERR;
                    error_code_q <= 32'(NPU_ERR_INTERNAL_PROTOCOL);
                    perf_error_count_q <= perf_error_count_q + 32'd1;
                end
            endcase
        end
    end

endmodule
