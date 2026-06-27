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

    assign abi_constants_ok_o =
        (NPU_ABI_MAJOR == 1) &&
        (NPU_ABI_MINOR == 0) &&
        (NPU_DESC_SIZE_BYTES == 128) &&
        (NPU_DESC_ALIGN_BYTES == 16) &&
        (NPU_TENSOR_ALIGN_BYTES == 16) &&
        (NPU_ARRAY_M == 16) &&
        (NPU_ARRAY_N == 16) &&
        (NPU_INPUT_BITS == 8) &&
        (NPU_ACC_BITS == 32) &&
        (NPU_DEVICE_ID_RESET == 32'h4E50_5501) &&
        (NPU_ABI_VERSION_RESET == 32'h0001_0000) &&
        (NPU_CAP0_RESET == 32'h0000_003F) &&
        (NPU_CAP1_RESET == 32'h0820_1010);

    npu_fifo #(
        .DATA_W(8),
        .DEPTH(2)
    ) u_fifo (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .in_valid_i(fifo_in_valid_i),
        .in_ready_o(fifo_in_ready_o),
        .in_data_i(fifo_in_data_i),
        .out_valid_o(fifo_out_valid_o),
        .out_ready_i(fifo_out_ready_i),
        .out_data_o(fifo_out_data_o),
        .count_o(fifo_count_o)
    );

    npu_skid_buffer #(
        .DATA_W(8)
    ) u_skid (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .in_valid_i(skid_in_valid_i),
        .in_ready_o(skid_in_ready_o),
        .in_data_i(skid_in_data_i),
        .out_valid_o(skid_out_valid_o),
        .out_ready_i(skid_out_ready_i),
        .out_data_o(skid_out_data_o)
    );

    npu_register_slice #(
        .DATA_W(8)
    ) u_slice (
        .clk_i(clk_i),
        .rst_ni(rst_ni),
        .in_valid_i(slice_in_valid_i),
        .in_ready_o(slice_in_ready_o),
        .in_data_i(slice_in_data_i),
        .out_valid_o(slice_out_valid_o),
        .out_ready_i(slice_out_ready_i),
        .out_data_o(slice_out_data_o)
    );

endmodule
