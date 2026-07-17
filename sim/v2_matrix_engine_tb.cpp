#include "Vnpu_v2_matrix_engine.h"

#include "holon_npu_isa.h"
#include "holon_npu_program.h"
#include "tb_coverage.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <verilated.h>

namespace {

constexpr std::uint32_t kLocalMemBytes = 4096;
constexpr std::uint32_t kCommandOffset = 160;
constexpr std::uint32_t kRandomCommandOffset = 3072;

void eval(Vnpu_v2_matrix_engine& dut) {
    dut.eval();
}

void tick(Vnpu_v2_matrix_engine& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_v2_matrix_engine& dut) {
    dut.soft_reset_i = 0;
    dut.local_mem_bytes_i = kLocalMemBytes;
    dut.issue_valid_i = 0;
    dut.issue_data_i[0] = 0;
    dut.issue_data_i[1] = 0;
    dut.issue_data_i[2] = 0;
    dut.issue_data_i[3] = 0;
    dut.event_ready_i = 1;
    dut.host_wr_valid_i = 0;
    dut.host_wr_addr_i = 0;
    dut.host_wr_data_i = 0;
    dut.host_wr_strb_i = 0;
    dut.host_rd_valid_i = 0;
    dut.host_rd_addr_i = 0;
}

void reset(Vnpu_v2_matrix_engine& dut) {
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

bool host_write(
    Vnpu_v2_matrix_engine& dut,
    std::uint32_t addr,
    std::uint32_t data,
    std::uint8_t strobe = 0xF
) {
    dut.host_wr_valid_i = 1;
    dut.host_wr_addr_i = addr;
    dut.host_wr_data_i = data;
    dut.host_wr_strb_i = strobe;
    bool accepted = false;
    for (int cycle = 0; cycle < 16; ++cycle) {
        eval(dut);
        if (dut.host_wr_ready_o) {
            accepted = true;
            tick(dut);
            break;
        }
        tick(dut);
    }
    dut.host_wr_valid_i = 0;
    tick(dut);
    return accepted;
}

bool host_write_byte(Vnpu_v2_matrix_engine& dut, std::uint32_t addr, std::uint8_t data) {
    const auto lane = addr & 3U;
    return host_write(
        dut,
        addr & ~std::uint32_t{3},
        static_cast<std::uint32_t>(data) << (lane * 8U),
        static_cast<std::uint8_t>(1U << lane)
    );
}

struct read_result {
    bool ready = false;
    bool valid = false;
    bool error = false;
    std::uint32_t data = 0;
};

read_result host_read(Vnpu_v2_matrix_engine& dut, std::uint32_t addr) {
    dut.host_rd_valid_i = 1;
    dut.host_rd_addr_i = addr;
    read_result result{};
    for (int cycle = 0; cycle < 16; ++cycle) {
        eval(dut);
        if (dut.host_rd_ready_o) {
            result.ready = true;
            tick(dut);
            break;
        }
        tick(dut);
    }
    dut.host_rd_valid_i = 0;
    for (int cycle = 0; cycle < 16; ++cycle) {
        eval(dut);
        if (dut.host_rd_resp_valid_o) {
            result.valid = true;
            result.error = dut.host_rd_resp_error_o != 0;
            result.data = dut.host_rd_resp_data_o;
            tick(dut);
            break;
        }
        tick(dut);
    }
    return result;
}

std::uint32_t matrix_instruction(std::uint8_t accumulator, std::uint16_t command_offset) {
    return HOLON_NPU_ISA_CLASS_MATRIX |
           (static_cast<std::uint32_t>(HOLON_NPU_ISA_OPCODE_MATRIX_GEMM)
            << HOLON_NPU_ISA_OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(accumulator) << HOLON_NPU_ISA_RD_SHIFT) |
           command_offset;
}

struct event_result {
    bool valid = false;
    bool fault = false;
    std::uint32_t fault_code = 0;
};

event_result issue(Vnpu_v2_matrix_engine& dut, std::uint32_t instruction) {
    dut.issue_valid_i = 1;
    dut.issue_data_i[0] = instruction;
    bool accepted = false;
    for (int cycle = 0; cycle < 16; ++cycle) {
        eval(dut);
        if (dut.issue_ready_o) {
            accepted = true;
            tick(dut);
            break;
        }
        tick(dut);
    }
    dut.issue_valid_i = 0;

    event_result event{};
    for (int cycle = 0; cycle < 4096; ++cycle) {
        eval(dut);
        if (dut.event_valid_o) {
            event.valid = accepted;
            event.fault = (dut.event_data_o & 1U) != 0;
            event.fault_code = static_cast<std::uint32_t>(dut.event_data_o >> 32U);
            tick(dut);
            break;
        }
        tick(dut);
    }
    return event;
}

bool write_command(Vnpu_v2_matrix_engine& dut, std::uint8_t flags, std::uint8_t m = 2) {
    const auto shape = static_cast<std::uint32_t>(m) |
        (std::uint32_t{2} << HOLON_NPU_ISA_MATRIX_SHAPE_N_SHIFT) |
        (std::uint32_t{2} << HOLON_NPU_ISA_MATRIX_SHAPE_K_SHIFT) |
        (static_cast<std::uint32_t>(flags) << HOLON_NPU_ISA_MATRIX_SHAPE_FLAGS_SHIFT);
    const std::array<std::uint32_t, 8> words{0, 32, 64, 2, 2, 8, shape, 0};
    bool ok = true;
    for (std::size_t index = 0; index < words.size(); ++index) {
        ok &= host_write(dut, kCommandOffset + static_cast<std::uint32_t>(index * 4), words[index]);
    }
    return ok;
}

bool write_command(
    Vnpu_v2_matrix_engine& dut,
    std::uint32_t command_offset,
    std::uint32_t a_base,
    std::uint32_t b_base,
    std::uint32_t c_base,
    std::uint32_t a_stride,
    std::uint32_t b_stride,
    std::uint32_t c_stride,
    std::uint8_t m,
    std::uint8_t n,
    std::uint8_t k,
    std::uint8_t flags
) {
    const auto shape = static_cast<std::uint32_t>(m) |
        (static_cast<std::uint32_t>(n) << HOLON_NPU_ISA_MATRIX_SHAPE_N_SHIFT) |
        (static_cast<std::uint32_t>(k) << HOLON_NPU_ISA_MATRIX_SHAPE_K_SHIFT) |
        (static_cast<std::uint32_t>(flags) << HOLON_NPU_ISA_MATRIX_SHAPE_FLAGS_SHIFT);
    const std::array words{a_base, b_base, c_base, a_stride, b_stride, c_stride, shape, 0U};
    bool ok = true;
    for (std::size_t index = 0; index < words.size(); ++index) {
        ok &= host_write(
            dut,
            command_offset + static_cast<std::uint32_t>(index * sizeof(std::uint32_t)),
            words[index]
        );
    }
    return ok;
}

bool expect_event_ok(std::string_view name, const event_result& event) {
    bool ok = true;
    ok &= expect_eq(std::string{name} + " valid", event.valid, true);
    ok &= expect_eq(std::string{name} + " fault", event.fault, false);
    ok &= expect_eq(std::string{name} + " code", event.fault_code, HOLON_NPU_V2_FAULT_NONE);
    return ok;
}

bool expect_c(Vnpu_v2_matrix_engine& dut, std::span<const std::int32_t> expected) {
    bool ok = true;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto value = host_read(dut, 64 + static_cast<std::uint32_t>(index * 4));
        ok &= expect_eq("C read ready", value.ready, true);
        ok &= expect_eq("C read valid", value.valid, true);
        ok &= expect_eq("C read error", value.error, false);
        ok &= expect_eq(
            "C[" + std::to_string(index) + "]",
            value.data,
            static_cast<std::uint32_t>(expected[index])
        );
    }
    return ok;
}

bool test_clear_accumulate_store(Vnpu_v2_matrix_engine& dut) {
    reset(dut);
    bool ok = true;
    ok &= host_write(dut, 0, 0x0403'0201U);
    ok &= host_write(dut, 32, 0x0807'0605U);
    ok &= write_command(
        dut,
        HOLON_NPU_ISA_MATRIX_FLAG_CLEAR | HOLON_NPU_ISA_MATRIX_FLAG_STORE
    );
    ok &= expect_event_ok("clear/store", issue(dut, matrix_instruction(0, kCommandOffset)));
    const std::array<std::int32_t, 4> first{19, 22, 43, 50};
    ok &= expect_c(dut, first);

    ok &= write_command(dut, HOLON_NPU_ISA_MATRIX_FLAG_CLEAR);
    ok &= expect_event_ok("clear/no-store", issue(dut, matrix_instruction(0, kCommandOffset)));
    ok &= expect_c(dut, first);

    ok &= write_command(
        dut,
        HOLON_NPU_ISA_MATRIX_FLAG_ACCUMULATE | HOLON_NPU_ISA_MATRIX_FLAG_STORE
    );
    ok &= expect_event_ok("accumulate/store", issue(dut, matrix_instruction(0, kCommandOffset)));
    const std::array<std::int32_t, 4> doubled{38, 44, 86, 100};
    ok &= expect_c(dut, doubled);
    return ok;
}

bool test_matrix_faults(Vnpu_v2_matrix_engine& dut) {
    reset(dut);
    bool ok = write_command(dut, HOLON_NPU_ISA_MATRIX_FLAG_CLEAR, 0);
    const auto bad_shape = issue(dut, matrix_instruction(0, kCommandOffset));
    ok &= expect_eq("bad shape valid", bad_shape.valid, true);
    ok &= expect_eq("bad shape fault", bad_shape.fault, true);
    ok &= expect_eq("bad shape code", bad_shape.fault_code, HOLON_NPU_V2_FAULT_MATRIX_ISSUE);

    const auto unaligned = issue(dut, matrix_instruction(0, kCommandOffset + 4));
    ok &= expect_eq("unaligned valid", unaligned.valid, true);
    ok &= expect_eq("unaligned fault", unaligned.fault, true);
    ok &= expect_eq("unaligned code", unaligned.fault_code, HOLON_NPU_V2_FAULT_MATRIX_ISSUE);
    return ok;
}

bool run_random_matrix_case(
    Vnpu_v2_matrix_engine& dut,
    std::mt19937_64& random,
    std::uint8_t m,
    std::uint8_t n,
    std::uint8_t k,
    std::uint32_t case_index
) {
    constexpr std::uint32_t a_base = 0;
    constexpr std::uint32_t b_base = 512;
    constexpr std::uint32_t c_base = 1024;
    std::uniform_int_distribution<int> padding_distribution(0, 3);
    std::uniform_int_distribution<int> operand_distribution(-128, 127);
    const auto a_stride = static_cast<std::uint32_t>(k) + padding_distribution(random);
    const auto b_stride = static_cast<std::uint32_t>(n) + padding_distribution(random);
    const auto c_stride = (static_cast<std::uint32_t>(n) + padding_distribution(random)) * 4U;
    std::vector<std::int8_t> a(static_cast<std::size_t>(m) * k);
    std::vector<std::int8_t> b(static_cast<std::size_t>(k) * n);
    std::vector<std::uint32_t> expected(static_cast<std::size_t>(m) * n, 0);

    reset(dut);
    bool ok = true;
    for (std::uint32_t row = 0; row < m; ++row) {
        for (std::uint32_t kk = 0; kk < k; ++kk) {
            const auto value = static_cast<std::int8_t>(operand_distribution(random));
            a[row * k + kk] = value;
            ok &= host_write_byte(
                dut,
                a_base + row * a_stride + kk,
                std::bit_cast<std::uint8_t>(value)
            );
        }
    }
    for (std::uint32_t kk = 0; kk < k; ++kk) {
        for (std::uint32_t col = 0; col < n; ++col) {
            const auto value = static_cast<std::int8_t>(operand_distribution(random));
            b[kk * n + col] = value;
            ok &= host_write_byte(
                dut,
                b_base + kk * b_stride + col,
                std::bit_cast<std::uint8_t>(value)
            );
        }
    }
    for (std::uint32_t row = 0; row < m; ++row) {
        for (std::uint32_t col = 0; col < n; ++col) {
            auto& sum = expected[row * n + col];
            for (std::uint32_t kk = 0; kk < k; ++kk) {
                const auto product = static_cast<std::int32_t>(a[row * k + kk]) *
                                     static_cast<std::int32_t>(b[kk * n + col]);
                sum += static_cast<std::uint32_t>(product);
            }
        }
    }

    ok &= write_command(
        dut,
        kRandomCommandOffset,
        a_base,
        b_base,
        c_base,
        a_stride,
        b_stride,
        c_stride,
        m,
        n,
        k,
        HOLON_NPU_ISA_MATRIX_FLAG_CLEAR | HOLON_NPU_ISA_MATRIX_FLAG_STORE
    );
    ok &= expect_event_ok(
        "random matrix event " + std::to_string(case_index),
        issue(dut, matrix_instruction(0, kRandomCommandOffset))
    );
    for (std::uint32_t row = 0; row < m; ++row) {
        for (std::uint32_t col = 0; col < n; ++col) {
            const auto actual = host_read(dut, c_base + row * c_stride + col * 4U);
            const auto context = "random matrix case " + std::to_string(case_index) +
                                 " [" + std::to_string(row) + "," + std::to_string(col) + "]";
            ok &= expect_eq(context + " ready", actual.ready, true);
            ok &= expect_eq(context + " valid", actual.valid, true);
            ok &= expect_eq(context + " error", actual.error, false);
            ok &= expect_eq(context, actual.data, expected[row * n + col]);
        }
    }
    return ok;
}

bool test_constrained_random(Vnpu_v2_matrix_engine& dut) {
    constexpr std::uint64_t seed = 0x484F'4C4F'4E4D'4154ULL;
    std::mt19937_64 random{seed};
    std::uniform_int_distribution<int> dimension_distribution(1, 16);
    bool ok = run_random_matrix_case(dut, random, 1, 1, 1, 0);
    ok &= run_random_matrix_case(dut, random, 16, 16, 16, 1);
    for (std::uint32_t case_index = 2; case_index < 18; ++case_index) {
        ok &= run_random_matrix_case(
            dut,
            random,
            static_cast<std::uint8_t>(dimension_distribution(random)),
            static_cast<std::uint8_t>(dimension_distribution(random)),
            static_cast<std::uint8_t>(dimension_distribution(random)),
            case_index
        );
    }
    if (!ok) {
        std::cerr << "matrix constrained-random seed: 0x" << std::hex << seed << std::dec << '\n';
    }
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_v2_matrix_engine", argc, argv};
    Vnpu_v2_matrix_engine dut;
    bool ok = test_clear_accumulate_store(dut);
    ok &= test_matrix_faults(dut);
    ok &= test_constrained_random(dut);
    dut.final();
    test.cover({
        holon_npu_tb::coverage_point::v2_matrix_engine_clear,
        holon_npu_tb::coverage_point::v2_matrix_engine_accumulate,
        holon_npu_tb::coverage_point::v2_matrix_engine_store,
        holon_npu_tb::coverage_point::v2_matrix_engine_fault,
        holon_npu_tb::coverage_point::v2_matrix_constrained_random,
        holon_npu_tb::coverage_point::isa_class_matrix,
        holon_npu_tb::coverage_point::v2_matrix_gemm_program,
    });
    return test.finish(ok);
}
