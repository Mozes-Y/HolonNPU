/* verilator lint_off UNUSEDSIGNAL */
`include "npu_assert.svh"

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

    `HOLON_NPU_ASSERT(vr_valid_stable_until_ready,
        @(posedge clk_i) disable iff (!rst_ni)
            valid && !ready |=> valid)
    `HOLON_NPU_ASSERT(vr_data_stable_until_ready,
        @(posedge clk_i) disable iff (!rst_ni)
            valid && !ready |=> $stable(data))
    `HOLON_NPU_COVER(vr_handshake_seen,
        @(posedge clk_i) disable iff (!rst_ni)
            valid && ready)

endinterface
/* verilator lint_on UNUSEDSIGNAL */
