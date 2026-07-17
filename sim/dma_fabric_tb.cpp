#include "Vnpu_dma_fabric.h"

#include "tb_coverage.hpp"

#include "holon_npu_program.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include <verilated.h>

namespace {

constexpr std::uint32_t kRespOkay = 0;
constexpr std::uint32_t kRespSlvErr = 2;

struct Burst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
    std::uint32_t size = 0;
    std::uint32_t index = 0;
};

struct ObservedBurst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
    std::uint32_t size = 0;
};

struct LocalWrite {
    std::uint32_t addr = 0;
    std::uint32_t data = 0;
};

class LocalMemory {
public:
    explicit LocalMemory(std::size_t byte_count) : memory_(byte_count) {}

    void store_words(std::uint32_t addr, std::span<const std::uint32_t> words) {
        for (std::size_t word_index = 0; word_index < words.size(); ++word_index) {
            const auto word = words[word_index];
            const auto byte_addr = addr + static_cast<std::uint32_t>(word_index * sizeof(std::uint32_t));
            for (std::size_t byte_index = 0; byte_index < sizeof(std::uint32_t); ++byte_index) {
                memory_.at(byte_addr + byte_index) =
                    static_cast<std::uint8_t>((word >> (byte_index * 8U)) & 0xFFU);
            }
        }
    }

    void drive_response(Vnpu_dma_fabric& dut) {
        dut.data_rd_valid_i = pending_valid_ ? 1 : 0;
        dut.data_rd_error_i = pending_error_ ? 1 : 0;
        dut.data_rd_data_i = pending_data_;
    }

    void capture_request(const Vnpu_dma_fabric& dut) {
        next_valid_ = dut.data_rd_valid_o != 0;
        next_error_ = next_valid_ && !range_ok(static_cast<std::uint32_t>(dut.data_rd_addr_o), sizeof(std::uint32_t));
        next_data_ = next_error_ ? 0 : load_word(static_cast<std::uint32_t>(dut.data_rd_addr_o));
    }

    void advance() {
        pending_valid_ = next_valid_;
        pending_error_ = next_error_;
        pending_data_ = next_data_;
        next_valid_ = false;
        next_error_ = false;
        next_data_ = 0;
    }

private:
    [[nodiscard]] bool range_ok(std::uint32_t addr, std::size_t byte_count) const {
        const auto offset = static_cast<std::size_t>(addr);
        return offset <= memory_.size() && byte_count <= memory_.size() - offset;
    }

    [[nodiscard]] std::uint32_t load_word(std::uint32_t addr) const {
        std::uint32_t value = 0;
        for (std::size_t byte_index = 0; byte_index < sizeof(std::uint32_t); ++byte_index) {
            value |= static_cast<std::uint32_t>(memory_.at(addr + byte_index)) << (byte_index * 8U);
        }
        return value;
    }

    std::vector<std::uint8_t> memory_;
    bool pending_valid_ = false;
    bool pending_error_ = false;
    std::uint32_t pending_data_ = 0;
    bool next_valid_ = false;
    bool next_error_ = false;
    std::uint32_t next_data_ = 0;
};

void eval(Vnpu_dma_fabric& dut) {
    dut.eval();
}

