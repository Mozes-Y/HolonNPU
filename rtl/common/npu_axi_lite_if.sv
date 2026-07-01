/* verilator lint_off UNUSEDSIGNAL */
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

endinterface
/* verilator lint_on UNUSEDSIGNAL */
