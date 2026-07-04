#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace holon_npu_tb {

enum class coverage_point : std::uint16_t {
    smoke_basic,
    common_fifo,
    common_skid_buffer,
    common_register_slice,
    pe_weight_load,
    pe_masked_weight,
    pe_negative_operands,
    pe_int32_wrap,
    array_shape_1,
    array_shape_16,
    array_shape_mixed_tail,
    tiling_masks,
    tiling_buffers,
    tiling_masked_tail,
    tiling_schedule,
    axi_lite_aw_w_skew,
    control_done_irq,
    control_error_irq,
    control_soft_reset,
    dma_read_single_burst,
    dma_read_multi_burst,
    dma_read_alignment_error,
    dma_read_axi_error,
    dma_write_single_burst,
    dma_write_multi_burst,
    dma_write_alignment_error,
    dma_write_axi_error,
    descriptor_valid,
    descriptor_invalid_version,
    descriptor_invalid_size,
    descriptor_invalid_flags,
    descriptor_reserved_nonzero,
    descriptor_fuzz,
    gemm_shape_1,
    gemm_shape_lt16,
    gemm_shape_16,
    gemm_shape_16_tail,
    gemm_shape_multi_tile,
    gemm_tail_m,
    gemm_tail_n,
    gemm_tail_k,
    gemm_tail_mixed,
    gemm_shape_64,
    gemm_reset_in_flight,
    gemm_axi_read_error,
    gemm_axi_write_error,
    top_gemm_fixed,
    top_gemm_constrained_random,
    top_status_done,
    top_status_error,
    top_axil_write_skew,
    top_descriptor_read_error,
    top_gemm_read_error,
    top_gemm_write_error,
    top_soft_reset_in_flight,
};

struct coverage_point_info {
    coverage_point point;
    std::string_view name;
    std::string_view description;
};

