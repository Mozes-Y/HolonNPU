#include "Vnpu_systolic_array_test_top.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include <verilated.h>

namespace {

constexpr int kMaxM = 17;
constexpr int kMaxN = 19;

void eval(Vnpu_systolic_array_test_top& dut) {
    dut.eval();
}

void tick(Vnpu_systolic_array_test_top& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void reset(Vnpu_systolic_array_test_top& dut) {
    dut.clk_i = 0;
    dut.rst_ni = 0;
    dut.clear_i = 0;
    dut.step_valid_i = 0;
    dut.active_m_i = 0;
    dut.active_n_i = 0;
    dut.active_k_i = 0;
    dut.k_index_i = 0;
    dut.pattern_i = 0;
    dut.read_row_i = 0;
    dut.read_col_i = 0;
    tick(dut);
    dut.rst_ni = 1;
    eval(dut);
}

void clear_array(Vnpu_systolic_array_test_top& dut) {
    dut.clear_i = 1;
    dut.step_valid_i = 0;
    tick(dut);
    dut.clear_i = 0;
    eval(dut);
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

std::uint32_t read_data_bits(Vnpu_systolic_array_test_top& dut, int row, int col) {
    dut.read_row_i = static_cast<std::uint8_t>(row);
    dut.read_col_i = static_cast<std::uint8_t>(col);
    eval(dut);
    return static_cast<std::uint32_t>(dut.read_data_o);
}

bool read_valid(Vnpu_systolic_array_test_top& dut, int row, int col) {
    dut.read_row_i = static_cast<std::uint8_t>(row);
    dut.read_col_i = static_cast<std::uint8_t>(col);
    eval(dut);
    return dut.read_valid_o != 0;
}

bool expect_eq(std::string_view name, std::uint32_t actual, std::uint32_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

bool run_case(
    Vnpu_systolic_array_test_top& dut,
    int m_dim,
    int n_dim,
    int k_dim,
    int pattern,
    std::string_view name
) {
    reset(dut);
    clear_array(dut);

    dut.active_m_i = static_cast<std::uint8_t>(m_dim);
    dut.active_n_i = static_cast<std::uint8_t>(n_dim);
    dut.active_k_i = static_cast<std::uint8_t>(k_dim);
    dut.pattern_i = static_cast<std::uint8_t>(pattern);
    dut.step_valid_i = 1;

    const int systolic_cycles = k_dim + m_dim + n_dim - 1;
    for (int cycle = 0; cycle < systolic_cycles; ++cycle) {
        dut.k_index_i = static_cast<std::uint8_t>(cycle);
        tick(dut);
    }

    dut.step_valid_i = 0;
    eval(dut);

    bool ok = true;
    for (int row = 0; row < kMaxM; ++row) {
        for (int col = 0; col < kMaxN; ++col) {
            const bool active = (row < m_dim) && (col < n_dim);
            const auto expected_valid = active ? 1U : 0U;
            const auto expected_data = active ? golden(row, col, k_dim, pattern) : 0U;

            const auto actual_valid = read_valid(dut, row, col) ? 1U : 0U;
            const auto actual_data = read_data_bits(dut, row, col);

            ok &= expect_eq(std::string{"array valid "} + std::string{name}, actual_valid, expected_valid);
            ok &= expect_eq(std::string{"array data "} + std::string{name}, actual_data, expected_data);
        }
    }

    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Vnpu_systolic_array_test_top dut;
    bool ok = true;

    ok &= run_case(dut, 1, 1, 1, 0, "1x1x1");
    ok &= run_case(dut, 16, 16, 16, 1, "16x16x16");
    ok &= run_case(dut, 17, 19, 23, 2, "17x19x23");

    dut.final();
    return ok ? 0 : 1;
}
