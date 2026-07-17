/* verilator lint_off DECLFILENAME */

module npu_v2_axi_write_arbiter_core (
    input logic               clk_i,
    input logic               rst_ni,
    input logic               soft_reset_i,
    npu_axi4_if.write_slave    dma,
    npu_axi4_if.write_slave    completion,
    npu_axi4_if.write_master   m_axi
);

    typedef enum logic [1:0] {
        OWNER_NONE,
        OWNER_DMA,
        OWNER_COMPLETION
    } owner_e;

    owner_e owner_q;
    logic select_dma;
    logic select_completion;

    assign select_completion = (owner_q == OWNER_COMPLETION) ||
                               ((owner_q == OWNER_NONE) && completion.awvalid);
    assign select_dma = (owner_q == OWNER_DMA) ||
                        ((owner_q == OWNER_NONE) && !completion.awvalid && dma.awvalid);

    assign m_axi.awid = select_completion ? completion.awid : dma.awid;
    assign m_axi.awaddr = select_completion ? completion.awaddr : dma.awaddr;
    assign m_axi.awlen = select_completion ? completion.awlen : dma.awlen;
    assign m_axi.awsize = select_completion ? completion.awsize : dma.awsize;
    assign m_axi.awburst = select_completion ? completion.awburst : dma.awburst;
    assign m_axi.awvalid = select_completion ? completion.awvalid :
                           select_dma ? dma.awvalid : 1'b0;
    assign completion.awready = select_completion && m_axi.awready;
    assign dma.awready = select_dma && m_axi.awready;

    assign m_axi.wdata = select_completion ? completion.wdata : dma.wdata;
    assign m_axi.wstrb = select_completion ? completion.wstrb : dma.wstrb;
    assign m_axi.wlast = select_completion ? completion.wlast : dma.wlast;
    assign m_axi.wvalid = select_completion ? completion.wvalid :
                          select_dma ? dma.wvalid : 1'b0;
    assign completion.wready = select_completion && m_axi.wready;
    assign dma.wready = select_dma && m_axi.wready;

    assign completion.bid = m_axi.bid;
    assign completion.bresp = m_axi.bresp;
    assign completion.bvalid = (owner_q == OWNER_COMPLETION) && m_axi.bvalid;
    assign dma.bid = m_axi.bid;
    assign dma.bresp = m_axi.bresp;
    assign dma.bvalid = (owner_q == OWNER_DMA) && m_axi.bvalid;
    assign m_axi.bready = (owner_q == OWNER_COMPLETION) ? completion.bready :
                          (owner_q == OWNER_DMA) ? dma.bready : 1'b0;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) begin
            owner_q <= OWNER_NONE;
        end else if (soft_reset_i) begin
            owner_q <= OWNER_NONE;
        end else begin
            if ((owner_q == OWNER_NONE) && m_axi.awvalid && m_axi.awready) begin
                owner_q <= select_completion ? OWNER_COMPLETION : OWNER_DMA;
            end else if ((owner_q != OWNER_NONE) && m_axi.bvalid && m_axi.bready) begin
                owner_q <= OWNER_NONE;
            end
        end
    end

    v2_axi_write_owner_stable_until_response: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            owner_q != OWNER_NONE && !(m_axi.bvalid && m_axi.bready)
            |=> owner_q == $past(owner_q)
    );
    v2_axi_write_response_has_owner: assert property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            m_axi.bvalid |-> owner_q != OWNER_NONE
    );
    v2_axi_write_completion_priority_seen: cover property (
        @(posedge clk_i) disable iff (!rst_ni || soft_reset_i)
            (owner_q == OWNER_NONE) && completion.awvalid && dma.awvalid && m_axi.awready
    );

endmodule

/* verilator lint_on DECLFILENAME */
