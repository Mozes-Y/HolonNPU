/* verilator lint_off UNUSEDSIGNAL */
`include "npu_assert.svh"

interface npu_axi_lite_if #(
    parameter int unsigned ADDR_W = 12,
    parameter int unsigned DATA_W = 32
) (
    input logic aclk_i,
    input logic aresetn_i
);

    localparam int unsigned STRB_W = DATA_W / 8;

    logic [ADDR_W-1:0] awaddr;
    logic              awvalid;
    logic              awready;
    logic [DATA_W-1:0] wdata;
    logic [STRB_W-1:0] wstrb;
    logic              wvalid;
    logic              wready;
    logic [1:0]        bresp;
    logic              bvalid;
    logic              bready;
    logic [ADDR_W-1:0] araddr;
    logic              arvalid;
    logic              arready;
    logic [DATA_W-1:0] rdata;
    logic [1:0]        rresp;
    logic              rvalid;
    logic              rready;

    modport master (
        input  aclk_i,
        input  aresetn_i,
        output awaddr,
        output awvalid,
        input  awready,
        output wdata,
        output wstrb,
        output wvalid,
        input  wready,
        input  bresp,
        input  bvalid,
        output bready,
        output araddr,
        output arvalid,
        input  arready,
        input  rdata,
        input  rresp,
        input  rvalid,
        output rready
    );

    modport slave (
        input  aclk_i,
        input  aresetn_i,
        input  awaddr,
        input  awvalid,
        output awready,
        input  wdata,
        input  wstrb,
        input  wvalid,
        output wready,
        output bresp,
        output bvalid,
        input  bready,
        input  araddr,
        input  arvalid,
        output arready,
        output rdata,
        output rresp,
        output rvalid,
        input  rready
    );

    `HOLON_NPU_ASSERT(axil_aw_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            awvalid && !awready |=> awvalid && $stable(awaddr))
    `HOLON_NPU_ASSERT(axil_w_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            wvalid && !wready |=> wvalid && $stable(wdata) && $stable(wstrb))
    `HOLON_NPU_ASSERT(axil_b_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            bvalid && !bready |=> bvalid && $stable(bresp))
    `HOLON_NPU_ASSERT(axil_ar_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            arvalid && !arready |=> arvalid && $stable(araddr))
    `HOLON_NPU_ASSERT(axil_r_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            rvalid && !rready |=> rvalid && $stable(rdata) && $stable(rresp))

    `HOLON_NPU_COVER(axil_write_response_seen,
        @(posedge aclk_i) disable iff (!aresetn_i)
            bvalid && bready)
    `HOLON_NPU_COVER(axil_read_response_seen,
        @(posedge aclk_i) disable iff (!aresetn_i)
            rvalid && rready)

endinterface
/* verilator lint_on UNUSEDSIGNAL */
