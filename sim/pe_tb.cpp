#include "Vnpu_pe_i8.h"

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
    dut.valid_i = 0;
    dut.mask_i = 0;
    dut.a_i = 0;
    dut.b_i = 0;
    tick(dut);
    dut.rst_ni = 1;
    eval(dut);
}

void clear_acc(Vnpu_pe_i8& dut) {
    dut.clear_i = 1;
    dut.valid_i = 0;
    dut.mask_i = 0;
    tick(dut);
    dut.clear_i = 0;
    eval(dut);
}

std::uint32_t acc_bits(const Vnpu_pe_i8& dut) {
    return static_cast<std::uint32_t>(dut.acc_o);
}

void drive_product(Vnpu_pe_i8& dut, std::int8_t a, std::int8_t b, bool mask) {
    dut.valid_i = 1;
    dut.mask_i = mask ? 1 : 0;
    dut.a_i = static_cast<std::uint8_t>(a);
    dut.b_i = static_cast<std::uint8_t>(b);
    tick(dut);
}

bool expect_eq(std::string_view name, std::uint32_t actual, std::uint32_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

bool test_reset_and_mask(Vnpu_pe_i8& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("pe valid after reset", dut.valid_o, 0U);
    ok &= expect_eq("pe acc after reset", acc_bits(dut), 0U);

    drive_product(dut, 7, 9, false);
    ok &= expect_eq("pe valid after masked step", dut.valid_o, 0U);
    ok &= expect_eq("pe acc after masked step", acc_bits(dut), 0U);

    return ok;
}

bool test_positive_negative_zero(Vnpu_pe_i8& dut) {
    reset(dut);

    bool ok = true;

    drive_product(dut, 3, 4, true);
    ok &= expect_eq("pe positive valid", dut.valid_o, 1U);
    ok &= expect_eq("pe positive product", acc_bits(dut), 12U);

    clear_acc(dut);
    drive_product(dut, -7, 6, true);
    ok &= expect_eq("pe negative valid", dut.valid_o, 1U);
    ok &= expect_eq("pe negative product", acc_bits(dut), 0xFFFF'FFD6U);

    clear_acc(dut);
    drive_product(dut, -128, 0, true);
    ok &= expect_eq("pe zero valid", dut.valid_o, 1U);
    ok &= expect_eq("pe zero product", acc_bits(dut), 0U);

    return ok;
}

bool test_int32_wrap_boundary(Vnpu_pe_i8& dut) {
    reset(dut);

    dut.valid_i = 1;
    dut.mask_i = 1;
    dut.a_i = static_cast<std::uint8_t>(std::int8_t{-128});
    dut.b_i = static_cast<std::uint8_t>(std::int8_t{-128});

    for (int i = 0; i < 131072; ++i) {
        tick(dut);
    }

    bool ok = true;
    ok &= expect_eq("pe int32 wrap boundary", acc_bits(dut), 0x8000'0000U);

    tick(dut);
    ok &= expect_eq("pe int32 wrap after boundary", acc_bits(dut), 0x8000'4000U);

    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Vnpu_pe_i8 dut;
    bool ok = true;

    ok &= test_reset_and_mask(dut);
    ok &= test_positive_negative_zero(dut);
    ok &= test_int32_wrap_boundary(dut);

    dut.final();
    return ok ? 0 : 1;
}