inline constexpr std::array coverage_registry{
    coverage_point_info{coverage_point::smoke_basic, "smoke_basic", "Smoke test completed."},
    coverage_point_info{coverage_point::common_fifo, "common_fifo", "Common FIFO behavior."},
    coverage_point_info{coverage_point::common_skid_buffer, "common_skid_buffer", "Skid buffer behavior."},
    coverage_point_info{coverage_point::common_register_slice, "common_register_slice", "Register slice behavior."},
    coverage_point_info{coverage_point::pe_weight_load, "pe_weight_load", "PE B weight load path."},
    coverage_point_info{coverage_point::pe_masked_weight, "pe_masked_weight", "PE masked weight behavior."},
    coverage_point_info{coverage_point::pe_negative_operands, "pe_negative_operands", "Signed negative PE operands."},
    coverage_point_info{coverage_point::pe_int32_wrap, "pe_int32_wrap", "INT32 wraparound arithmetic."},
    coverage_point_info{coverage_point::array_shape_1, "array_shape_1", "1x1x1 array shape."},
    coverage_point_info{coverage_point::array_shape_16, "array_shape_16", "16x16x16 array shape."},
    coverage_point_info{coverage_point::array_shape_mixed_tail, "array_shape_mixed_tail", "Mixed-tail array shape."},
    coverage_point_info{coverage_point::tiling_masks, "tiling_masks", "Tile mask generation."},
    coverage_point_info{coverage_point::tiling_buffers, "tiling_buffers", "Reusable tile buffers."},
    coverage_point_info{coverage_point::tiling_masked_tail, "tiling_masked_tail", "Masked tail compute path."},
    coverage_point_info{coverage_point::tiling_schedule, "tiling_schedule", "Tiling schedule progression."},
    coverage_point_info{coverage_point::axi_lite_aw_w_skew, "axi_lite_aw_w_skew", "AXI-Lite AW/W skew."},
    coverage_point_info{coverage_point::control_done_irq, "control_done_irq", "Control done IRQ path."},
    coverage_point_info{coverage_point::control_error_irq, "control_error_irq", "Control error IRQ path."},
    coverage_point_info{coverage_point::control_soft_reset, "control_soft_reset", "Control soft reset path."},
    coverage_point_info{coverage_point::dma_read_single_burst, "dma_read_single_burst", "Read DMA single burst."},
    coverage_point_info{coverage_point::dma_read_multi_burst, "dma_read_multi_burst", "Read DMA multi-burst transfer."},
    coverage_point_info{coverage_point::dma_read_alignment_error, "dma_read_alignment_error", "Read DMA alignment rejection."},
    coverage_point_info{coverage_point::dma_read_axi_error, "dma_read_axi_error", "Read DMA AXI error."},
    coverage_point_info{coverage_point::dma_write_single_burst, "dma_write_single_burst", "Write DMA single burst."},
    coverage_point_info{coverage_point::dma_write_multi_burst, "dma_write_multi_burst", "Write DMA multi-burst transfer."},
    coverage_point_info{coverage_point::dma_write_alignment_error, "dma_write_alignment_error", "Write DMA alignment rejection."},
    coverage_point_info{coverage_point::dma_write_axi_error, "dma_write_axi_error", "Write DMA AXI error."},
    coverage_point_info{coverage_point::descriptor_valid, "descriptor_valid", "Valid descriptor issue."},
    coverage_point_info{coverage_point::descriptor_invalid_version, "descriptor_invalid_version", "Invalid descriptor version."},
    coverage_point_info{coverage_point::descriptor_invalid_size, "descriptor_invalid_size", "Invalid descriptor size."},
    coverage_point_info{coverage_point::descriptor_invalid_flags, "descriptor_invalid_flags", "Invalid descriptor flags."},
    coverage_point_info{coverage_point::descriptor_reserved_nonzero, "descriptor_reserved_nonzero", "Reserved descriptor fields."},
    coverage_point_info{coverage_point::descriptor_fuzz, "descriptor_fuzz", "Descriptor fuzz cases."},
    coverage_point_info{coverage_point::gemm_shape_1, "gemm_shape_1", "GEMM 1-sized shape."},
    coverage_point_info{coverage_point::gemm_shape_lt16, "gemm_shape_lt16", "GEMM sub-16 shape."},
    coverage_point_info{coverage_point::gemm_shape_16, "gemm_shape_16", "GEMM exact 16 tile."},
    coverage_point_info{coverage_point::gemm_shape_16_tail, "gemm_shape_16_tail", "GEMM 16 plus tail shape."},
    coverage_point_info{coverage_point::gemm_shape_multi_tile, "gemm_shape_multi_tile", "GEMM multi-tile shape."},
    coverage_point_info{coverage_point::gemm_tail_m, "gemm_tail_m", "GEMM M tail."},
    coverage_point_info{coverage_point::gemm_tail_n, "gemm_tail_n", "GEMM N tail."},
    coverage_point_info{coverage_point::gemm_tail_k, "gemm_tail_k", "GEMM K tail."},
    coverage_point_info{coverage_point::gemm_tail_mixed, "gemm_tail_mixed", "GEMM mixed tail."},
    coverage_point_info{coverage_point::gemm_shape_64, "gemm_shape_64", "GEMM 64-sized anchor."},
    coverage_point_info{coverage_point::gemm_reset_in_flight, "gemm_reset_in_flight", "GEMM reset while active."},
    coverage_point_info{coverage_point::gemm_axi_read_error, "gemm_axi_read_error", "GEMM AXI read error."},
    coverage_point_info{coverage_point::gemm_axi_write_error, "gemm_axi_write_error", "GEMM AXI write error."},
    coverage_point_info{coverage_point::top_gemm_fixed, "top_gemm_fixed", "Top fixed GEMM cases."},
    coverage_point_info{coverage_point::top_gemm_constrained_random, "top_gemm_constrained_random", "Top random GEMM cases."},
    coverage_point_info{coverage_point::top_status_done, "top_status_done", "Top done status path."},
    coverage_point_info{coverage_point::top_status_error, "top_status_error", "Top error status path."},
    coverage_point_info{coverage_point::top_axil_write_skew, "top_axil_write_skew", "Top AXI-Lite write skew."},
    coverage_point_info{coverage_point::top_descriptor_read_error, "top_descriptor_read_error", "Top descriptor read error."},
    coverage_point_info{coverage_point::top_gemm_read_error, "top_gemm_read_error", "Top GEMM read error."},
    coverage_point_info{coverage_point::top_gemm_write_error, "top_gemm_write_error", "Top GEMM write error."},
    coverage_point_info{coverage_point::top_soft_reset_in_flight, "top_soft_reset_in_flight", "Top soft reset while active."},
};

std::string_view coverage_point_name(coverage_point point);

class test_run {
public:
    test_run(std::string_view test_name, int argc, char** argv);

    test_run(const test_run&) = delete;
    test_run& operator=(const test_run&) = delete;

    test_run(test_run&&) = delete;
    test_run& operator=(test_run&&) = delete;

    void cover(std::initializer_list<coverage_point> points);
    [[nodiscard]] int finish(bool passed) const;

private:
    std::string test_name_;
    std::string sanitized_name_;
    std::filesystem::path coverage_root_;
    std::vector<coverage_point> hit_points_;
    std::vector<std::string> verilator_args_;
    std::vector<char*> verilator_argv_;
};

}  // namespace holon_npu_tb
