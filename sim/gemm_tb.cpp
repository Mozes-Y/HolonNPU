#include "Vnpu_gemm_accelerator.h"

#include "gemm_case_gen.hpp"
#include "tb_coverage.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <verilated.h>

namespace {

constexpr std::uint32_t kRespOkay = 0;
constexpr std::uint32_t kRespSlvErr = 2;
constexpr std::uint32_t kErrAxiRead = 6;
constexpr std::uint32_t kErrAxiWrite = 7;
constexpr std::uint32_t kStageLoadA = 1;
constexpr std::uint32_t kStageLoadB = 2;
constexpr std::uint32_t kStageCompute = 3;
constexpr std::uint32_t kStageStore = 4;
constexpr std::uint32_t kStageDone = 5;

struct Beat {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
};

struct Burst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
    std::uint32_t index = 0;
};

struct GemmCase {
    int m = 0;
    int n = 0;
    int k = 0;
    std::uint32_t seed = 0;
    std::string name;
};

GemmCase to_gemm_case(const holon_npu_tb::GeneratedGemmCase& test_case) {
    return GemmCase{
        test_case.m,
        test_case.n,
        test_case.k,
        test_case.seed,
        test_case.name,
    };
}

void eval(Vnpu_gemm_accelerator& dut) {
    dut.eval();
}

void tick(Vnpu_gemm_accelerator& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) {
    return ((value + alignment - 1U) / alignment) * alignment;
}

