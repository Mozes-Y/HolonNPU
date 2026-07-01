/* verilator lint_off UNUSEDSIGNAL */
interface npu_vr_if #(
    parameter int unsigned DATA_W = 32
) (
    input logic clk_i,
    input logic rst_ni
);

    logic              valid;
    logic              ready;
    logic [DATA_W-1:0] data;

    modport source (
        input  clk_i,
        input  rst_ni,
        output valid,
        input  ready,
        output data
    );

    modport sink (
        input  clk_i,
        input  rst_ni,
        input  valid,
        output ready,
        input  data
    );

    modport monitor (
        input clk_i,
        input rst_ni,
        input valid,
        input ready,
        input data
    );

endinterface
/* verilator lint_on UNUSEDSIGNAL */
