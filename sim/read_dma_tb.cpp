#include "Vnpu_read_dma_test_top.h"

#include <cstdint>
#include <deque>
#include <iostream>
#include <string_view>
#include <vector>

#include <verilated.h>

namespace {

constexpr std::uint32_t kErrUnsupportedAlignment = 5;
constexpr std::uint32_t kErrAxiRead = 6;
constexpr std::uint32_t kRespOkay = 0;
constexpr std::uint32_t kRespSlvErr = 2;

struct Beat {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    bool last = false;
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

void eval(Vnpu_read_dma_test_top& dut) {
    dut.eval();
}

void tick(Vnpu_read_dma_test_top& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_read_dma_test_top& dut) {
    dut.start_i = 0;
    dut.addr_i = 0;
    dut.bytes_i = 0;
    dut.m_axi_arready_i = 0;
    dut.m_axi_rdata_lo_i = 0;
    dut.m_axi_rdata_hi_i = 0;
    dut.m_axi_rresp_i = kRespOkay;
    dut.m_axi_rlast_i = 0;
    dut.m_axi_rvalid_i = 0;
    dut.out_ready_i = 1;
}

void reset(Vnpu_read_dma_test_top& dut) {
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

class ReadMemoryModel {
public:
    explicit ReadMemoryModel(std::size_t size) : memory_(size) {
        for (std::size_t i = 0; i < memory_.size(); ++i) {
            memory_[i] = static_cast<std::uint8_t>((i * 17U + 3U) & 0xFFU);
        }
    }

    void set_error_on_first_beat(bool enabled) {
        error_on_first_beat_ = enabled;
    }

    const std::vector<ObservedBurst>& observed_bursts() const {
        return observed_bursts_;
    }

    std::uint32_t accepted_r_beats() const {
        return accepted_r_beats_;
    }

    Beat expected_beat(std::uint64_t addr, std::uint32_t beat_index, bool last) const {
        return load_beat(addr + (static_cast<std::uint64_t>(beat_index) * 16U), last);
    }

    void step(Vnpu_read_dma_test_top& dut, std::vector<Beat>& captured) {
        dut.m_axi_arready_i = 1;

        if (!active_ && !pending_.empty()) {
            active_ = true;
            active_burst_ = pending_.front();
            pending_.pop_front();
        }

        if (active_) {
            const bool last = (active_burst_.index + 1U) == active_burst_.beats;
            const auto beat = load_beat(
                active_burst_.addr + (static_cast<std::uint64_t>(active_burst_.index) * 16U),
                last
            );
            dut.m_axi_rvalid_i = 1;
            dut.m_axi_rdata_lo_i = beat.lo;
            dut.m_axi_rdata_hi_i = beat.hi;
            dut.m_axi_rlast_i = beat.last ? 1 : 0;
            dut.m_axi_rresp_i = (error_on_first_beat_ && !error_sent_) ? kRespSlvErr : kRespOkay;
        } else {
            dut.m_axi_rvalid_i = 0;
            dut.m_axi_rdata_lo_i = 0;
            dut.m_axi_rdata_hi_i = 0;
            dut.m_axi_rlast_i = 0;
            dut.m_axi_rresp_i = kRespOkay;
        }

        eval(dut);
        const bool ar_fire = dut.m_axi_arvalid_o && dut.m_axi_arready_i;
        const bool r_fire = dut.m_axi_rvalid_i && dut.m_axi_rready_o;

        tick(dut);

        if (ar_fire) {
            const auto beats = static_cast<std::uint32_t>(dut.m_axi_arlen_o) + 1U;
            pending_.push_back(Burst{dut.m_axi_araddr_o, beats, 0});
            observed_bursts_.push_back(ObservedBurst{dut.m_axi_araddr_o, beats});
        }

        if (r_fire && active_) {
            accepted_r_beats_ += 1U;
            if (error_on_first_beat_ && !error_sent_) {
                error_sent_ = true;
            }

            active_burst_.index += 1U;
            if (active_burst_.index == active_burst_.beats) {
                active_ = false;
            }
        }

        if (dut.out_valid_o) {
            captured.push_back(Beat{
                dut.out_data_lo_o,
                dut.out_data_hi_o,
                dut.out_last_o != 0,
            });
        }
    }

private:
    Beat load_beat(std::uint64_t addr, bool last) const {
        Beat beat;
        for (int i = 0; i < 8; ++i) {
            beat.lo |= static_cast<std::uint64_t>(memory_.at(addr + i)) << (i * 8);
            beat.hi |= static_cast<std::uint64_t>(memory_.at(addr + 8 + i)) << (i * 8);
        }
        beat.last = last;
        return beat;
    }

    std::vector<std::uint8_t> memory_;
    std::deque<Burst> pending_;
    std::vector<ObservedBurst> observed_bursts_;
    bool active_ = false;
    bool error_on_first_beat_ = false;
    bool error_sent_ = false;
    std::uint32_t accepted_r_beats_ = 0;
    Burst active_burst_;
};

void start_read(Vnpu_read_dma_test_top& dut, std::uint64_t addr, std::uint32_t bytes) {
    dut.addr_i = addr;
    dut.bytes_i = bytes;
    dut.start_i = 1;
    tick(dut);
    dut.start_i = 0;
    eval(dut);
}

bool run_read(
    Vnpu_read_dma_test_top& dut,
    ReadMemoryModel& memory,
    std::uint64_t addr,
    std::uint32_t bytes,
    std::vector<Beat>& captured
) {
    start_read(dut, addr, bytes);

    for (int cycle = 0; cycle < 512; ++cycle) {
        if (dut.done_o || dut.error_o) {
            return true;
        }
        memory.step(dut, captured);
    }

    std::cerr << "read DMA timed out\n";
    return false;
}

bool test_single_burst(Vnpu_read_dma_test_top& dut) {
    reset(dut);
    ReadMemoryModel memory(4096);
    std::vector<Beat> captured;

    bool ok = run_read(dut, memory, 0x100, 64, captured);
    ok &= expect_eq("single read done", dut.done_o, 1U);
    ok &= expect_eq("single read error", dut.error_o, 0U);
    ok &= expect_eq("single read beat count", captured.size(), 4U);
    ok &= expect_eq("single read burst count", memory.observed_bursts().size(), 1U);
    ok &= expect_eq("single read burst addr", memory.observed_bursts().at(0).addr, 0x100U);
    ok &= expect_eq("single read burst beats", memory.observed_bursts().at(0).beats, 4U);

    for (std::uint32_t i = 0; i < captured.size(); ++i) {
        const auto expected = memory.expected_beat(0x100, i, i == 3);
        ok &= expect_eq("single read lo", captured.at(i).lo, expected.lo);
        ok &= expect_eq("single read hi", captured.at(i).hi, expected.hi);
        ok &= expect_eq("single read last", captured.at(i).last ? 1U : 0U, expected.last ? 1U : 0U);
    }

    return ok;
}

bool test_multi_burst_cross_tile(Vnpu_read_dma_test_top& dut) {
    reset(dut);
    ReadMemoryModel memory(8192);
    std::vector<Beat> captured;

    bool ok = run_read(dut, memory, 0x1F0, 20U * 16U, captured);
    ok &= expect_eq("multi read done", dut.done_o, 1U);
    ok &= expect_eq("multi read error", dut.error_o, 0U);
    ok &= expect_eq("multi read beat count", captured.size(), 20U);
    ok &= expect_eq("multi read burst count", memory.observed_bursts().size(), 2U);
    ok &= expect_eq("multi read burst0 addr", memory.observed_bursts().at(0).addr, 0x1F0U);
    ok &= expect_eq("multi read burst0 beats", memory.observed_bursts().at(0).beats, 16U);
    ok &= expect_eq("multi read burst1 addr", memory.observed_bursts().at(1).addr, 0x2F0U);
    ok &= expect_eq("multi read burst1 beats", memory.observed_bursts().at(1).beats, 4U);
    ok &= expect_eq("multi read final last", captured.back().last ? 1U : 0U, 1U);

    for (std::uint32_t i = 0; i < captured.size(); ++i) {
        const auto expected = memory.expected_beat(0x1F0, i, i == 19);
        ok &= expect_eq("multi read lo", captured.at(i).lo, expected.lo);
        ok &= expect_eq("multi read hi", captured.at(i).hi, expected.hi);
    }

    return ok;
}

bool test_alignment_error(Vnpu_read_dma_test_top& dut) {
    reset(dut);
    ReadMemoryModel memory(4096);
    std::vector<Beat> captured;

    start_read(dut, 0x103, 64);
    bool ok = true;
    ok &= expect_eq("unaligned read error", dut.error_o, 1U);
    ok &= expect_eq("unaligned read error code", dut.error_code_o, kErrUnsupportedAlignment);
    ok &= expect_eq("unaligned read no arvalid", dut.m_axi_arvalid_o, 0U);

    reset(dut);
    start_read(dut, 0x100, 24);
    ok &= expect_eq("unaligned size read error", dut.error_o, 1U);
    ok &= expect_eq("unaligned size read error code", dut.error_code_o, kErrUnsupportedAlignment);

    reset(dut);
    start_read(dut, 0x100, 0);
    ok &= expect_eq("zero byte read error", dut.error_o, 1U);
    ok &= expect_eq("zero byte read error code", dut.error_code_o, kErrUnsupportedAlignment);

    return ok;
}

bool test_axi_error(Vnpu_read_dma_test_top& dut) {
    reset(dut);
    ReadMemoryModel memory(4096);
    memory.set_error_on_first_beat(true);
    std::vector<Beat> captured;

    bool ok = run_read(dut, memory, 0x100, 64, captured);
    ok &= expect_eq("read axi error state", dut.error_o, 1U);
    ok &= expect_eq("read axi error code", dut.error_code_o, kErrAxiRead);
    ok &= expect_eq("read axi error drained beats", memory.accepted_r_beats(), 4U);
    ok &= expect_eq("read axi error no output", captured.size(), 0U);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Vnpu_read_dma_test_top dut;
    bool ok = true;

    ok &= test_single_burst(dut);
    ok &= test_multi_burst_cross_tile(dut);
    ok &= test_alignment_error(dut);
    ok &= test_axi_error(dut);

    dut.final();
    return ok ? 0 : 1;
}