bool expect_eq(std::string_view name, std::uint64_t actual, std::uint64_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

void clear_inputs(Vnpu_gemm_accelerator& dut) {
    dut.command_valid_i = 0;
    dut.soft_reset_i = 0;
    dut.clear_perf_i = 0;
    dut.command_m_i = 0;
    dut.command_n_i = 0;
    dut.command_k_i = 0;
    dut.command_a_addr_i = 0;
    dut.command_b_addr_i = 0;
    dut.command_c_addr_i = 0;
    dut.command_a_stride_i = 0;
    dut.command_b_stride_i = 0;
    dut.command_c_stride_i = 0;
    dut.command_irq_on_done_i = 0;
    dut.command_irq_on_error_i = 0;
    dut.command_clear_perf_on_start_i = 0;
    dut.m_axi_arready_i = 0;
    dut.m_axi_rdata_lo_i = 0;
    dut.m_axi_rdata_hi_i = 0;
    dut.m_axi_rresp_i = kRespOkay;
    dut.m_axi_rlast_i = 0;
    dut.m_axi_rvalid_i = 0;
    dut.m_axi_awready_i = 0;
    dut.m_axi_wready_i = 0;
    dut.m_axi_bresp_i = kRespOkay;
    dut.m_axi_bvalid_i = 0;
}

void reset(Vnpu_gemm_accelerator& dut) {
    dut.clk_i = 0;
    dut.rst_ni = 0;
    clear_inputs(dut);
    tick(dut);
    dut.rst_ni = 1;
    eval(dut);
}

class AxiMemory {
public:
    explicit AxiMemory(std::size_t size) : memory_(size) {}

    void set_read_error_on_first_beat(bool enabled) {
        read_error_on_first_beat_ = enabled;
    }

    void set_write_error_response(bool enabled) {
        write_error_response_ = enabled;
    }

    void store_u8(std::uint64_t addr, std::uint8_t value) {
        memory_.at(addr) = value;
    }

    std::uint8_t load_u8(std::uint64_t addr) const {
        return memory_.at(addr);
    }

    std::uint32_t load_u32(std::uint64_t addr) const {
        std::uint32_t value = 0;
        for (int byte = 0; byte < 4; ++byte) {
            value |= static_cast<std::uint32_t>(memory_.at(addr + byte)) << (byte * 8);
        }
        return value;
    }

    void step(Vnpu_gemm_accelerator& dut) {
        dut.m_axi_arready_i = 1;
        dut.m_axi_awready_i = 1;
        dut.m_axi_wready_i = 1;

        if (!read_active_ && !read_pending_.empty()) {
            read_active_ = true;
            read_burst_ = read_pending_.front();
            read_pending_.pop_front();
        }

        if (read_active_) {
            const bool last = (read_burst_.index + 1U) == read_burst_.beats;
            const auto beat = load_beat(read_burst_.addr + (static_cast<std::uint64_t>(read_burst_.index) * 16U));
            dut.m_axi_rvalid_i = 1;
            dut.m_axi_rdata_lo_i = beat.lo;
            dut.m_axi_rdata_hi_i = beat.hi;
            dut.m_axi_rlast_i = last ? 1 : 0;
            dut.m_axi_rresp_i = (read_error_on_first_beat_ && !read_error_sent_) ? kRespSlvErr : kRespOkay;
        } else {
            dut.m_axi_rvalid_i = 0;
            dut.m_axi_rdata_lo_i = 0;
            dut.m_axi_rdata_hi_i = 0;
            dut.m_axi_rlast_i = 0;
            dut.m_axi_rresp_i = kRespOkay;
        }

        if (write_response_pending_) {
            dut.m_axi_bvalid_i = 1;
            dut.m_axi_bresp_i = write_error_response_ ? kRespSlvErr : kRespOkay;
        } else {
            dut.m_axi_bvalid_i = 0;
            dut.m_axi_bresp_i = kRespOkay;
        }

        eval(dut);

        const bool ar_fire = dut.m_axi_arvalid_o && dut.m_axi_arready_i;
        const bool r_fire = dut.m_axi_rvalid_i && dut.m_axi_rready_o;
        const bool aw_fire = dut.m_axi_awvalid_o && dut.m_axi_awready_i;
        const bool w_fire = dut.m_axi_wvalid_o && dut.m_axi_wready_i;
        const bool b_fire = dut.m_axi_bvalid_i && dut.m_axi_bready_o;

        const auto ar_addr = static_cast<std::uint64_t>(dut.m_axi_araddr_o);
        const auto ar_beats = static_cast<std::uint32_t>(dut.m_axi_arlen_o) + 1U;
        const auto aw_addr = static_cast<std::uint64_t>(dut.m_axi_awaddr_o);
        const auto aw_beats = static_cast<std::uint32_t>(dut.m_axi_awlen_o) + 1U;
        const auto w_beat = Beat{
            static_cast<std::uint64_t>(dut.m_axi_wdata_lo_o),
            static_cast<std::uint64_t>(dut.m_axi_wdata_hi_o),
        };
        const bool w_last = dut.m_axi_wlast_o != 0;

        tick(dut);

        if (ar_fire) {
            read_pending_.push_back(Burst{ar_addr, ar_beats, 0});
        }

        if (r_fire && read_active_) {
            if (read_error_on_first_beat_ && !read_error_sent_) {
                read_error_sent_ = true;
                read_active_ = false;
            } else {
                read_burst_.index += 1U;
                if (read_burst_.index == read_burst_.beats) {
                    read_active_ = false;
                }
            }
        }

        if (aw_fire) {
            write_active_ = true;
            write_burst_ = Burst{aw_addr, aw_beats, 0};
        }

        if (w_fire && write_active_) {
            store_beat(write_burst_.addr + (static_cast<std::uint64_t>(write_burst_.index) * 16U), w_beat);
            write_burst_.index += 1U;
            if (w_last) {
                write_active_ = false;
                write_response_pending_ = true;
            }
        }

        if (b_fire) {
            write_response_pending_ = false;
        }
    }

private:
    Beat load_beat(std::uint64_t addr) const {
        Beat beat;
        for (int byte = 0; byte < 8; ++byte) {
            beat.lo |= static_cast<std::uint64_t>(memory_.at(addr + byte)) << (byte * 8);
            beat.hi |= static_cast<std::uint64_t>(memory_.at(addr + 8 + byte)) << (byte * 8);
        }
        return beat;
    }

    void store_beat(std::uint64_t addr, Beat beat) {
        for (int byte = 0; byte < 8; ++byte) {
            memory_.at(addr + byte) = static_cast<std::uint8_t>((beat.lo >> (byte * 8)) & 0xFFU);
            memory_.at(addr + 8 + byte) = static_cast<std::uint8_t>((beat.hi >> (byte * 8)) & 0xFFU);
        }
    }

    std::vector<std::uint8_t> memory_;
    std::deque<Burst> read_pending_;
    bool read_active_ = false;
    bool write_active_ = false;
    bool write_response_pending_ = false;
    bool read_error_on_first_beat_ = false;
    bool read_error_sent_ = false;
    bool write_error_response_ = false;
    Burst read_burst_;
    Burst write_burst_;
};

std::int8_t pattern_value(std::uint32_t seed, int row, int col, bool is_b) {
    std::mt19937 rng(seed ^ (is_b ? 0xB01DF00DU : 0xA11CE5EEDU));
    rng.discard(static_cast<unsigned long long>((row * 257) + (col * 17) + (is_b ? 11 : 3)));
    return static_cast<std::int8_t>(static_cast<int>(rng() % 255U) - 127);
}

std::uint32_t golden(
    const std::vector<std::int8_t>& a,
    const std::vector<std::int8_t>& b,
    int m,
    int n,
    int k,
    int row,
    int col
) {
    (void)m;
    std::uint32_t acc = 0;

    for (int kk = 0; kk < k; ++kk) {
        const auto av = static_cast<std::int32_t>(a.at((row * k) + kk));
        const auto bv = static_cast<std::int32_t>(b.at((kk * n) + col));
        acc += static_cast<std::uint32_t>(av * bv);
    }

    return acc;
}

void start_command(
    Vnpu_gemm_accelerator& dut,
    std::uint32_t m,
    std::uint32_t n,
    std::uint32_t k,
    std::uint64_t a_addr,
    std::uint64_t b_addr,
    std::uint64_t c_addr,
    std::uint32_t a_stride,
    std::uint32_t b_stride,
    std::uint32_t c_stride
) {
    dut.command_m_i = m;
    dut.command_n_i = n;
    dut.command_k_i = k;
    dut.command_a_addr_i = a_addr;
    dut.command_b_addr_i = b_addr;
    dut.command_c_addr_i = c_addr;
    dut.command_a_stride_i = a_stride;
    dut.command_b_stride_i = b_stride;
    dut.command_c_stride_i = c_stride;
    dut.command_irq_on_done_i = 1;
    dut.command_irq_on_error_i = 1;
    dut.command_clear_perf_on_start_i = 1;
    dut.command_valid_i = 1;
    tick(dut);
    dut.command_valid_i = 0;
    eval(dut);
}

bool run_case(Vnpu_gemm_accelerator& dut, const GemmCase& test_case) {
    reset(dut);

    constexpr std::uint64_t kBaseA = 0x1000;
    constexpr std::uint64_t kBaseB = 0x20000;
    constexpr std::uint64_t kBaseC = 0x50000;
    AxiMemory memory(1024 * 1024);

    const auto a_stride = align_up(static_cast<std::uint32_t>(test_case.k), 16);
    const auto b_stride = align_up(static_cast<std::uint32_t>(test_case.n), 16);
    const auto c_stride = align_up(static_cast<std::uint32_t>(test_case.n * 4), 16);

    std::vector<std::int8_t> a(static_cast<std::size_t>(test_case.m * test_case.k));
    std::vector<std::int8_t> b(static_cast<std::size_t>(test_case.k * test_case.n));

    for (int row = 0; row < test_case.m; ++row) {
        for (int col = 0; col < test_case.k; ++col) {
            const auto value = pattern_value(test_case.seed, row, col, false);
            a.at((row * test_case.k) + col) = value;
            memory.store_u8(kBaseA + (static_cast<std::uint64_t>(row) * a_stride) + col,
                            static_cast<std::uint8_t>(value));
        }
    }

    for (int row = 0; row < test_case.k; ++row) {
        for (int col = 0; col < test_case.n; ++col) {
            const auto value = pattern_value(test_case.seed, row, col, true);
            b.at((row * test_case.n) + col) = value;
            memory.store_u8(kBaseB + (static_cast<std::uint64_t>(row) * b_stride) + col,
                            static_cast<std::uint8_t>(value));
        }
    }

    for (int row = 0; row < test_case.m; ++row) {
        for (std::uint32_t byte = 0; byte < c_stride; ++byte) {
            memory.store_u8(kBaseC + (static_cast<std::uint64_t>(row) * c_stride) + byte, 0xA5);
        }
    }

    start_command(
        dut,
        static_cast<std::uint32_t>(test_case.m),
        static_cast<std::uint32_t>(test_case.n),
        static_cast<std::uint32_t>(test_case.k),
        kBaseA,
        kBaseB,
        kBaseC,
        a_stride,
        b_stride,
        c_stride
    );

    std::array<bool, 7> seen_stage{};
    bool completed = false;

    for (int cycle = 0; cycle < 250000; ++cycle) {
        if (dut.stage_o < seen_stage.size()) {
            seen_stage.at(dut.stage_o) = true;
        }

        if (dut.done_o || dut.error_o) {
            completed = true;
            break;
        }

        memory.step(dut);
    }

    bool ok = true;
    ok &= expect_eq(test_case.name + " completed", completed ? 1U : 0U, 1U);
    ok &= expect_eq(test_case.name + " error", dut.error_o, 0U);
    ok &= expect_eq(test_case.name + " done", dut.done_o, 1U);
    ok &= expect_eq(test_case.name + " irq", dut.irq_o, 1U);
    ok &= expect_eq(test_case.name + " perf desc", dut.perf_desc_count_o, 1U);
    ok &= expect_eq(test_case.name + " perf errors", dut.perf_error_count_o, 0U);
    ok &= expect_eq(test_case.name + " saw load A", seen_stage.at(kStageLoadA) ? 1U : 0U, 1U);
    ok &= expect_eq(test_case.name + " saw load B", seen_stage.at(kStageLoadB) ? 1U : 0U, 1U);
    ok &= expect_eq(test_case.name + " saw compute", seen_stage.at(kStageCompute) ? 1U : 0U, 1U);
    ok &= expect_eq(test_case.name + " saw store", seen_stage.at(kStageStore) ? 1U : 0U, 1U);
    ok &= expect_eq(test_case.name + " saw done", seen_stage.at(kStageDone) ? 1U : 0U, 1U);
    ok &= expect_eq(test_case.name + " perf cycles nonzero", dut.perf_cycles_o != 0 ? 1U : 0U, 1U);
    ok &= expect_eq(test_case.name + " perf busy nonzero", dut.perf_busy_cycles_o != 0 ? 1U : 0U, 1U);

    for (int row = 0; row < test_case.m; ++row) {
        for (int col = 0; col < test_case.n; ++col) {
            const auto actual = memory.load_u32(kBaseC + (static_cast<std::uint64_t>(row) * c_stride) +
                                                (static_cast<std::uint64_t>(col) * 4U));
            const auto expected = golden(a, b, test_case.m, test_case.n, test_case.k, row, col);
            if (actual != expected) {
                std::cerr << test_case.name << " C[" << row << "][" << col << "]: expected 0x"
                          << std::hex << expected << ", got 0x" << actual << std::dec << '\n';
                ok = false;
            }
        }
    }

    return ok;
}

bool run_until_terminal(Vnpu_gemm_accelerator& dut, AxiMemory& memory, int max_cycles) {
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        if (dut.done_o || dut.error_o) {
            return true;
        }
        memory.step(dut);
    }

    std::cerr << "GEMM hardening run timed out\n";
    return false;
}

