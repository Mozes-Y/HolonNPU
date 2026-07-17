interface npu_frontend_if (
    input logic clk_i,
    input logic rst_ni
);

    /* verilator lint_off UNUSEDSIGNAL */

    logic        soft_reset;
    logic        start;
    logic        halt_request;
    logic        resume;
    logic        debug_step;
    logic [31:0] entry_pc;
    logic [31:0] program_size_bytes;
    logic [31:0] local_mem_bytes;

    logic        running;
    logic        halted;
    logic        done;
    logic        fault;
    logic [31:0] fault_code;
    logic [31:0] debug_pc;
    logic [63:0] instret;
    logic        quiescent;

    logic        program_rd_valid;
    logic [31:0] program_rd_addr;
    logic        program_rd_resp_valid;
    logic [31:0] program_rd_resp_data;
    logic        program_rd_resp_error;

    logic        dma_issue_valid;
    logic        dma_issue_ready;
    logic [127:0] dma_issue_data;
    logic        dma_event_valid;
    logic        dma_fault_valid;
    logic [31:0] dma_fault_code;
    logic        vector_issue_valid;
    logic        vector_issue_ready;
    logic [127:0] vector_issue_data;
    logic        vector_result_valid;
    logic        vector_result_ready;
    logic [63:0] vector_result_data;
    logic        matrix_issue_valid;
    logic        matrix_issue_ready;
    logic [127:0] matrix_issue_data;
    logic        matrix_result_valid;
    logic        matrix_result_ready;
    logic [63:0] matrix_result_data;
    logic        sync_issue_valid;
    logic        sync_issue_ready;
    logic [63:0] sync_issue_data;

    modport controller (
        input  running,
        input  halted,
        input  done,
        input  fault,
        input  fault_code,
        input  debug_pc,
        input  instret,
        input  quiescent,
        input  program_rd_valid,
        input  program_rd_addr,
        input  dma_issue_valid,
        input  dma_issue_data,
        input  dma_event_valid,
        input  dma_fault_valid,
        input  dma_fault_code,
        input  vector_issue_valid,
        input  vector_issue_data,
        input  vector_result_valid,
        input  vector_result_data,
        input  matrix_issue_valid,
        input  matrix_issue_data,
        input  matrix_result_valid,
        input  matrix_result_data,
        input  sync_issue_valid,
        input  sync_issue_data,
        output soft_reset,
        output start,
        output halt_request,
        output resume,
        output debug_step,
        output entry_pc,
        output program_size_bytes,
        output local_mem_bytes,
        output program_rd_resp_valid,
        output program_rd_resp_data,
        output program_rd_resp_error,
        output dma_issue_ready,
        output vector_issue_ready,
        output vector_result_ready,
        output matrix_issue_ready,
        output matrix_result_ready,
        output sync_issue_ready
    );

    modport dma_engine (
        input  clk_i,
        input  rst_ni,
        input  soft_reset,
        input  local_mem_bytes,
        input  dma_issue_valid,
        input  dma_issue_data,
        output dma_issue_ready,
        output dma_event_valid,
        output dma_fault_valid,
        output dma_fault_code
    );

    modport frontend (
        input  clk_i,
        input  rst_ni,
        input  soft_reset,
        input  start,
        input  halt_request,
        input  resume,
        input  debug_step,
        input  entry_pc,
        input  program_size_bytes,
        input  local_mem_bytes,
        input  program_rd_resp_valid,
        input  program_rd_resp_data,
        input  program_rd_resp_error,
        input  dma_issue_ready,
        input  dma_event_valid,
        input  dma_fault_valid,
        input  dma_fault_code,
        input  vector_issue_ready,
        input  vector_result_valid,
        input  vector_result_data,
        input  matrix_issue_ready,
        input  matrix_result_valid,
        input  matrix_result_data,
        input  sync_issue_ready,
        output running,
        output halted,
        output done,
        output fault,
        output fault_code,
        output debug_pc,
        output instret,
        output quiescent,
        output program_rd_valid,
        output program_rd_addr,
        output dma_issue_valid,
        output dma_issue_data,
        output vector_issue_valid,
        output vector_issue_data,
        output vector_result_ready,
        output matrix_issue_valid,
        output matrix_issue_data,
        output matrix_result_ready,
        output sync_issue_valid,
        output sync_issue_data
    );

    frontend_start_while_idle: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            start |-> !(running || halted)
    );
    frontend_program_read_aligned: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            program_rd_valid |-> (program_rd_addr[1:0] == 2'b00)
    );
    frontend_dma_terminal_exclusive: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            !(dma_event_valid && dma_fault_valid)
    );
    frontend_vector_fault_has_code: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            vector_result_valid && vector_result_data[0] |->
                (vector_result_data[63:32] != 32'h0000_0000)
    );
    frontend_matrix_fault_has_code: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            matrix_result_valid && matrix_result_data[0] |->
                (matrix_result_data[63:32] != 32'h0000_0000)
    );

    /* verilator lint_on UNUSEDSIGNAL */

endinterface
