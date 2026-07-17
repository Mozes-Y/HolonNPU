#include "Vnpu_pe_i8.h"

#include "tb_coverage.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

#include <verilated.h>

namespace {

void eval(Vnpu_pe_i8& dut) {
    dut.eval();
}

void tick(Vnpu_pe_i8& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void reset(Vnpu_pe_i8& dut) {
    dut.clk_i = 0;
    dut.rst_ni = 0;
    dut.clear_i = 0;
    dut.weight_valid_i = 0;
    dut.weight_mask_i = 0;
    dut.weight_i = 0;
    dut.valid_i = 0;
    dut.mask_i = 0;
    dut.a_i = 0;
    dut.psum_i = 0;
    tick(dut);
    dut.rst_ni = 1;
    eval(dut);
}

void clear_pe(Vnpu_pe_i8& dut) {
    dut.clear_i = 1;
    dut.weight_valid_i = 0;
    dut.valid_i = 0;
    dut.mask_i = 0;
    tick(dut);
    dut.clear_i = 0;
    eval(dut);
}

void load_weight(Vnpu_pe_i8& dut, std::int8_t weight, bool mask = true) {
    dut.weight_valid_i = 1;
    dut.weight_mask_i = mask ? 1 : 0;
    dut.weight_i = static_cast<std::uint8_t>(weight);
    dut.valid_i = 0;
    tick(dut);
    dut.weight_valid_i = 0;
    dut.weight_mask_i = 0;
    eval(dut);
}

void drive_product(Vnpu_pe_i8& dut, std::int8_t a, std::uint32_t psum, bool mask) {
    dut.valid_i = 1;
    dut.mask_i = mask ? 1 : 0;
    dut.a_i = static_cast<std::uint8_t>(a);
    dut.psum_i = psum;
    tick(dut);
    dut.valid_i = 0;
    dut.mask_i = 0;
    eval(dut);
}

std::uint32_t psum_bits(const Vnpu_pe_i8& dut) {
    return static_cast<std::uint32_t>(dut.psum_o);
}

bool expect_eq(std::string_view name, std::uint32_t actual, std::uint32_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

bool test_reset_clear_and_masked_weight(Vnpu_pe_i8& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("pe valid after reset", dut.valid_o, 0U);
    ok &= expect_eq("pe psum after reset", psum_bits(dut), 0U);

    load_weight(dut, 7, false);
    drive_product(dut, 9, 5, true);
    ok &= expect_eq("pe masked weight valid", dut.valid_o, 1U);
    ok &= expect_eq("pe masked weight keeps zero", psum_bits(dut), 5U);

    load_weight(dut, 7);
    drive_product(dut, 9, 5, true);
    ok &= expect_eq("pe loaded weight product", psum_bits(dut), 68U);

    clear_pe(dut);
    drive_product(dut, 9, 5, true);
    ok &= expect_eq("pe clear resets weight", psum_bits(dut), 5U);

    return ok;
}

bool test_positive_negative_zero_and_pass_through(Vnpu_pe_i8& dut) {
    reset(dut);

    bool ok = true;

    load_weight(dut, 4);
    drive_product(dut, 3, 10, true);
    ok &= expect_eq("pe positive valid", dut.valid_o, 1U);
    ok &= expect_eq("pe positive psum", psum_bits(dut), 22U);

    load_weight(dut, 6);
    drive_product(dut, -7, 10, true);
    ok &= expect_eq("pe negative psum", psum_bits(dut), 0xFFFF'FFE0U);

    load_weight(dut, 0);
    drive_product(dut, -128, 10, true);
    ok &= expect_eq("pe zero weight psum", psum_bits(dut), 10U);

    load_weight(dut, 11);
    drive_product(dut, 5, 0x1234'5678U, false);
    ok &= expect_eq("pe masked compute pass through", psum_bits(dut), 0x1234'5678U);

    return ok;
}

bool test_int32_wrap_boundary(Vnpu_pe_i8& dut) {
    reset(dut);
    load_weight(dut, -128);

    bool ok = true;
    drive_product(dut, -128, 0x7FFF'C000U, true);
    ok &= expect_eq("pe int32 wrap boundary", psum_bits(dut), 0x8000'0000U);

    drive_product(dut, -128, 0x7FFF'FFFFU, true);
    ok &= expect_eq("pe int32 wrap after boundary", psum_bits(dut), 0x8000'3FFFU);

    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_pe", argc, argv};

    Vnpu_pe_i8 dut;
    bool ok = true;

    using enum holon_npu_tb::coverage_point;
    const bool weight_case = test_reset_clear_and_masked_weight(dut);
    test.observe({pe_weight_load, pe_masked_weight}, weight_case);
    ok &= weight_case;

    const bool signed_case = test_positive_negative_zero_and_pass_through(dut);
    test.observe(pe_negative_operands, signed_case);
    ok &= signed_case;

    const bool wrap_case = test_int32_wrap_boundary(dut);
    test.observe(pe_int32_wrap, wrap_case);
    ok &= wrap_case;

    dut.final();
    return test.finish(ok);
}
