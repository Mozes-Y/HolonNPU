/* verilator lint_off UNUSEDSIGNAL */
/* verilator lint_off UNDRIVEN */
`include "npu_assert.svh"

interface npu_axi4_if #(
    parameter int unsigned ADDR_W = 64,
    parameter int unsigned DATA_W = 128,
    parameter int unsigned ID_W = 1
) (
    input logic aclk_i,
    input logic aresetn_i
);

    localparam int unsigned STRB_W = DATA_W / 8;

    logic [ID_W-1:0]   awid;
    logic [ADDR_W-1:0] awaddr;
    logic [7:0]        awlen;
    logic [2:0]        awsize;
    logic [1:0]        awburst;
    logic              awvalid;
    logic              awready;
    logic [DATA_W-1:0] wdata;
    logic [STRB_W-1:0] wstrb;
    logic              wlast;
    logic              wvalid;
    logic              wready;
    logic [ID_W-1:0]   bid;
    logic [1:0]        bresp;
    logic              bvalid;
    logic              bready;

    logic [ID_W-1:0]   arid;
    logic [ADDR_W-1:0] araddr;
    logic [7:0]        arlen;
    logic [2:0]        arsize;
    logic [1:0]        arburst;
    logic              arvalid;
    logic              arready;
    logic [ID_W-1:0]   rid;
    logic [DATA_W-1:0] rdata;
    logic [1:0]        rresp;
    logic              rlast;
    logic              rvalid;
    logic              rready;

    modport master (
        input  aclk_i,
        input  aresetn_i,
        output awid,
        output awaddr,
        output awlen,
        output awsize,
        output awburst,
        output awvalid,
        input  awready,
        output wdata,
        output wstrb,
        output wlast,
        output wvalid,
        input  wready,
        input  bid,
        input  bresp,
        input  bvalid,
        output bready,
        output arid,
        output araddr,
        output arlen,
        output arsize,
        output arburst,
        output arvalid,
        input  arready,
        input  rid,
        input  rdata,
        input  rresp,
        input  rlast,
        input  rvalid,
        output rready
    );

    modport read_master (
        input  aclk_i,
        input  aresetn_i,
        output arid,
        output araddr,
        output arlen,
        output arsize,
        output arburst,
        output arvalid,
        input  arready,
        input  rid,
        input  rdata,
        input  rresp,
        input  rlast,
        input  rvalid,
        output rready
    );

    modport write_master (
        input  aclk_i,
        input  aresetn_i,
        output awid,
        output awaddr,
        output awlen,
        output awsize,
        output awburst,
        output awvalid,
        input  awready,
        output wdata,
        output wstrb,
        output wlast,
        output wvalid,
        input  wready,
        input  bid,
        input  bresp,
        input  bvalid,
        output bready
    );

    modport slave (
        input  aclk_i,
        input  aresetn_i,
        input  awid,
        input  awaddr,
        input  awlen,
        input  awsize,
        input  awburst,
        input  awvalid,
        output awready,
        input  wdata,
        input  wstrb,
        input  wlast,
        input  wvalid,
        output wready,
        output bid,
        output bresp,
        output bvalid,
        input  bready,
        input  arid,
        input  araddr,
        input  arlen,
        input  arsize,
        input  arburst,
        input  arvalid,
        output arready,
        output rid,
        output rdata,
        output rresp,
        output rlast,
        output rvalid,
        input  rready
    );

    modport read_slave (
        input  aclk_i,
        input  aresetn_i,
        input  arid,
        input  araddr,
        input  arlen,
        input  arsize,
        input  arburst,
        input  arvalid,
        output arready,
        output rid,
        output rdata,
        output rresp,
        output rlast,
        output rvalid,
        input  rready
    );

    modport write_slave (
        input  aclk_i,
        input  aresetn_i,
        input  awid,
        input  awaddr,
        input  awlen,
        input  awsize,
        input  awburst,
        input  awvalid,
        output awready,
        input  wdata,
        input  wstrb,
        input  wlast,
        input  wvalid,
        output wready,
        output bid,
        output bresp,
        output bvalid,
        input  bready
    );

    `HOLON_NPU_ASSERT(axi_aw_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            awvalid && !awready |=>
                awvalid && $stable(awid) && $stable(awaddr) &&
                $stable(awlen) && $stable(awsize) && $stable(awburst))
    `HOLON_NPU_ASSERT(axi_w_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            wvalid && !wready |=>
                wvalid && $stable(wdata) && $stable(wstrb) && $stable(wlast))
    `HOLON_NPU_ASSERT(axi_b_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            bvalid && !bready |=> bvalid && $stable(bid) && $stable(bresp))
    `HOLON_NPU_ASSERT(axi_ar_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            arvalid && !arready |=>
                arvalid && $stable(arid) && $stable(araddr) &&
                $stable(arlen) && $stable(arsize) && $stable(arburst))
    `HOLON_NPU_ASSERT(axi_r_stable_until_ready,
        @(posedge aclk_i) disable iff (!aresetn_i)
            rvalid && !rready |=>
                rvalid && $stable(rid) && $stable(rdata) &&
                $stable(rresp) && $stable(rlast))

    `HOLON_NPU_COVER(axi_read_burst_seen,
        @(posedge aclk_i) disable iff (!aresetn_i)
            arvalid && arready)
    `HOLON_NPU_COVER(axi_write_burst_seen,
        @(posedge aclk_i) disable iff (!aresetn_i)
            awvalid && awready)

endinterface
/* verilator lint_on UNDRIVEN */
/* verilator lint_on UNUSEDSIGNAL */