void start_zero_gemm(Vnpu_gemm_accelerator& dut) {
    start_command(
        dut,
        1,
        1,
        1,
        0x1000,
        0x2000,
        0x3000,
        16,
        16,
        16
    );
}

bool test_reset_in_flight(Vnpu_gemm_accelerator& dut) {
    reset(dut);
    AxiMemory memory(65536);
    start_command(dut, 16, 16, 16, 0x1000, 0x2000, 0x3000, 16, 16, 64);

    for (int cycle = 0; cycle < 32; ++cycle) {
        memory.step(dut);
    }

    dut.rst_ni = 0;
    tick(dut);
    dut.rst_ni = 1;
    eval(dut);

    bool ok = true;
    ok &= expect_eq("reset in flight busy", dut.busy_o, 0U);
    ok &= expect_eq("reset in flight done", dut.done_o, 0U);
    ok &= expect_eq("reset in flight error", dut.error_o, 0U);
    ok &= expect_eq("reset in flight ready", dut.command_ready_o, 1U);
    ok &= expect_eq("reset in flight stage", dut.stage_o, 0U);

    AxiMemory recovery_memory(65536);
    start_zero_gemm(dut);
    ok &= expect_eq("reset recovery terminal", run_until_terminal(dut, recovery_memory, 5000) ? 1U : 0U, 1U);
    ok &= expect_eq("reset recovery done", dut.done_o, 1U);
    ok &= expect_eq("reset recovery error", dut.error_o, 0U);
    return ok;
}

