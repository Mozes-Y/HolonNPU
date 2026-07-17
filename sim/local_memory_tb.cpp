#include "Vnpu_local_memory.h"

#include "tb_coverage.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

#include <verilated.h>

namespace {

void eval(Vnpu_local_memory& dut) {
    dut.eval();
}

void tick(Vnpu_local_memory& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_local_memory& dut) {
    dut.soft_reset_i = 0;
    dut.program_wr_valid_i = 0;
    dut.program_wr_addr_i = 0;
    dut.program_wr_data_i = 0;
    dut.data_wr_valid_i = 0;
    dut.data_wr_addr_i = 0;
    dut.data_wr_data_i = 0;
    dut.data_wr_strb_i = 0;
    dut.client_data_wr_valid_i = 0;
    dut.client_data_wr_addr_i = 0;
    dut.client_data_wr_data_i = 0;
    dut.client_data_wr_strb_i = 0;
    dut.program_rd_valid_i = 0;
    dut.program_rd_addr_i = 0;
    dut.data_rd_valid_i = 0;
    dut.data_rd_addr_i = 0;
}

void reset(Vnpu_local_memory& dut) {
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

bool program_write(Vnpu_local_memory& dut, std::uint32_t addr, std::uint32_t data) {
    dut.program_wr_valid_i = 1;
    dut.program_wr_addr_i = addr;
    dut.program_wr_data_i = data;
    tick(dut);
    const bool error = dut.program_wr_error_o != 0;
    dut.program_wr_valid_i = 0;
    eval(dut);
    return !error;
}

bool data_write(
    Vnpu_local_memory& dut,
    std::uint32_t addr,
    std::uint32_t data,
    std::uint8_t strobe = 0xFU
) {
    dut.data_wr_valid_i = 1;
    dut.data_wr_addr_i = addr;
    dut.data_wr_data_i = data;
    dut.data_wr_strb_i = strobe;
    tick(dut);
    const bool error = dut.data_wr_error_o != 0;
    dut.data_wr_valid_i = 0;
    eval(dut);
    return !error;
}

bool client_data_write(Vnpu_local_memory& dut, std::uint32_t addr, std::uint32_t data) {
    dut.client_data_wr_valid_i = 1;
    dut.client_data_wr_addr_i = addr;
    dut.client_data_wr_data_i = data;
    dut.client_data_wr_strb_i = 0xFU;
    bool accepted = false;
    for (int cycle = 0; cycle < 4; ++cycle) {
        eval(dut);
        if (dut.client_data_wr_ready_o) {
            accepted = true;
            tick(dut);
            break;
        }
        tick(dut);
    }
    dut.client_data_wr_valid_i = 0;
    eval(dut);
    return accepted;
}

struct ReadResult {
    bool valid = false;
    bool ready = false;
    bool error = false;
    std::uint32_t data = 0;
};

ReadResult program_read(Vnpu_local_memory& dut, std::uint32_t addr) {
    dut.program_rd_valid_i = 1;
    dut.program_rd_addr_i = addr;
    eval(dut);
    const bool ready = dut.program_rd_ready_o != 0;
    tick(dut);
    ReadResult result{
        .valid = dut.program_rd_valid_o != 0,
        .ready = ready,
        .error = dut.program_rd_error_o != 0,
        .data = static_cast<std::uint32_t>(dut.program_rd_data_o),
    };
    dut.program_rd_valid_i = 0;
    tick(dut);
    return result;
}

ReadResult data_read(Vnpu_local_memory& dut, std::uint32_t addr) {
    dut.data_rd_valid_i = 1;
    dut.data_rd_addr_i = addr;
    eval(dut);
    const bool ready = dut.data_rd_ready_o != 0;
    tick(dut);
    ReadResult result{
        .valid = dut.data_rd_valid_o != 0,
        .ready = ready,
        .error = dut.data_rd_error_o != 0,
        .data = static_cast<std::uint32_t>(dut.data_rd_data_o),
    };
    dut.data_rd_valid_i = 0;
    tick(dut);
    return result;
}

bool test_program_and_data_storage(Vnpu_local_memory& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("program ready", dut.program_wr_ready_o, 1);
    ok &= expect_eq("data ready", dut.data_wr_ready_o, 1);
    ok &= expect_eq("program write 0", program_write(dut, 0, 0x1020'3040U), true);
    ok &= expect_eq("program write 4", program_write(dut, 4, 0x5060'7080U), true);
    ok &= expect_eq("data write 0", data_write(dut, 0, 0xA0B0'C0D0U), true);
    ok &= expect_eq("data write 4", data_write(dut, 4, 0x1122'3344U), true);
    ok &= expect_eq("data byte write", data_write(dut, 4, 0xAABB'CCDDU, 0x5U), true);

    const auto program0 = program_read(dut, 0);
    const auto program4 = program_read(dut, 4);
    const auto data0 = data_read(dut, 0);
    const auto data4 = data_read(dut, 4);
    ok &= expect_eq("program read valid", program0.valid, true);
    ok &= expect_eq("program read ready", program0.ready, true);
    ok &= expect_eq("program read error", program0.error, false);
    ok &= expect_eq("program word 0", program0.data, 0x1020'3040U);
    ok &= expect_eq("program word 4", program4.data, 0x5060'7080U);
    ok &= expect_eq("data read valid", data0.valid, true);
    ok &= expect_eq("data read ready", data0.ready, true);
    ok &= expect_eq("data read error", data0.error, false);
    ok &= expect_eq("data word 0", data0.data, 0xA0B0'C0D0U);
    ok &= expect_eq("data word 4", data4.data, 0x11BB'33DDU);
    return ok;
}

bool test_bounds_and_alignment_faults(Vnpu_local_memory& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("misaligned program write rejected", program_write(dut, 2, 0xDEAD'0001U), false);
    ok &= expect_eq("misaligned data write rejected", data_write(dut, 6, 0xDEAD'0002U), false);

    const auto bad_program = program_read(dut, 2);
    const auto bad_data = data_read(dut, 6);
    ok &= expect_eq("bad program read valid", bad_program.valid, true);
    ok &= expect_eq("bad program read error", bad_program.error, true);
    ok &= expect_eq("bad data read valid", bad_data.valid, true);
    ok &= expect_eq("bad data read error", bad_data.error, true);
    return ok;
}

bool test_data_port_arbitration(Vnpu_local_memory& dut) {
    reset(dut);

    dut.data_wr_valid_i = 1;
    dut.data_wr_addr_i = 8;
    dut.data_wr_data_i = 0xAAAA'0008U;
    dut.data_wr_strb_i = 0xFU;
    dut.client_data_wr_valid_i = 1;
    dut.client_data_wr_addr_i = 12;
    dut.client_data_wr_data_i = 0xBBBB'000CU;
    dut.client_data_wr_strb_i = 0xFU;
    eval(dut);

    bool ok = true;
    ok &= expect_eq("loader write wins contention", dut.data_wr_ready_o, 1);
    ok &= expect_eq("client write waits on contention", dut.client_data_wr_ready_o, 0);
    tick(dut);

    dut.data_wr_valid_i = 0;
    eval(dut);
    ok &= expect_eq("client write accepted after loader", dut.client_data_wr_ready_o, 1);
    tick(dut);
    dut.client_data_wr_valid_i = 0;
    eval(dut);

    const auto loader_word = data_read(dut, 8);
    const auto client_word = data_read(dut, 12);
    ok &= expect_eq("loader arbitration word", loader_word.data, 0xAAAA'0008U);
    ok &= expect_eq("client arbitration word", client_word.data, 0xBBBB'000CU);

    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_local_memory", argc, argv};

    Vnpu_local_memory dut;
    using enum holon_npu_tb::coverage_point;
    const bool storage_case = test_program_and_data_storage(dut);
    test.observe({local_memory_program_read, local_memory_data_read}, storage_case);
    bool ok = storage_case;

    const bool bounds_case = test_bounds_and_alignment_faults(dut);
    test.observe(local_memory_bounds_fault, bounds_case);
    ok &= bounds_case;

    const bool arbitration_case = test_data_port_arbitration(dut);
    test.observe(local_memory_data_arbiter, arbitration_case);
    ok &= arbitration_case;

    dut.final();
    return test.finish(ok);
}
