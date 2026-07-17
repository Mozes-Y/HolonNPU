interface npu_localmem_wr_if (
    input logic clk_i,
    input logic rst_ni
);

    logic        req_valid;
    logic        req_ready;
    logic [31:0] req_addr;
    logic [31:0] req_data;
    logic [3:0]  req_strb;
    logic        resp_valid;
    logic        resp_error;

    modport master (
        input  clk_i,
        input  rst_ni,
        input  req_ready,
        input  resp_valid,
        input  resp_error,
        output req_valid,
        output req_addr,
        output req_data,
        output req_strb
    );

    modport slave (
        input  clk_i,
        input  rst_ni,
        input  req_valid,
        input  req_addr,
        input  req_data,
        input  req_strb,
        output req_ready,
        output resp_valid,
        output resp_error
    );

    localmem_write_request_stable: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            req_valid && !req_ready |=>
                req_valid &&
                (req_addr == $past(req_addr)) &&
                (req_data == $past(req_data)) &&
                (req_strb == $past(req_strb))
    );

endinterface
