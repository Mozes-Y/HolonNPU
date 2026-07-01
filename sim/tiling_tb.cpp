#include "Vnpu_tiling_datapath_test_top.h"

#include <cstdint>
#include <iostream>
#include <string_view>

#include <verilated.h>

namespace {

enum ScheduleState : std::uint32_t {
    kIdle = 0,
    kLoad = 1,
    kCompute = 2,
    kStore = 3,
    kDone = 4,
};

void eval(Vnpu_tiling_datapath_test_top& dut) {
    dut.eval();
}

void tick(Vnpu_tiling_datapath_test_top& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_tiling_datapath_test_top& dut) {
    dut.m_remaining_i = 0;
    dut.n_remaining_i = 0;
    dut.k_remaining_i = 0;

    dut.start_i = 0;
    dut.compute_done_i = 0;
    dut.store_done_i = 0;

    dut.a_wr_valid_i = 0;
    dut.a_wr_bank_i = 0;
    dut.a_wr_addr_i = 0;
    dut.a_wr_data_i = 0;
    dut.a_rd_valid_i = 0;
    dut.a_rd_bank_i = 0;
    dut.a_rd_addr_i = 0;

    dut.b_wr_valid_i = 0;
    dut.b_wr_bank_i = 0;
    dut.b_wr_addr_i = 0;
    dut.b_wr_data_i = 0;
    dut.b_rd_valid_i = 0;
    dut.b_rd_bank_i = 0;
    dut.b_rd_addr_i = 0;

    dut.c_wr_valid_i = 0;
    dut.c_wr_bank_i = 0;
    dut.c_wr_addr_i = 0;
    dut.c_wr_data_i = 0;
    dut.c_rd_valid_i = 0;
    dut.c_rd_bank_i = 0;
    dut.c_rd_addr_i = 0;

    dut.compute_clear_i = 0;
    dut.compute_weight_load_i = 0;
    dut.compute_weight_k_i = 0;
    dut.compute_step_valid_i = 0;
    dut.compute_k_index_i = 0;
    dut.compute_pattern_i = 0;
    dut.compute_read_row_i = 0;
    dut.compute_read_col_i = 0;
}

void reset(Vnpu_tiling_datapath_test_top& dut) {
    dut.clk_i = 0;
    dut.rst_ni = 0;
    clear_inputs(dut);
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

bool test_masks(Vnpu_tiling_datapath_test_top& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("zero row mask", dut.row_mask_o, 0x0000U);
    ok &= expect_eq("zero col mask", dut.col_mask_o, 0x0000U);
    ok &= expect_eq("zero k mask", dut.k_mask_o, 0x0000U);

    dut.m_remaining_i = 16;
    dut.n_remaining_i = 16;
    dut.k_remaining_i = 16;
    eval(dut);
    ok &= expect_eq("full row mask", dut.row_mask_o, 0xFFFFU);
    ok &= expect_eq("full col mask", dut.col_mask_o, 0xFFFFU);
    ok &= expect_eq("full k mask", dut.k_mask_o, 0xFFFFU);

    dut.m_remaining_i = 17;
    dut.n_remaining_i = 19;
    dut.k_remaining_i = 23;
    eval(dut);
    ok &= expect_eq("clamped row mask for 17 remaining", dut.row_mask_o, 0xFFFFU);
    ok &= expect_eq("clamped col mask for 19 remaining", dut.col_mask_o, 0xFFFFU);
    ok &= expect_eq("clamped k mask for 23 remaining", dut.k_mask_o, 0xFFFFU);

    dut.m_remaining_i = 1;
    dut.n_remaining_i = 3;
    dut.k_remaining_i = 7;
    eval(dut);
    ok &= expect_eq("edge row mask for 17x19x23 tail", dut.row_mask_o, 0x0001U);
    ok &= expect_eq("edge col mask for 17x19x23 tail", dut.col_mask_o, 0x0007U);
    ok &= expect_eq("edge k mask for 17x19x23 tail", dut.k_mask_o, 0x007FU);

    return ok;
}

void write_a(Vnpu_tiling_datapath_test_top& dut, int bank, int addr, std::uint8_t data) {
    dut.a_wr_valid_i = 1;
    dut.a_wr_bank_i = static_cast<std::uint8_t>(bank);
    dut.a_wr_addr_i = static_cast<std::uint16_t>(addr);
    dut.a_wr_data_i = data;
    tick(dut);
    dut.a_wr_valid_i = 0;
    eval(dut);
}

void write_b(Vnpu_tiling_datapath_test_top& dut, int bank, int addr, std::uint8_t data) {
    dut.b_wr_valid_i = 1;
    dut.b_wr_bank_i = static_cast<std::uint8_t>(bank);
    dut.b_wr_addr_i = static_cast<std::uint16_t>(addr);
    dut.b_wr_data_i = data;
    tick(dut);
    dut.b_wr_valid_i = 0;
    eval(dut);
}

void write_c(Vnpu_tiling_datapath_test_top& dut, int bank, int addr, std::uint32_t data) {
    dut.c_wr_valid_i = 1;
    dut.c_wr_bank_i = static_cast<std::uint8_t>(bank);
    dut.c_wr_addr_i = static_cast<std::uint16_t>(addr);
    dut.c_wr_data_i = data;
    tick(dut);
    dut.c_wr_valid_i = 0;
    eval(dut);
}

bool test_buffers(Vnpu_tiling_datapath_test_top& dut) {
    reset(dut);

    bool ok = true;
    write_a(dut, 0, 10, 0xA5);
    write_b(dut, 1, 11, 0x5A);
    write_c(dut, 0, 12, 0x1234'5678U);

    dut.a_rd_valid_i = 1;
    dut.a_rd_bank_i = 0;
    dut.a_rd_addr_i = 10;
    eval(dut);
    ok &= expect_eq("a read valid", dut.a_rd_valid_o, 1U);
    ok &= expect_eq("a read data", dut.a_rd_data_o, 0xA5U);
    ok &= expect_eq("a read error", dut.a_rd_error_o, 0U);

    dut.b_rd_valid_i = 1;
    dut.b_rd_bank_i = 1;
    dut.b_rd_addr_i = 11;
    eval(dut);
    ok &= expect_eq("b read valid", dut.b_rd_valid_o, 1U);
    ok &= expect_eq("b read data", dut.b_rd_data_o, 0x5AU);
    ok &= expect_eq("b read error", dut.b_rd_error_o, 0U);

    dut.c_rd_valid_i = 1;
    dut.c_rd_bank_i = 0;
    dut.c_rd_addr_i = 12;
    eval(dut);
    ok &= expect_eq("c read valid", dut.c_rd_valid_o, 1U);
    ok &= expect_eq("c read data", dut.c_rd_data_o, 0x1234'5678U);
    ok &= expect_eq("c read error", dut.c_rd_error_o, 0U);

    dut.a_wr_valid_i = 1;
    dut.a_wr_bank_i = 2;
    dut.a_wr_addr_i = 0;
    eval(dut);
    ok &= expect_eq("a illegal bank ready", dut.a_wr_ready_o, 0U);
    ok &= expect_eq("a illegal bank error", dut.a_wr_error_o, 1U);

    dut.a_wr_bank_i = 0;
    dut.a_wr_addr_i = 256;
    eval(dut);
    ok &= expect_eq("a illegal addr ready", dut.a_wr_ready_o, 0U);
    ok &= expect_eq("a illegal addr error", dut.a_wr_error_o, 1U);

    return ok;
}

std::int8_t make_a(int row, int k, int pattern) {
    int value = 0;
    switch (pattern) {
        case 0:
            value = ((row + (2 * k)) % 7) - 3;
            break;
        case 1:
            value = (((3 * row) + (5 * k) + 11) % 127) - 63;
            break;
        default:
            value = (((7 * row) + (9 * k) + 5) % 255) - 127;
            break;
    }
    return static_cast<std::int8_t>(value);
}

std::int8_t make_b(int col, int k, int pattern) {
    int value = 0;
    switch (pattern) {
        case 0:
            value = (((2 * col) + k) % 5) - 2;
            break;
        case 1:
            value = (((4 * col) + (7 * k) + 3) % 127) - 63;
            break;
        default:
            value = (((11 * col) + (13 * k) + 17) % 255) - 127;
            break;
    }
    return static_cast<std::int8_t>(value);
}

std::uint32_t golden(int row, int col, int k_dim, int pattern) {
    std::uint32_t acc = 0;

    for (int k = 0; k < k_dim; ++k) {
        const auto a = static_cast<std::int32_t>(make_a(row, k, pattern));
        const auto b = static_cast<std::int32_t>(make_b(col, k, pattern));
        const auto product = static_cast<std::int32_t>(a * b);
        acc += static_cast<std::uint32_t>(product);
    }

    return acc;
}

std::uint32_t read_compute_bits(Vnpu_tiling_datapath_test_top& dut, int row, int col) {
    dut.compute_read_row_i = static_cast<std::uint8_t>(row);
    dut.compute_read_col_i = static_cast<std::uint8_t>(col);
    eval(dut);
    return static_cast<std::uint32_t>(dut.compute_read_data_o);
}

std::uint32_t read_compute_valid(Vnpu_tiling_datapath_test_top& dut, int row, int col) {
    dut.compute_read_row_i = static_cast<std::uint8_t>(row);
    dut.compute_read_col_i = static_cast<std::uint8_t>(col);
    eval(dut);
    return dut.compute_read_valid_o ? 1U : 0U;
}

bool test_masked_tail_compute(Vnpu_tiling_datapath_test_top& dut) {
    reset(dut);

    dut.m_remaining_i = 1;
    dut.n_remaining_i = 3;
    dut.k_remaining_i = 7;
    dut.compute_pattern_i = 2;

    dut.compute_clear_i = 1;
    tick(dut);
    dut.compute_clear_i = 0;

    for (int k = 0; k < 7; ++k) {
        dut.compute_weight_load_i = 1;
        dut.compute_weight_k_i = static_cast<std::uint8_t>(k);
        tick(dut);
    }
    dut.compute_weight_load_i = 0;

    dut.compute_step_valid_i = 1;

    for (int cycle = 0; cycle < 20; ++cycle) {
        dut.compute_k_index_i = static_cast<std::uint8_t>(cycle);
        tick(dut);
    }

    dut.compute_step_valid_i = 0;
    dut.compute_k_index_i = 0;
    tick(dut);
    eval(dut);

    bool ok = true;
    for (int row = 0; row < 16; ++row) {
        for (int col = 0; col < 16; ++col) {
            const bool active = (row < 1) && (col < 3);
            const auto expected_valid = active ? 1U : 0U;
            const auto expected_data = active ? golden(row, col, 7, 2) : 0U;

            ok &= expect_eq("tail compute valid", read_compute_valid(dut, row, col), expected_valid);
            ok &= expect_eq("tail compute data", read_compute_bits(dut, row, col), expected_data);
        }
    }

    return ok;
}

bool test_schedule(Vnpu_tiling_datapath_test_top& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("schedule reset state", dut.schedule_state_o, kIdle);
    ok &= expect_eq("schedule reset bank", dut.load_bank_o, 0U);

    dut.start_i = 1;
    tick(dut);
    dut.start_i = 0;
    ok &= expect_eq("schedule load state", dut.schedule_state_o, kLoad);
    ok &= expect_eq("schedule load valid", dut.load_valid_o, 1U);
    ok &= expect_eq("schedule first load bank", dut.load_bank_o, 0U);

    tick(dut);
    ok &= expect_eq("schedule compute state", dut.schedule_state_o, kCompute);
    ok &= expect_eq("schedule compute valid", dut.compute_valid_o, 1U);
    ok &= expect_eq("schedule first compute bank", dut.compute_bank_o, 0U);

    tick(dut);
    ok &= expect_eq("schedule waits in compute", dut.schedule_state_o, kCompute);

    dut.compute_done_i = 1;
    tick(dut);
    dut.compute_done_i = 0;
    ok &= expect_eq("schedule store state", dut.schedule_state_o, kStore);
    ok &= expect_eq("schedule store valid", dut.store_valid_o, 1U);
    ok &= expect_eq("schedule first store bank", dut.store_bank_o, 0U);

    dut.store_done_i = 1;
    tick(dut);
    dut.store_done_i = 0;
    ok &= expect_eq("schedule done state", dut.schedule_state_o, kDone);
    ok &= expect_eq("schedule done pulse", dut.done_o, 1U);
    ok &= expect_eq("schedule bank toggled", dut.load_bank_o, 1U);

    tick(dut);
    ok &= expect_eq("schedule returns idle", dut.schedule_state_o, kIdle);

    dut.start_i = 1;
    tick(dut);
    dut.start_i = 0;
    ok &= expect_eq("schedule second load state", dut.schedule_state_o, kLoad);
    ok &= expect_eq("schedule second load bank", dut.load_bank_o, 1U);

    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Vnpu_tiling_datapath_test_top dut;
    bool ok = true;

    ok &= test_masks(dut);
    ok &= test_buffers(dut);
    ok &= test_masked_tail_compute(dut);
    ok &= test_schedule(dut);

    dut.final();
    return ok ? 0 : 1;
}