bool test_axi_read_error(Vnpu_gemm_accelerator& dut) {
    reset(dut);
    AxiMemory memory(65536);
    memory.set_read_error_on_first_beat(true);
    start_zero_gemm(dut);

    bool ok = true;
    ok &= expect_eq("read error terminal", run_until_terminal(dut, memory, 5000) ? 1U : 0U, 1U);
    ok &= expect_eq("read error state", dut.error_o, 1U);
    ok &= expect_eq("read error code", dut.error_code_o, kErrAxiRead);
    ok &= expect_eq("read error irq", dut.irq_o, 1U);
    ok &= expect_eq("read error perf", dut.perf_error_count_o, 1U);
    return ok;
}

bool test_axi_write_error(Vnpu_gemm_accelerator& dut) {
    reset(dut);
    AxiMemory memory(65536);
    memory.set_write_error_response(true);
    start_zero_gemm(dut);

    bool ok = true;
    ok &= expect_eq("write error terminal", run_until_terminal(dut, memory, 5000) ? 1U : 0U, 1U);
    ok &= expect_eq("write error state", dut.error_o, 1U);
    ok &= expect_eq("write error code", dut.error_code_o, kErrAxiWrite);
    ok &= expect_eq("write error irq", dut.irq_o, 1U);
    ok &= expect_eq("write error perf", dut.perf_error_count_o, 1U);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_gemm", argc, argv};

    Vnpu_gemm_accelerator dut;
    bool ok = true;

    for (const auto& test_case : holon_npu_tb::fixed_shape_anchors("")) {
        ok &= run_case(dut, to_gemm_case(test_case));
    }
    for (const auto& test_case : holon_npu_tb::constrained_random_gemm_cases(0x1A2B, 64, "gemm-")) {
        ok &= run_case(dut, to_gemm_case(test_case));
    }
    ok &= test_reset_in_flight(dut);
    ok &= test_axi_read_error(dut);
    ok &= test_axi_write_error(dut);

    dut.final();
    using enum holon_npu_tb::coverage_point;
    test.cover({gemm_shape_1, gemm_shape_lt16, gemm_shape_16, gemm_shape_16_tail,
                gemm_shape_multi_tile, gemm_tail_m, gemm_tail_n, gemm_tail_k,
                gemm_tail_mixed, gemm_shape_64, gemm_reset_in_flight,
                gemm_axi_read_error, gemm_axi_write_error});
    return test.finish(ok);
}
