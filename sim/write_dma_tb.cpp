#include "Vnpu_write_dma_test_top.h"

#include "tb_coverage.hpp"

#include <cstdint>
#include <deque>
#include <iostream>
#include <string_view>
#include <vector>

#include <verilated.h>

namespace {

constexpr std::uint32_t kErrUnsupportedAlignment = 5;
constexpr std::uint32_t kErrAxiWrite = 7;
constexpr std::uint32_t kRespOkay = 0;
constexpr std::uint32_t kRespSlvErr = 2;

struct Beat {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
};

struct Burst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
    std::uint32_t index = 0;
};

struct ObservedBurst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
};

void eval(Vnpu_write_dma_test_top& dut) {
    dut.eval();
}

void tick(Vnpu_write_dma_test_top& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_write_dma_test_top& dut) {
    dut.start_i = 0;
    dut.addr_i = 0;
    dut.bytes_i = 0;
    dut.m_axi_awready_i = 0;
    dut.m_axi_wready_i = 0;
    dut.m_axi_bresp_i = kRespOkay;
    dut.m_axi_bvalid_i = 0;
    dut.in_valid_i = 0;
    dut.in_data_lo_i = 0;
    dut.in_data_hi_i = 0;
}

void reset(Vnpu_write_dma_test_top& dut) {
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

Beat make_source_beat(std::uint32_t index) {
    return Beat{
        0x1000'0000'0000'0000ULL + index,
        0x2000'0000'0000'0000ULL + index,
    };
}

class WriteMemoryModel {
public:
    explicit WriteMemoryModel(std::size_t size) : memory_(size) {}

    void set_error_response(bool enabled) {
        error_response_ = enabled;
    }

    const std::vector<ObservedBurst>& observed_bursts() const {
        return observed_bursts_;
    }

    Beat load_beat(std::uint64_t addr) const {
        Beat beat;
        for (int i = 0; i < 8; ++i) {
            beat.lo |= static_cast<std::uint64_t>(memory_.at(addr + i)) << (i * 8);
            beat.hi |= static_cast<std::uint64_t>(memory_.at(addr + 8 + i)) << (i * 8);
        }
        return beat;
    }

    void step(Vnpu_write_dma_test_top& dut, const std::vector<Beat>& source, std::uint32_t& source_index) {
        dut.m_axi_awready_i = 1;
        dut.m_axi_wready_i = 1;

        if (source_index < source.size()) {
            dut.in_valid_i = 1;
            dut.in_data_lo_i = source.at(source_index).lo;
            dut.in_data_hi_i = source.at(source_index).hi;
        } else {
            dut.in_valid_i = 0;
            dut.in_data_lo_i = 0;
            dut.in_data_hi_i = 0;
        }

        if (b_pending_) {
            dut.m_axi_bvalid_i = 1;
            dut.m_axi_bresp_i = error_response_ ? kRespSlvErr : kRespOkay;
        } else {
            dut.m_axi_bvalid_i = 0;
            dut.m_axi_bresp_i = kRespOkay;
        }

        eval(dut);
        const bool aw_fire = dut.m_axi_awvalid_o && dut.m_axi_awready_i;
        const bool w_fire = dut.m_axi_wvalid_o && dut.m_axi_wready_i;
        const bool b_fire = dut.m_axi_bvalid_i && dut.m_axi_bready_o;

        const auto aw_addr = dut.m_axi_awaddr_o;
        const auto aw_beats = static_cast<std::uint32_t>(dut.m_axi_awlen_o) + 1U;
        const auto w_lo = dut.m_axi_wdata_lo_o;
        const auto w_hi = dut.m_axi_wdata_hi_o;
        const bool w_last = dut.m_axi_wlast_o != 0;

        tick(dut);

        if (aw_fire) {
            active_ = true;
            active_burst_ = Burst{aw_addr, aw_beats, 0};
            observed_bursts_.push_back(ObservedBurst{aw_addr, aw_beats});
        }

        if (w_fire && active_) {
            store_beat(active_burst_.addr + (static_cast<std::uint64_t>(active_burst_.index) * 16U), Beat{w_lo, w_hi});
            source_index += 1U;
            active_burst_.index += 1U;
            if (w_last) {
                b_pending_ = true;
                active_ = false;
            }
        }

        if (b_fire) {
            b_pending_ = false;
        }
    }

private:
    void store_beat(std::uint64_t addr, Beat beat) {
        for (int i = 0; i < 8; ++i) {
            memory_.at(addr + i) = static_cast<std::uint8_t>((beat.lo >> (i * 8)) & 0xFFU);
            memory_.at(addr + 8 + i) = static_cast<std::uint8_t>((beat.hi >> (i * 8)) & 0xFFU);
        }
    }

    std::vector<std::uint8_t> memory_;
    std::vector<ObservedBurst> observed_bursts_;
    bool active_ = false;
    bool b_pending_ = false;
    bool error_response_ = false;
    Burst active_burst_;
};

void start_write(Vnpu_write_dma_test_top& dut, std::uint64_t addr, std::uint32_t bytes) {
    dut.addr_i = addr;
    dut.bytes_i = bytes;
    dut.start_i = 1;
    tick(dut);
    dut.start_i = 0;
    eval(dut);
}

bool run_write(
    Vnpu_write_dma_test_top& dut,
    WriteMemoryModel& memory,
    std::uint64_t addr,
    std::uint32_t bytes,
    const std::vector<Beat>& source
) {
    std::uint32_t source_index = 0;
    start_write(dut, addr, bytes);

    for (int cycle = 0; cycle < 512; ++cycle) {
        if (dut.done_o || dut.error_o) {
            return true;
        }
        memory.step(dut, source, source_index);
    }

    std::cerr << "write DMA timed out\n";
    return false;
}

std::vector<Beat> make_source(std::uint32_t beats) {
    std::vector<Beat> source;
    for (std::uint32_t i = 0; i < beats; ++i) {
        source.push_back(make_source_beat(i));
    }
    return source;
}

bool test_single_burst(Vnpu_write_dma_test_top& dut) {
    reset(dut);
    WriteMemoryModel memory(4096);
    const auto source = make_source(4);

    bool ok = run_write(dut, memory, 0x180, 64, source);
    ok &= expect_eq("single write done", dut.done_o, 1U);
    ok &= expect_eq("single write error", dut.error_o, 0U);
    ok &= expect_eq("single write burst count", memory.observed_bursts().size(), 1U);
    ok &= expect_eq("single write burst addr", memory.observed_bursts().at(0).addr, 0x180U);
    ok &= expect_eq("single write burst beats", memory.observed_bursts().at(0).beats, 4U);

    for (std::uint32_t i = 0; i < source.size(); ++i) {
        const auto actual = memory.load_beat(0x180 + (i * 16U));
        ok &= expect_eq("single write lo", actual.lo, source.at(i).lo);
        ok &= expect_eq("single write hi", actual.hi, source.at(i).hi);
    }

    return ok;
}

bool test_multi_burst_cross_tile(Vnpu_write_dma_test_top& dut) {
    reset(dut);
    WriteMemoryModel memory(8192);
    const auto source = make_source(20);

    bool ok = run_write(dut, memory, 0x1F0, 20U * 16U, source);
    ok &= expect_eq("multi write done", dut.done_o, 1U);
    ok &= expect_eq("multi write error", dut.error_o, 0U);
    ok &= expect_eq("multi write burst count", memory.observed_bursts().size(), 2U);
    ok &= expect_eq("multi write burst0 addr", memory.observed_bursts().at(0).addr, 0x1F0U);
    ok &= expect_eq("multi write burst0 beats", memory.observed_bursts().at(0).beats, 16U);
    ok &= expect_eq("multi write burst1 addr", memory.observed_bursts().at(1).addr, 0x2F0U);
    ok &= expect_eq("multi write burst1 beats", memory.observed_bursts().at(1).beats, 4U);

    for (std::uint32_t i = 0; i < source.size(); ++i) {
        const auto actual = memory.load_beat(0x1F0 + (i * 16U));
        ok &= expect_eq("multi write lo", actual.lo, source.at(i).lo);
        ok &= expect_eq("multi write hi", actual.hi, source.at(i).hi);
    }

    return ok;
}

bool test_alignment_error(Vnpu_write_dma_test_top& dut) {
    reset(dut);
    bool ok = true;

    start_write(dut, 0x181, 64);
    ok &= expect_eq("unaligned write error", dut.error_o, 1U);
    ok &= expect_eq("unaligned write error code", dut.error_code_o, kErrUnsupportedAlignment);
    ok &= expect_eq("unaligned write no awvalid", dut.m_axi_awvalid_o, 0U);

    reset(dut);
    start_write(dut, 0x180, 24);
    ok &= expect_eq("unaligned size write error", dut.error_o, 1U);
    ok &= expect_eq("unaligned size write error code", dut.error_code_o, kErrUnsupportedAlignment);

    reset(dut);
    start_write(dut, 0x180, 0);
    ok &= expect_eq("zero byte write error", dut.error_o, 1U);
    ok &= expect_eq("zero byte write error code", dut.error_code_o, kErrUnsupportedAlignment);
    return ok;
}

bool test_axi_error(Vnpu_write_dma_test_top& dut) {
    reset(dut);
    WriteMemoryModel memory(4096);
    memory.set_error_response(true);
    const auto source = make_source(4);

    bool ok = run_write(dut, memory, 0x180, 64, source);
    ok &= expect_eq("write axi error state", dut.error_o, 1U);
    ok &= expect_eq("write axi error code", dut.error_code_o, kErrAxiWrite);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_write_dma", argc, argv};

    Vnpu_write_dma_test_top dut;
    bool ok = true;

    ok &= test_single_burst(dut);
    ok &= test_multi_burst_cross_tile(dut);
    ok &= test_alignment_error(dut);
    ok &= test_axi_error(dut);

    dut.final();
    using enum holon_npu_tb::coverage_point;
    test.cover({dma_write_single_burst, dma_write_multi_burst, dma_write_alignment_error,
                dma_write_axi_error});
    return test.finish(ok);
}
