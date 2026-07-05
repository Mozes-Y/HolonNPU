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

    vr_valid_stable_until_ready: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            valid && !ready |=> valid
    );
    vr_data_stable_until_ready: assert property (
        @(posedge clk_i) disable iff (!rst_ni)
            valid && !ready |=> $stable(data)
    );
    vr_handshake_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni)
            valid && ready
    );

endinterface
/* verilator lint_on UNUSEDSIGNAL */
