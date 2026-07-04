#include "Vnpu_smoke_top.h"

#include "tb_coverage.hpp"

#include <cstdint>
#include <iostream>

#include <verilated.h>

namespace {

void tick(Vnpu_smoke_top& dut) {
    dut.clk_i = 0;
    dut.eval();
    dut.clk_i = 1;
    dut.eval();
    dut.clk_i = 0;
    dut.eval();
}

bool expect_eq(const char* name, std::uint32_t actual, std::uint32_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_smoke", argc, argv};

    Vnpu_smoke_top dut;
    bool ok = true;
    dut.clk_i = 0;
    dut.rst_ni = 0;
    dut.in_valid_i = 0;
    dut.in_data_i = 0;

    tick(dut);

    ok &= expect_eq("out_valid_o after reset", dut.out_valid_o, 0U);
    ok &= expect_eq("out_data_o after reset", dut.out_data_o, 0U);

    dut.rst_ni = 1;
    dut.in_valid_i = 1;
    dut.in_data_i = 0x41;
    tick(dut);

    ok &= expect_eq("out_valid_o after valid input", dut.out_valid_o, 1U);
    ok &= expect_eq("out_data_o after valid input", dut.out_data_o, 0x42U);

    dut.in_valid_i = 0;
    tick(dut);

    ok &= expect_eq("out_valid_o after idle input", dut.out_valid_o, 0U);

    dut.final();
    test.cover({holon_npu_tb::coverage_point::smoke_basic});
    return test.finish(ok);
}