void tick(Vnpu_dma_fabric& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_dma_fabric& dut) {
    dut.soft_reset_i = 0;
    dut.dma_issue_valid_i = 0;
    dut.dma_store_i = 0;
    dut.dma_system_addr_i = 0;
    dut.dma_local_addr_i = 0;
    dut.dma_byte_count_i = 0;
    dut.local_mem_bytes_i = 64;
    dut.data_wr_ready_i = 1;
    dut.data_rd_valid_i = 0;
    dut.data_rd_data_i = 0;
    dut.data_rd_error_i = 0;

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

void reset(Vnpu_dma_fabric& dut) {
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

class AxiMemory {
public:
    explicit AxiMemory(std::size_t byte_count) : memory_(byte_count) {}

    void store_words(std::uint64_t addr, std::span<const std::uint32_t> words) {
        for (std::size_t word_index = 0; word_index < words.size(); ++word_index) {
            const auto word = words[word_index];
            const auto byte_addr = addr + (word_index * sizeof(std::uint32_t));
            for (std::size_t byte_index = 0; byte_index < sizeof(std::uint32_t); ++byte_index) {
                memory_.at(byte_addr + byte_index) =
                    static_cast<std::uint8_t>((word >> (byte_index * 8U)) & 0xFFU);
            }
        }
    }

    std::vector<std::uint32_t> read_words(std::uint64_t addr, std::size_t count) const {
        std::vector<std::uint32_t> words(count);
        for (std::size_t word_index = 0; word_index < count; ++word_index) {
            const auto byte_addr = addr + (word_index * sizeof(std::uint32_t));
            std::uint32_t value = 0;
            for (std::size_t byte_index = 0; byte_index < sizeof(std::uint32_t); ++byte_index) {
                value |= static_cast<std::uint32_t>(memory_.at(byte_addr + byte_index)) << (byte_index * 8U);
            }
            words.at(word_index) = value;
        }
        return words;
    }

    [[nodiscard]] const std::vector<ObservedBurst>& read_bursts() const {
        return read_bursts_;
    }

    [[nodiscard]] const std::vector<ObservedBurst>& write_bursts() const {
        return write_bursts_;
    }

    void step(
        Vnpu_dma_fabric& dut,
        std::vector<LocalWrite>& writes,
        std::uint32_t rresp = kRespOkay,
        std::uint32_t bresp = kRespOkay,
        LocalMemory* local = nullptr
    ) {
        dut.m_axi_arready_i = 1;
        dut.m_axi_awready_i = 1;
        dut.m_axi_wready_i = 1;
        dut.m_axi_bvalid_i = pending_b_valid_ ? 1 : 0;
        dut.m_axi_bresp_i = pending_b_valid_ ? pending_b_resp_ : bresp;
        if (local != nullptr) {
            local->drive_response(dut);
        } else {
            dut.data_rd_valid_i = 0;
            dut.data_rd_data_i = 0;
            dut.data_rd_error_i = 0;
        }

        if (!active_ && !pending_.empty()) {
            active_ = true;
            active_burst_ = pending_.front();
            pending_.pop_front();
        }

        if (active_) {
            const auto beat_bytes = std::uint64_t{1} << active_burst_.size;
            const auto beat_addr = active_burst_.addr +
                                   (static_cast<std::uint64_t>(active_burst_.index) * beat_bytes);
            const auto bus_addr = beat_addr & ~std::uint64_t{0x0F};
            dut.m_axi_rvalid_i = 1;
            dut.m_axi_rdata_lo_i = load64(bus_addr);
            dut.m_axi_rdata_hi_i = load64(bus_addr + 8U);
            dut.m_axi_rlast_i = ((active_burst_.index + 1U) == active_burst_.beats) ? 1 : 0;
            dut.m_axi_rresp_i = rresp;
        } else {
            dut.m_axi_rvalid_i = 0;
            dut.m_axi_rdata_lo_i = 0;
            dut.m_axi_rdata_hi_i = 0;
            dut.m_axi_rlast_i = 0;
            dut.m_axi_rresp_i = kRespOkay;
        }

        eval(dut);
        if (dut.data_wr_valid_o && dut.data_wr_ready_i) {
            writes.push_back(LocalWrite{
                .addr = static_cast<std::uint32_t>(dut.data_wr_addr_o),
                .data = static_cast<std::uint32_t>(dut.data_wr_data_o),
            });
        }

        const bool ar_fire = dut.m_axi_arvalid_o && dut.m_axi_arready_i;
        const bool r_fire = dut.m_axi_rvalid_i && dut.m_axi_rready_o;
        const bool aw_fire = dut.m_axi_awvalid_o && dut.m_axi_awready_i;
        const bool w_fire = dut.m_axi_wvalid_o && dut.m_axi_wready_i;
        const bool b_fire = dut.m_axi_bvalid_i && dut.m_axi_bready_o;
        const auto ar_addr = static_cast<std::uint64_t>(dut.m_axi_araddr_o);
        const auto ar_beats = static_cast<std::uint32_t>(dut.m_axi_arlen_o) + 1U;
        const auto ar_size = static_cast<std::uint32_t>(dut.m_axi_arsize_o);
        const auto aw_addr = static_cast<std::uint64_t>(dut.m_axi_awaddr_o);
        const auto aw_beats = static_cast<std::uint32_t>(dut.m_axi_awlen_o) + 1U;
        const auto aw_size = static_cast<std::uint32_t>(dut.m_axi_awsize_o);
        const auto wdata_lo = static_cast<std::uint64_t>(dut.m_axi_wdata_lo_o);
        const auto wdata_hi = static_cast<std::uint64_t>(dut.m_axi_wdata_hi_o);
        const auto wstrb = static_cast<std::uint32_t>(dut.m_axi_wstrb_o);
        const bool wlast = dut.m_axi_wlast_o != 0;
        if (local != nullptr) {
            local->capture_request(dut);
        }
        tick(dut);

        if (ar_fire) {
            pending_.push_back(Burst{ar_addr, ar_beats, ar_size, 0});
            read_bursts_.push_back(ObservedBurst{ar_addr, ar_beats, ar_size});
        }
        if (aw_fire) {
            write_active_ = true;
            write_addr_ = aw_addr;
            write_size_ = aw_size;
            write_index_ = 0;
            write_bursts_.push_back(ObservedBurst{aw_addr, aw_beats, aw_size});
        }
        if (w_fire && write_active_) {
            const auto beat_bytes = std::uint64_t{1} << write_size_;
            const auto beat_addr = write_addr_ + (static_cast<std::uint64_t>(write_index_) * beat_bytes);
            store_bus_word(beat_addr & ~std::uint64_t{0x0F}, wdata_lo, wdata_hi, wstrb);
            write_index_ += 1U;
            if (wlast) {
                write_active_ = false;
                pending_b_valid_ = true;
                pending_b_resp_ = bresp;
            }
        }
        if (b_fire) {
            pending_b_valid_ = false;
            pending_b_resp_ = kRespOkay;
        }
        if (r_fire && active_) {
            active_burst_.index += 1U;
            if (active_burst_.index == active_burst_.beats) {
                active_ = false;
            }
        }
        if (local != nullptr) {
            local->advance();
        }
    }

private:
    [[nodiscard]] std::uint64_t load64(std::uint64_t addr) const {
        std::uint64_t value = 0;
        for (int index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(memory_.at(addr + index)) << (index * 8);
        }
        return value;
    }

    void store_bus_word(
        std::uint64_t bus_addr,
        std::uint64_t data_lo,
        std::uint64_t data_hi,
        std::uint32_t strobe
    ) {
        for (std::size_t byte_index = 0; byte_index < 16; ++byte_index) {
            if (((strobe >> byte_index) & 1U) != 0U) {
                const auto word = byte_index < 8 ? data_lo : data_hi;
                const auto lane_byte = byte_index < 8 ? byte_index : byte_index - 8;
                memory_.at(bus_addr + byte_index) =
                    static_cast<std::uint8_t>((word >> (lane_byte * 8U)) & 0xFFU);
            }
        }
    }

    std::vector<std::uint8_t> memory_;
    std::vector<ObservedBurst> read_bursts_;
    std::vector<ObservedBurst> write_bursts_;
    std::deque<Burst> pending_;
    bool active_ = false;
    Burst active_burst_{};
    bool write_active_ = false;
    std::uint64_t write_addr_ = 0;
    std::uint32_t write_size_ = 0;
    std::uint32_t write_index_ = 0;
    bool pending_b_valid_ = false;
    std::uint32_t pending_b_resp_ = kRespOkay;
};

void issue_load(
    Vnpu_dma_fabric& dut,
    std::uint64_t system_addr,
    std::uint32_t local_addr,
    std::uint32_t byte_count
) {
    dut.dma_store_i = 0;
    dut.dma_system_addr_i = system_addr;
    dut.dma_local_addr_i = local_addr;
    dut.dma_byte_count_i = byte_count;
    dut.dma_issue_valid_i = 1;
    tick(dut);
    dut.dma_issue_valid_i = 0;
    eval(dut);
}

void issue_store(
    Vnpu_dma_fabric& dut,
    std::uint64_t system_addr,
    std::uint32_t local_addr,
    std::uint32_t byte_count
) {
    dut.dma_store_i = 1;
    dut.dma_system_addr_i = system_addr;
    dut.dma_local_addr_i = local_addr;
    dut.dma_byte_count_i = byte_count;
    dut.dma_issue_valid_i = 1;
    tick(dut);
    dut.dma_issue_valid_i = 0;
    dut.dma_store_i = 0;
    eval(dut);
}

bool run_until_terminal(
    Vnpu_dma_fabric& dut,
    AxiMemory& memory,
    std::vector<LocalWrite>& writes,
    std::uint32_t rresp = kRespOkay,
    std::uint32_t bresp = kRespOkay,
    LocalMemory* local = nullptr,
    int max_cycles = 80
) {
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        memory.step(dut, writes, rresp, bresp, local);
        if (dut.dma_event_valid_o || dut.dma_fault_valid_o) {
            return true;
        }
    }
    return dut.dma_event_valid_o || dut.dma_fault_valid_o;
}

bool test_successful_load(Vnpu_dma_fabric& dut) {
    reset(dut);
    AxiMemory memory(0x1000);
    std::vector<LocalWrite> writes;
    const std::uint32_t words[] = {0x1111'0001U, 0x2222'0002U, 0x3333'0003U};
    memory.store_words(0x80, words);

    issue_load(dut, 0x80, 8, sizeof(words));
    bool ok = true;
    ok &= expect_eq("issue ready", dut.dma_issue_ready_o, 0U);
    ok &= expect_eq("terminal", run_until_terminal(dut, memory, writes), true);
    ok &= expect_eq("event", dut.dma_event_valid_o, 1U);
    ok &= expect_eq("fault", dut.dma_fault_valid_o, 0U);
    ok &= expect_eq("write count", writes.size(), std::size_t{3});
    ok &= expect_eq("write 0 addr", writes.at(0).addr, 8U);
    ok &= expect_eq("write 0 data", writes.at(0).data, words[0]);
    ok &= expect_eq("write 1 addr", writes.at(1).addr, 12U);
    ok &= expect_eq("write 1 data", writes.at(1).data, words[1]);
    ok &= expect_eq("write 2 addr", writes.at(2).addr, 16U);
    ok &= expect_eq("write 2 data", writes.at(2).data, words[2]);
    return ok;
}

bool test_64bit_system_address(Vnpu_dma_fabric& dut) {
    reset(dut);
    constexpr std::uint64_t system_addr = 0x0000'0001'0000'0080ULL;
    issue_load(dut, system_addr, 0, sizeof(std::uint32_t));

    bool ok = true;
    ok &= expect_eq("64-bit address valid", dut.m_axi_arvalid_o, 1U);
    ok &= expect_eq("64-bit address value", dut.m_axi_araddr_o, system_addr);
    return ok;
}

bool test_backpressure(Vnpu_dma_fabric& dut) {
    reset(dut);
    AxiMemory memory(0x1000);
    std::vector<LocalWrite> writes;
    const std::uint32_t word = 0xCAFE'1234U;
    memory.store_words(0x90, std::span<const std::uint32_t>(&word, 1));

    issue_load(dut, 0x90, 0, sizeof(word));
    dut.data_wr_ready_i = 0;
    memory.step(dut, writes);
    bool ok = true;
    ok &= expect_eq("rready blocked", dut.m_axi_rready_o, 0U);
    ok &= expect_eq("no write under backpressure", writes.size(), std::size_t{0});
    dut.data_wr_ready_i = 1;
    ok &= expect_eq("terminal", run_until_terminal(dut, memory, writes), true);
    ok &= expect_eq("write count", writes.size(), std::size_t{1});
    ok &= expect_eq("write data", writes.at(0).data, word);
    return ok;
}

bool test_successful_store(Vnpu_dma_fabric& dut) {
    reset(dut);
    AxiMemory memory(0x1000);
    LocalMemory local(64);
    std::vector<LocalWrite> writes;
    const std::uint32_t words[] = {0x4444'1001U, 0x5555'1002U};
    local.store_words(12, words);

    issue_store(dut, 0xB4, 12, sizeof(words));
    bool ok = true;
    ok &= expect_eq("store terminal", run_until_terminal(dut, memory, writes, kRespOkay, kRespOkay, &local), true);
    ok &= expect_eq("store event", dut.dma_event_valid_o, 1U);
    ok &= expect_eq("store fault", dut.dma_fault_valid_o, 0U);
    ok &= expect_eq("store local writes", writes.size(), std::size_t{0});
    const auto stored = memory.read_words(0xB4, 2);
    ok &= expect_eq("store word 0", stored.at(0), words[0]);
    ok &= expect_eq("store word 1", stored.at(1), words[1]);
    return ok;
}

bool test_4k_boundary_splits(Vnpu_dma_fabric& dut) {
    constexpr std::uint64_t load_address = 0x0FFCU;
    constexpr std::uint64_t store_address = 0x1FFCU;
    const std::array<std::uint32_t, 8> words{
        0xA000'0001U, 0xA000'0002U, 0xA000'0003U, 0xA000'0004U,
        0xA000'0005U, 0xA000'0006U, 0xA000'0007U, 0xA000'0008U,
    };

    bool ok = true;
    {
        reset(dut);
        dut.local_mem_bytes_i = 64;
        AxiMemory memory(0x3000);
        std::vector<LocalWrite> writes;
        memory.store_words(load_address, words);
        issue_load(dut, load_address, 0, sizeof(words));
        ok &= expect_eq("split load terminal", run_until_terminal(dut, memory, writes), true);
        ok &= expect_eq("split load event", dut.dma_event_valid_o, 1U);
        ok &= expect_eq("split load burst count", memory.read_bursts().size(), 2U);
        if (memory.read_bursts().size() == 2U) {
            ok &= expect_eq("split load burst0 address", memory.read_bursts()[0].addr, 0x0FFCU);
            ok &= expect_eq("split load burst0 beats", memory.read_bursts()[0].beats, 1U);
            ok &= expect_eq("split load burst1 address", memory.read_bursts()[1].addr, 0x1000U);
            ok &= expect_eq("split load burst1 beats", memory.read_bursts()[1].beats, 7U);
        }
        ok &= expect_eq("split load write count", writes.size(), words.size());
        for (std::size_t index = 0; index < std::min(writes.size(), words.size()); ++index) {
            ok &= expect_eq("split load data", writes[index].data, words[index]);
        }
    }
    {
        reset(dut);
        dut.local_mem_bytes_i = 64;
        AxiMemory memory(0x3000);
        LocalMemory local(64);
        std::vector<LocalWrite> writes;
        local.store_words(0, words);
        issue_store(dut, store_address, 0, sizeof(words));
        ok &= expect_eq(
            "split store terminal",
            run_until_terminal(dut, memory, writes, kRespOkay, kRespOkay, &local, 160),
            true
        );
        ok &= expect_eq("split store event", dut.dma_event_valid_o, 1U);
        ok &= expect_eq("split store burst count", memory.write_bursts().size(), 2U);
        if (memory.write_bursts().size() == 2U) {
            ok &= expect_eq("split store burst0 address", memory.write_bursts()[0].addr, 0x1FFCU);
            ok &= expect_eq("split store burst0 beats", memory.write_bursts()[0].beats, 1U);
            ok &= expect_eq("split store burst1 address", memory.write_bursts()[1].addr, 0x2000U);
            ok &= expect_eq("split store burst1 beats", memory.write_bursts()[1].beats, 7U);
        }
        const auto stored = memory.read_words(store_address, words.size());
        for (std::size_t index = 0; index < words.size(); ++index) {
            ok &= expect_eq("split store data", stored[index], words[index]);
        }
    }
    return ok;
}

bool test_bounds_fault(Vnpu_dma_fabric& dut) {
    reset(dut);
    dut.local_mem_bytes_i = 8;
    issue_load(dut, 0x80, 4, 8);

    bool ok = true;
    ok &= expect_eq("fault valid", dut.dma_fault_valid_o, 1U);
    ok &= expect_eq("fault code", dut.dma_fault_code_o, HOLON_NPU_FAULT_LOCAL_MEMORY_BOUNDS);
    ok &= expect_eq("no event", dut.dma_event_valid_o, 0U);
    tick(dut);
    return ok;
}

bool test_system_address_overflow_fault(Vnpu_dma_fabric& dut) {
    reset(dut);
    issue_load(dut, std::numeric_limits<std::uint64_t>::max() - 3U, 0, 8);

    bool ok = true;
    ok &= expect_eq("overflow fault valid", dut.dma_fault_valid_o, 1U);
    ok &= expect_eq("overflow fault code", dut.dma_fault_code_o, HOLON_NPU_FAULT_DMA_REQUEST);
    ok &= expect_eq("overflow no event", dut.dma_event_valid_o, 0U);
    ok &= expect_eq("overflow no AXI request", dut.m_axi_arvalid_o, 0U);
    return ok;
}

bool test_store_bresp_fault(Vnpu_dma_fabric& dut) {
    reset(dut);
    AxiMemory memory(0x1000);
    LocalMemory local(64);
    std::vector<LocalWrite> writes;
    const std::uint32_t word = 0xFACE'5501U;
    local.store_words(0, std::span<const std::uint32_t>(&word, 1));

    issue_store(dut, 0xC0, 0, sizeof(word));
    bool ok = true;
    ok &= expect_eq(
        "store b fault terminal",
        run_until_terminal(dut, memory, writes, kRespOkay, kRespSlvErr, &local),
        true
    );
    ok &= expect_eq("store fault valid", dut.dma_fault_valid_o, 1U);
    ok &= expect_eq("store fault code", dut.dma_fault_code_o, HOLON_NPU_FAULT_AXI_WRITE);
    return ok;
}

bool test_axi_fault(Vnpu_dma_fabric& dut) {
    reset(dut);
    AxiMemory memory(0x1000);
    std::vector<LocalWrite> writes;
    const std::uint32_t word = 0xBAD0'0001U;
    memory.store_words(0xA0, std::span<const std::uint32_t>(&word, 1));

    issue_load(dut, 0xA0, 0, sizeof(word));
    bool ok = true;
    ok &= expect_eq("terminal", run_until_terminal(dut, memory, writes, kRespSlvErr), true);
    ok &= expect_eq("fault valid", dut.dma_fault_valid_o, 1U);
    ok &= expect_eq("fault code", dut.dma_fault_code_o, HOLON_NPU_FAULT_AXI_READ);
    ok &= expect_eq("no writes", writes.size(), std::size_t{0});
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_dma_fabric", argc, argv};

    Vnpu_dma_fabric dut;
    bool ok = true;

    using enum holon_npu_tb::coverage_point;
    const bool load_case = test_successful_load(dut);
    test.observe(dma_load_success, load_case);
    ok &= load_case;
    ok &= test_64bit_system_address(dut);

    const bool backpressure_case = test_backpressure(dut);
    test.observe(dma_backpressure, backpressure_case);
    ok &= backpressure_case;

    const bool store_case = test_successful_store(dut);
    test.observe(dma_store_success, store_case);
    ok &= store_case;

    const bool boundary_case = test_4k_boundary_splits(dut);
    test.observe({dma_load_4k_split, dma_store_4k_split}, boundary_case);
    ok &= boundary_case;

    const bool bounds_case = test_bounds_fault(dut);
    test.observe(dma_bounds_fault, bounds_case);
    ok &= bounds_case;

    const bool overflow_case = test_system_address_overflow_fault(dut);
    test.observe(dma_address_overflow_fault, overflow_case);
    ok &= overflow_case;

    const bool bresp_case = test_store_bresp_fault(dut);
    test.observe(dma_store_bresp_fault, bresp_case);
    ok &= bresp_case;

    const bool read_fault_case = test_axi_fault(dut);
    test.observe(dma_axi_fault, read_fault_case);
    ok &= read_fault_case;

    dut.final();
    return test.finish(ok);
}
