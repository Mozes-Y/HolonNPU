module npu_common_smoke_top (
    input  logic       clk_i,
    input  logic       rst_ni,

    input  logic       fifo_in_valid_i,
    output logic       fifo_in_ready_o,
    input  logic [7:0] fifo_in_data_i,
    output logic       fifo_out_valid_o,
    input  logic       fifo_out_ready_i,
    output logic [7:0] fifo_out_data_o,
    output logic [1:0] fifo_count_o,

    input  logic       skid_in_valid_i,
    output logic       skid_in_ready_o,
    input  logic [7:0] skid_in_data_i,
    output logic       skid_out_valid_o,
    input  logic       skid_out_ready_i,
    output logic [7:0] skid_out_data_o,

    input  logic       slice_in_valid_i,
    output logic       slice_in_ready_o,
    input  logic [7:0] slice_in_data_i,
    output logic       slice_out_valid_o,
    input  logic       slice_out_ready_i,
    output logic [7:0] slice_out_data_o,

    output logic       abi_constants_ok_o
);

    import npu_pkg::*;

    npu_vr_if #(.DATA_W(8)) fifo_in_if (.clk_i(clk_i), .rst_ni(rst_ni));
    npu_vr_if #(.DATA_W(8)) fifo_out_if (.clk_i(clk_i), .rst_ni(rst_ni));
    npu_vr_if #(.DATA_W(8)) skid_in_if (.clk_i(clk_i), .rst_ni(rst_ni));
    npu_vr_if #(.DATA_W(8)) skid_out_if (.clk_i(clk_i), .rst_ni(rst_ni));
    npu_vr_if #(.DATA_W(8)) slice_in_if (.clk_i(clk_i), .rst_ni(rst_ni));
    npu_vr_if #(.DATA_W(8)) slice_out_if (.clk_i(clk_i), .rst_ni(rst_ni));

    assign abi_constants_ok_o =
        (NPU_ABI_MAJOR == 2) &&
        (NPU_ABI_MINOR == 0) &&
        (NPU_DESC_SIZE_BYTES == 128) &&
        (NPU_DESC_ALIGN_BYTES == 16) &&
        (NPU_TENSOR_ALIGN_BYTES == 16) &&
        (NPU_ARRAY_K == 16) &&
        (NPU_ARRAY_N == 16) &&
        (NPU_INPUT_BITS == 8) &&
        (NPU_ACC_BITS == 32) &&
        (NPU_DEVICE_ID_RESET == 32'h4E50_5501) &&
        (NPU_ABI_VERSION_RESET == 32'h0002_0000) &&
        (NPU_CAP0_RESET == 32'h0000_003F) &&
        (NPU_CAP1_RESET == 32'h0820_1010);

    assign fifo_in_if.valid = fifo_in_valid_i;
    assign fifo_in_if.data = fifo_in_data_i;
    assign fifo_in_ready_o = fifo_in_if.ready;
    assign fifo_out_valid_o = fifo_out_if.valid;
    assign fifo_out_data_o = fifo_out_if.data;
    assign fifo_out_if.ready = fifo_out_ready_i;

    assign skid_in_if.valid = skid_in_valid_i;
    assign skid_in_if.data = skid_in_data_i;
    assign skid_in_ready_o = skid_in_if.ready;
    assign skid_out_valid_o = skid_out_if.valid;
    assign skid_out_data_o = skid_out_if.data;
    assign skid_out_if.ready = skid_out_ready_i;

    assign slice_in_if.valid = slice_in_valid_i;
    assign slice_in_if.data = slice_in_data_i;
    assign slice_in_ready_o = slice_in_if.ready;
    assign slice_out_valid_o = slice_out_if.valid;
    assign slice_out_data_o = slice_out_if.data;
    assign slice_out_if.ready = slice_out_ready_i;

    npu_fifo #(
        .DATA_W(8),
        .DEPTH(2)
    ) u_fifo (
        .in_i(fifo_in_if),
        .out_o(fifo_out_if),
        .count_o(fifo_count_o)
    );

    npu_skid_buffer #(
        .DATA_W(8)
    ) u_skid (
        .in_i(skid_in_if),
        .out_o(skid_out_if)
    );

    npu_register_slice #(
        .DATA_W(8)
    ) u_slice (
        .in_i(slice_in_if),
        .out_o(slice_out_if)
    );

endmodule
