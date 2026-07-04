#include "Vnpu_common_smoke_top.h"

#include "tb_coverage.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

#include <verilated.h>

namespace {

void eval(Vnpu_common_smoke_top& dut) {
    dut.eval();
}

void tick(Vnpu_common_smoke_top& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void set_idle_inputs(Vnpu_common_smoke_top& dut) {
    dut.fifo_in_valid_i = 0;
    dut.fifo_in_data_i = 0;
    dut.fifo_out_ready_i = 0;

    dut.skid_in_valid_i = 0;
    dut.skid_in_data_i = 0;
    dut.skid_out_ready_i = 0;

    dut.slice_in_valid_i = 0;
    dut.slice_in_data_i = 0;
    dut.slice_out_ready_i = 0;
}

void reset(Vnpu_common_smoke_top& dut) {
    dut.clk_i = 0;
    dut.rst_ni = 0;
    set_idle_inputs(dut);
    tick(dut);
    dut.rst_ni = 1;
    eval(dut);
}

bool expect_eq(std::string_view name, std::uint32_t actual, std::uint32_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

bool test_package_constants(Vnpu_common_smoke_top& dut) {
    reset(dut);
    return expect_eq("abi_constants_ok_o", dut.abi_constants_ok_o, 1U);
}

bool test_fifo(Vnpu_common_smoke_top& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("fifo ready after reset", dut.fifo_in_ready_o, 1U);
    ok &= expect_eq("fifo valid after reset", dut.fifo_out_valid_o, 0U);
    ok &= expect_eq("fifo count after reset", dut.fifo_count_o, 0U);

    dut.fifo_in_valid_i = 1;
    dut.fifo_in_data_i = 0x11;
    dut.fifo_out_ready_i = 0;
    tick(dut);
    ok &= expect_eq("fifo count after first push", dut.fifo_count_o, 1U);
    ok &= expect_eq("fifo valid after first push", dut.fifo_out_valid_o, 1U);
    ok &= expect_eq("fifo data after first push", dut.fifo_out_data_o, 0x11U);
    ok &= expect_eq("fifo ready with one entry", dut.fifo_in_ready_o, 1U);

    dut.fifo_in_data_i = 0x22;
    tick(dut);
    dut.fifo_in_valid_i = 0;
    eval(dut);
    ok &= expect_eq("fifo count when full", dut.fifo_count_o, 2U);
    ok &= expect_eq("fifo ready when full", dut.fifo_in_ready_o, 0U);
    ok &= expect_eq("fifo first data remains at output", dut.fifo_out_data_o, 0x11U);

    dut.fifo_out_ready_i = 1;
    tick(dut);
    ok &= expect_eq("fifo count after first pop", dut.fifo_count_o, 1U);
    ok &= expect_eq("fifo second data at output", dut.fifo_out_data_o, 0x22U);
    ok &= expect_eq("fifo ready after first pop", dut.fifo_in_ready_o, 1U);

    tick(dut);
    ok &= expect_eq("fifo count after second pop", dut.fifo_count_o, 0U);
    ok &= expect_eq("fifo valid after drain", dut.fifo_out_valid_o, 0U);

    return ok;
}

bool test_skid_buffer(Vnpu_common_smoke_top& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("skid ready after reset", dut.skid_in_ready_o, 1U);
    ok &= expect_eq("skid valid after reset", dut.skid_out_valid_o, 0U);

    dut.skid_in_valid_i = 1;
    dut.skid_in_data_i = 0xA1;
    dut.skid_out_ready_i = 0;
    eval(dut);
    ok &= expect_eq("skid transparent valid", dut.skid_out_valid_o, 1U);
    ok &= expect_eq("skid transparent data", dut.skid_out_data_o, 0xA1U);
    ok &= expect_eq("skid accepts while empty", dut.skid_in_ready_o, 1U);

    tick(dut);
    ok &= expect_eq("skid holds valid under backpressure", dut.skid_out_valid_o, 1U);
    ok &= expect_eq("skid holds data under backpressure", dut.skid_out_data_o, 0xA1U);
    ok &= expect_eq("skid blocks input while full", dut.skid_in_ready_o, 0U);

    dut.skid_in_valid_i = 0;
    dut.skid_out_ready_i = 1;
    tick(dut);
    ok &= expect_eq("skid drains after consume", dut.skid_out_valid_o, 0U);
    ok &= expect_eq("skid ready after consume", dut.skid_in_ready_o, 1U);

    dut.skid_in_valid_i = 1;
    dut.skid_in_data_i = 0xA2;
    dut.skid_out_ready_i = 1;
    tick(dut);
    ok &= expect_eq("skid new data accepted", dut.skid_out_valid_o, 1U);
    ok &= expect_eq("skid new data", dut.skid_out_data_o, 0xA2U);

    dut.skid_in_valid_i = 0;
    tick(dut);
    ok &= expect_eq("skid drains after final consume", dut.skid_out_valid_o, 0U);
    ok &= expect_eq("skid ready after drain", dut.skid_in_ready_o, 1U);

    return ok;
}

bool test_register_slice(Vnpu_common_smoke_top& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("slice ready after reset", dut.slice_in_ready_o, 1U);
    ok &= expect_eq("slice valid after reset", dut.slice_out_valid_o, 0U);

    dut.slice_in_valid_i = 1;
    dut.slice_in_data_i = 0x51;
    dut.slice_out_ready_i = 0;
    tick(dut);
    ok &= expect_eq("slice valid after capture", dut.slice_out_valid_o, 1U);
    ok &= expect_eq("slice data after capture", dut.slice_out_data_o, 0x51U);
    ok &= expect_eq("slice not ready while full", dut.slice_in_ready_o, 0U);

    dut.slice_in_valid_i = 0;
    dut.slice_out_ready_i = 1;
    tick(dut);
    ok &= expect_eq("slice drains after consume", dut.slice_out_valid_o, 0U);
    ok &= expect_eq("slice ready after consume", dut.slice_in_ready_o, 1U);

    dut.slice_in_valid_i = 1;
    dut.slice_in_data_i = 0x62;
    dut.slice_out_ready_i = 1;
    tick(dut);
    ok &= expect_eq("slice new data accepted", dut.slice_out_valid_o, 1U);
    ok &= expect_eq("slice new data", dut.slice_out_data_o, 0x62U);

    dut.slice_in_valid_i = 0;
    tick(dut);
    ok &= expect_eq("slice drains after final consume", dut.slice_out_valid_o, 0U);
    ok &= expect_eq("slice ready after drain", dut.slice_in_ready_o, 1U);

    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_common", argc, argv};

    Vnpu_common_smoke_top dut;
    bool ok = true;

    ok &= test_package_constants(dut);
    ok &= test_fifo(dut);
    ok &= test_skid_buffer(dut);
    ok &= test_register_slice(dut);

    dut.final();
    using enum holon_npu_tb::coverage_point;
    test.cover({common_fifo, common_skid_buffer, common_register_slice});
    return test.finish(ok);
}
