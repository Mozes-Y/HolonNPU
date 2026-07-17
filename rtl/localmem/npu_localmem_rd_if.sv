interface npu_localmem_rd_if (
    input logic clk_i,
    input logic rst_ni
);

    logic        req_valid;
    logic        req_ready;
    logic [31:0] req_addr;
    logic        resp_valid;
    logic [31:0] resp_data;
    logic        resp_error;

    modport master (
        input  clk_i,
        input  rst_ni,
        input  req_ready,
        input  resp_valid,
        input  resp_data,
        input  resp_error,
        output req_valid,
        output req_addr
    );

    modport slave (
        input  clk_i,
        input  rst_ni,
        input  req_valid,
        input  req_addr,
        output req_ready,
        output resp_valid,
        output resp_data,
        output resp_error
    );

    localmem_read_request_stable: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            req_valid && !req_ready |=> req_valid && (req_addr == $past(req_addr))
    );

endinterface
