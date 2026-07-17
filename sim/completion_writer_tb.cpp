#include "Vnpu_completion_writer.h"

#include "tb_coverage.hpp"

#include "holon_npu_program.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

#include <verilated.h>

namespace {

constexpr std::uint32_t kRespOkay = 0;
constexpr std::uint32_t kRespSlvErr = 2;

struct Beat {
    std::uint64_t lo;
    std::uint64_t hi;
    std::uint32_t strobe;
    bool last;
};

void eval(Vnpu_completion_writer& dut) {
    dut.eval();
}

void tick(Vnpu_completion_writer& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_completion_writer& dut) {
    dut.soft_reset_i = 0;
    dut.start_i = 0;
    dut.completion_addr_i = 0;
    dut.terminal_fault_i = 0;
    dut.fault_code_i = 0;
    dut.debug_pc_i = 0;
    dut.cycle_count_i = 0;
    dut.instret_i = 0;
    dut.m_axi_awready_i = 0;
    dut.m_axi_wready_i = 0;
    dut.m_axi_bresp_i = kRespOkay;
    dut.m_axi_bvalid_i = 0;
}

void reset(Vnpu_completion_writer& dut) {
    dut.clk_i = 0;
    dut.rst_ni = 0;
    clear_inputs(dut);
    tick(dut);
    dut.rst_ni = 1;
    eval(dut);
}

bool expect_eq(std::string_view name, std::uint64_t actual, std::uint64_t expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

void start_write(
    Vnpu_completion_writer& dut,
    std::uint64_t addr,
    bool terminal_fault,
    std::uint32_t fault_code
) {
    dut.completion_addr_i = addr;
    dut.terminal_fault_i = terminal_fault ? 1 : 0;
    dut.fault_code_i = fault_code;
    dut.debug_pc_i = 0x1234'5678U;
    dut.cycle_count_i = 0x0123'4567'89AB'CDEFULL;
    dut.instret_i = 0xFEDC'BA98'7654'3210ULL;
    dut.start_i = 1;
    tick(dut);
    dut.start_i = 0;
    eval(dut);
}

Beat accept_write_beat(Vnpu_completion_writer& dut) {
    dut.m_axi_wready_i = 1;
    eval(dut);
    const Beat beat{
        .lo = dut.m_axi_wdata_lo_o,
        .hi = dut.m_axi_wdata_hi_o,
        .strobe = dut.m_axi_wstrb_o,
        .last = dut.m_axi_wlast_o != 0,
    };
    tick(dut);
    dut.m_axi_wready_i = 0;
    eval(dut);
    return beat;
}

std::array<Beat, 2> accept_address_and_data(Vnpu_completion_writer& dut, bool& ok) {
    ok &= expect_eq("writer busy", dut.busy_o, 1U);
    ok &= expect_eq("AW valid", dut.m_axi_awvalid_o, 1U);
    ok &= expect_eq("AW address", dut.m_axi_awaddr_o, 0x100U);
    ok &= expect_eq("AW length", dut.m_axi_awlen_o, 1U);
    ok &= expect_eq("AW size", dut.m_axi_awsize_o, 4U);
    ok &= expect_eq("AW burst", dut.m_axi_awburst_o, 1U);

    tick(dut);
    ok &= expect_eq("AW stable under backpressure", dut.m_axi_awaddr_o, 0x100U);
    dut.m_axi_awready_i = 1;
    tick(dut);
    dut.m_axi_awready_i = 0;
    eval(dut);

    return {accept_write_beat(dut), accept_write_beat(dut)};
}

Beat accept_split_transaction(
    Vnpu_completion_writer& dut,
    std::uint64_t expected_address,
    bool& ok
) {
    ok &= expect_eq("split writer busy", dut.busy_o, 1U);
    ok &= expect_eq("split AW valid", dut.m_axi_awvalid_o, 1U);
    ok &= expect_eq("split AW address", dut.m_axi_awaddr_o, expected_address);
    ok &= expect_eq("split AW length", dut.m_axi_awlen_o, 0U);
    ok &= expect_eq("split AW size", dut.m_axi_awsize_o, 4U);
    ok &= expect_eq("split AW burst", dut.m_axi_awburst_o, 1U);
    dut.m_axi_awready_i = 1;
    tick(dut);
    dut.m_axi_awready_i = 0;
    const auto beat = accept_write_beat(dut);
    ok &= expect_eq("split beat last", beat.last, true);
    dut.m_axi_bvalid_i = 1;
    dut.m_axi_bresp_i = kRespOkay;
    tick(dut);
    dut.m_axi_bvalid_i = 0;
    eval(dut);
    return beat;
}

bool test_success_record(Vnpu_completion_writer& dut) {
    reset(dut);
    start_write(dut, 0x100, false, HOLON_NPU_FAULT_EXPLICIT_PROGRAM_FAULT);

    bool ok = true;
    const auto beats = accept_address_and_data(dut, ok);
    ok &= expect_eq("beat0 ABI", beats[0].lo & 0xFFFF'FFFFULL,
                    HOLON_NPU_ABI_VERSION_RESET);
    ok &= expect_eq("beat0 status", beats[0].lo >> 32,
                    HOLON_NPU_COMPLETION_STATUS_DONE);
    ok &= expect_eq("beat0 fault", beats[0].hi & 0xFFFF'FFFFULL,
                    HOLON_NPU_FAULT_NONE);
    ok &= expect_eq("beat0 PC", beats[0].hi >> 32, 0x1234'5678U);
    ok &= expect_eq("beat0 strobe", beats[0].strobe, 0xFFFFU);
    ok &= expect_eq("beat0 last", beats[0].last, false);
    ok &= expect_eq("beat1 cycles", beats[1].lo, 0x0123'4567'89AB'CDEFULL);
    ok &= expect_eq("beat1 instret", beats[1].hi, 0xFEDC'BA98'7654'3210ULL);
    ok &= expect_eq("beat1 strobe", beats[1].strobe, 0xFFFFU);
    ok &= expect_eq("beat1 last", beats[1].last, true);

    dut.m_axi_bvalid_i = 1;
    dut.m_axi_bresp_i = kRespOkay;
    tick(dut);
    dut.m_axi_bvalid_i = 0;
    eval(dut);
    ok &= expect_eq("success done", dut.done_o, 1U);
    ok &= expect_eq("success fault", dut.fault_o, 0U);
    return ok;
}

bool test_fault_record(Vnpu_completion_writer& dut) {
    reset(dut);
    start_write(dut, 0x100, true, HOLON_NPU_FAULT_EXPLICIT_PROGRAM_FAULT);

    bool ok = true;
    const auto beats = accept_address_and_data(dut, ok);
    ok &= expect_eq("fault record status", beats[0].lo >> 32,
                    HOLON_NPU_COMPLETION_STATUS_FAULT);
    ok &= expect_eq("fault record code", beats[0].hi & 0xFFFF'FFFFULL,
                    HOLON_NPU_FAULT_EXPLICIT_PROGRAM_FAULT);

    dut.m_axi_bvalid_i = 1;
    dut.m_axi_bresp_i = kRespOkay;
    tick(dut);
    dut.m_axi_bvalid_i = 0;
    eval(dut);
    ok &= expect_eq("fault record write done", dut.done_o, 1U);
    ok &= expect_eq("fault record writer fault", dut.fault_o, 0U);
    return ok;
}

bool test_write_response_fault(Vnpu_completion_writer& dut) {
    reset(dut);
    start_write(dut, 0x100, false, 0);

    bool ok = true;
    static_cast<void>(accept_address_and_data(dut, ok));
    dut.m_axi_bvalid_i = 1;
    dut.m_axi_bresp_i = kRespSlvErr;
    tick(dut);
    dut.m_axi_bvalid_i = 0;
    eval(dut);
    ok &= expect_eq("BRESP fault", dut.fault_o, 1U);
    ok &= expect_eq("BRESP fault code", dut.fault_code_o, HOLON_NPU_FAULT_AXI_WRITE);
    return ok;
}

bool test_4k_boundary_split(Vnpu_completion_writer& dut) {
    reset(dut);
    start_write(dut, 0x0FF0, false, HOLON_NPU_FAULT_NONE);

    bool ok = true;
    const auto beat0 = accept_split_transaction(dut, 0x0FF0, ok);
    const auto beat1 = accept_split_transaction(dut, 0x1000, ok);
    ok &= expect_eq("split beat0 ABI", beat0.lo & 0xFFFF'FFFFULL,
                    HOLON_NPU_ABI_VERSION_RESET);
    ok &= expect_eq("split beat1 cycles", beat1.lo, 0x0123'4567'89AB'CDEFULL);
    ok &= expect_eq("split writer done", dut.done_o, 1U);
    ok &= expect_eq("split writer fault", dut.fault_o, 0U);
    return ok;
}

bool test_invalid_address(Vnpu_completion_writer& dut) {
    reset(dut);
    start_write(dut, 0x108, false, 0);
    bool ok = true;
    ok &= expect_eq("misaligned address fault", dut.fault_o, 1U);
    ok &= expect_eq("misaligned address no AW", dut.m_axi_awvalid_o, 0U);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_completion_writer", argc, argv};
    Vnpu_completion_writer dut;

    bool ok = true;
    using enum holon_npu_tb::coverage_point;
    const bool success_case = test_success_record(dut);
    test.observe(completion_record_done, success_case);
    ok &= success_case;

    const bool fault_case = test_fault_record(dut);
    test.observe(completion_record_fault, fault_case);
    ok &= fault_case;

    const bool response_fault_case = test_write_response_fault(dut);
    test.observe(completion_record_axi_error, response_fault_case);
    ok &= response_fault_case;

    const bool boundary_case = test_4k_boundary_split(dut);
    test.observe(completion_record_4k_split, boundary_case);
    ok &= boundary_case;
    ok &= test_invalid_address(dut);

    dut.final();
    return test.finish(ok);
}
