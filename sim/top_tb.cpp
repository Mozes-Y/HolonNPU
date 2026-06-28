#include "Vnpu_top.h"

#include "holon_npu_desc.h"
#include "holon_npu_regs.h"

#include <array>
#include <cstdint>
#include <cstring>
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

struct Beat {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
};

struct Burst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
    std::uint32_t index = 0;
    std::uint32_t id = 0;
};

struct GemmCase {
    int m = 0;
    int n = 0;
    int k = 0;
    std::uint32_t seed = 0;
    std::string name;
};

void eval(Vnpu_top& dut) {
    dut.eval();
}

void tick(Vnpu_top& dut) {
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

void clear_inputs(Vnpu_top& dut) {
    dut.s_axil_awaddr_i = 0;
    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wdata_i = 0;
    dut.s_axil_wstrb_i = 0;
    dut.s_axil_wvalid_i = 0;
    dut.s_axil_bready_i = 1;
    dut.s_axil_araddr_i = 0;
    dut.s_axil_arvalid_i = 0;
    dut.s_axil_rready_i = 1;
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

void reset(Vnpu_top& dut) {
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

    void store_descriptor(std::uint64_t addr, const holon_npu_gemm_desc_t& desc) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&desc);
        for (std::size_t offset = 0; offset < sizeof(desc); ++offset) {
            memory_.at(addr + offset) = bytes[offset];
        }
    }

    void inject_read_error(std::uint32_t burst_id, std::uint32_t beat_index) {
        read_error_enabled_ = true;
        read_error_burst_id_ = burst_id;
        read_error_beat_index_ = beat_index;
        read_error_used_ = false;
    }

    bool read_error_used() const {
        return read_error_used_;
    }

    void inject_next_write_error() {
        write_error_once_ = true;
    }

    bool write_error_used() const {
        return write_error_used_;
    }

    void step(Vnpu_top& dut) {
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
            const bool read_error =
                read_error_enabled_ && !read_error_used_ &&
                (read_burst_.id == read_error_burst_id_) &&
                (read_burst_.index == read_error_beat_index_);
            dut.m_axi_rvalid_i = 1;
            dut.m_axi_rdata_lo_i = beat.lo;
            dut.m_axi_rdata_hi_i = beat.hi;
            dut.m_axi_rlast_i = last ? 1 : 0;
            dut.m_axi_rresp_i = read_error ? kRespSlvErr : kRespOkay;
        } else {
            dut.m_axi_rvalid_i = 0;
            dut.m_axi_rdata_lo_i = 0;
            dut.m_axi_rdata_hi_i = 0;
            dut.m_axi_rlast_i = 0;
            dut.m_axi_rresp_i = kRespOkay;
        }

        if (write_response_pending_) {
            dut.m_axi_bvalid_i = 1;
            dut.m_axi_bresp_i = write_response_error_pending_ ? kRespSlvErr : kRespOkay;
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
            read_pending_.push_back(Burst{ar_addr, ar_beats, 0, next_read_burst_id_});
            next_read_burst_id_ += 1U;
        }

        if (r_fire && read_active_) {
            if (read_error_enabled_ && !read_error_used_ &&
                (read_burst_.id == read_error_burst_id_) &&
                (read_burst_.index == read_error_beat_index_)) {
                read_error_used_ = true;
            }
            read_burst_.index += 1U;
            if (read_burst_.index == read_burst_.beats) {
                read_active_ = false;
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
                write_response_error_pending_ = write_error_once_;
                if (write_error_once_) {
                    write_error_used_ = true;
                    write_error_once_ = false;
                }
            }
        }

        if (b_fire) {
            write_response_pending_ = false;
            write_response_error_pending_ = false;
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
    bool write_response_error_pending_ = false;
    bool write_error_once_ = false;
    bool write_error_used_ = false;
    bool read_error_enabled_ = false;
    bool read_error_used_ = false;
    std::uint32_t read_error_burst_id_ = 0;
    std::uint32_t read_error_beat_index_ = 0;
    std::uint32_t next_read_burst_id_ = 0;
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

std::uint32_t axil_write(Vnpu_top& dut, std::uint32_t addr, std::uint32_t data) {
    dut.s_axil_awaddr_i = static_cast<std::uint16_t>(addr);
    dut.s_axil_awvalid_i = 1;
    dut.s_axil_wdata_i = data;
    dut.s_axil_wstrb_i = 0xF;
    dut.s_axil_wvalid_i = 1;
    dut.s_axil_bready_i = 1;
    tick(dut);

    const auto resp = static_cast<std::uint32_t>(dut.s_axil_bresp_o);
    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wvalid_i = 0;
    tick(dut);
    return resp;
}

std::uint32_t axil_read(Vnpu_top& dut, std::uint32_t addr, std::uint32_t* resp = nullptr) {
    dut.s_axil_araddr_i = static_cast<std::uint16_t>(addr);
    dut.s_axil_arvalid_i = 1;
    dut.s_axil_rready_i = 1;
    tick(dut);

    const auto data = static_cast<std::uint32_t>(dut.s_axil_rdata_o);
    const auto rresp = static_cast<std::uint32_t>(dut.s_axil_rresp_o);
    dut.s_axil_arvalid_i = 0;
    tick(dut);

    if (resp != nullptr) {
        *resp = rresp;
    }
    return data;
}

std::uint32_t axil_write_aw_then_w(Vnpu_top& dut, std::uint32_t addr, std::uint32_t data) {
    dut.s_axil_awaddr_i = static_cast<std::uint16_t>(addr);
    dut.s_axil_awvalid_i = 1;
    dut.s_axil_wvalid_i = 0;
    dut.s_axil_bready_i = 1;
    tick(dut);

    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wdata_i = data;
    dut.s_axil_wstrb_i = 0xF;
    dut.s_axil_wvalid_i = 1;
    tick(dut);

    const auto resp = static_cast<std::uint32_t>(dut.s_axil_bresp_o);
    dut.s_axil_wvalid_i = 0;
    tick(dut);
    return resp;
}

std::uint32_t axil_write_w_then_aw(Vnpu_top& dut, std::uint32_t addr, std::uint32_t data) {
    dut.s_axil_wdata_i = data;
    dut.s_axil_wstrb_i = 0xF;
    dut.s_axil_wvalid_i = 1;
    dut.s_axil_awvalid_i = 0;
    dut.s_axil_bready_i = 1;
    tick(dut);

    dut.s_axil_wvalid_i = 0;
    dut.s_axil_awaddr_i = static_cast<std::uint16_t>(addr);
    dut.s_axil_awvalid_i = 1;
    tick(dut);

    const auto resp = static_cast<std::uint32_t>(dut.s_axil_bresp_o);
    dut.s_axil_awvalid_i = 0;
    tick(dut);
    return resp;
}

holon_npu_gemm_desc_t make_descriptor(
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
    holon_npu_gemm_desc_t desc{};
    desc.size_bytes = static_cast<std::uint16_t>(HOLON_NPU_DESC_SIZE);
    desc.version = static_cast<std::uint8_t>(HOLON_NPU_ABI_MAJOR);
    desc.opcode = static_cast<std::uint8_t>(HOLON_NPU_OPCODE_GEMM_I8I8I32);
    desc.flags = HOLON_NPU_DESC_FLAG_IRQ_ON_DONE | HOLON_NPU_DESC_FLAG_IRQ_ON_ERROR |
                 HOLON_NPU_DESC_FLAG_CLEAR_PERF_ON_START;
    desc.m = m;
    desc.n = n;
    desc.k = k;
    desc.a_addr = a_addr;
    desc.b_addr = b_addr;
    desc.c_addr = c_addr;
    desc.a_row_stride_bytes = a_stride;
    desc.b_row_stride_bytes = b_stride;
    desc.c_row_stride_bytes = c_stride;
    return desc;
}

struct PreparedGemm {
    std::vector<std::int8_t> a;
    std::vector<std::int8_t> b;
    std::uint32_t a_stride = 0;
    std::uint32_t b_stride = 0;
    std::uint32_t c_stride = 0;
};

PreparedGemm prepare_gemm_memory(
    AxiMemory& memory,
    const GemmCase& test_case,
    std::uint64_t desc_addr,
    std::uint64_t a_addr,
    std::uint64_t b_addr,
    std::uint64_t c_addr
) {
    PreparedGemm prepared{};
    prepared.a_stride = align_up(static_cast<std::uint32_t>(test_case.k), 16);
    prepared.b_stride = align_up(static_cast<std::uint32_t>(test_case.n), 16);
    prepared.c_stride = align_up(static_cast<std::uint32_t>(test_case.n * 4), 16);
    prepared.a.resize(static_cast<std::size_t>(test_case.m * test_case.k));
    prepared.b.resize(static_cast<std::size_t>(test_case.k * test_case.n));

    for (int row = 0; row < test_case.m; ++row) {
        for (int col = 0; col < test_case.k; ++col) {
            const auto value = pattern_value(test_case.seed, row, col, false);
            prepared.a.at((row * test_case.k) + col) = value;
            memory.store_u8(a_addr + (static_cast<std::uint64_t>(row) * prepared.a_stride) + col,
                            static_cast<std::uint8_t>(value));
        }
    }

    for (int row = 0; row < test_case.k; ++row) {
        for (int col = 0; col < test_case.n; ++col) {
            const auto value = pattern_value(test_case.seed, row, col, true);
            prepared.b.at((row * test_case.n) + col) = value;
            memory.store_u8(b_addr + (static_cast<std::uint64_t>(row) * prepared.b_stride) + col,
                            static_cast<std::uint8_t>(value));
        }
    }

    auto desc = make_descriptor(
        static_cast<std::uint32_t>(test_case.m),
        static_cast<std::uint32_t>(test_case.n),
        static_cast<std::uint32_t>(test_case.k),
        a_addr,
        b_addr,
        c_addr,
        prepared.a_stride,
        prepared.b_stride,
        prepared.c_stride
    );
    memory.store_descriptor(desc_addr, desc);
    return prepared;
}

bool start_descriptor(Vnpu_top& dut, std::uint64_t desc_addr, std::string_view name) {
    bool ok = true;
    ok &= expect_eq(std::string(name) + " enable IRQ",
                    axil_write(dut, HOLON_NPU_REG_IRQ_ENABLE, HOLON_NPU_IRQ_DONE | HOLON_NPU_IRQ_ERROR),
                    kRespOkay);
    ok &= expect_eq(std::string(name) + " desc lo",
                    axil_write(dut, HOLON_NPU_REG_DESC_ADDR_LO, static_cast<std::uint32_t>(desc_addr)),
                    kRespOkay);
    ok &= expect_eq(std::string(name) + " desc hi",
                    axil_write(dut, HOLON_NPU_REG_DESC_ADDR_HI, static_cast<std::uint32_t>(desc_addr >> 32)),
                    kRespOkay);
    ok &= expect_eq(std::string(name) + " doorbell",
                    axil_write(dut, HOLON_NPU_REG_DOORBELL, HOLON_NPU_DOORBELL_START),
                    kRespOkay);
    return ok;
}

bool run_until_irq(Vnpu_top& dut, AxiMemory& memory, int max_cycles) {
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        if (dut.irq_o) {
            return true;
        }
        memory.step(dut);
    }
    return dut.irq_o != 0;
}

bool run_until_stage(Vnpu_top& dut, AxiMemory& memory, std::uint32_t stage, int max_cycles) {
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        if (static_cast<std::uint32_t>(dut.stage_o) == stage) {
            return true;
        }
        memory.step(dut);
    }
    return static_cast<std::uint32_t>(dut.stage_o) == stage;
}

bool check_gemm_results(
    const AxiMemory& memory,
    const PreparedGemm& prepared,
    const GemmCase& test_case,
    std::uint64_t c_addr
) {
    bool ok = true;
    for (int row = 0; row < test_case.m; ++row) {
        for (int col = 0; col < test_case.n; ++col) {
            const auto actual = memory.load_u32(c_addr + (static_cast<std::uint64_t>(row) * prepared.c_stride) +
                                                (static_cast<std::uint64_t>(col) * 4U));
            const auto expected = golden(prepared.a, prepared.b, test_case.m, test_case.n, test_case.k, row, col);
            if (actual != expected) {
                std::cerr << test_case.name << " C[" << row << "][" << col << "]: expected 0x"
                          << std::hex << expected << ", got 0x" << actual << std::dec << '\n';
                ok = false;
            }
        }
    }
    return ok;
}

bool run_top_gemm_case(Vnpu_top& dut, const GemmCase& test_case) {
    reset(dut);

    constexpr std::uint64_t kDesc = 0x1000;
    constexpr std::uint64_t kBaseA = 0x4000;
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

    auto desc = make_descriptor(
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
    memory.store_descriptor(kDesc, desc);

    bool ok = true;
    ok &= expect_eq(test_case.name + " DEVICE_ID", axil_read(dut, HOLON_NPU_REG_DEVICE_ID), HOLON_NPU_DEVICE_ID_RESET);
    ok &= start_descriptor(dut, kDesc, test_case.name);

    const bool completed = run_until_irq(dut, memory, 250000);

    const auto status = axil_read(dut, HOLON_NPU_REG_STATUS);
    ok &= expect_eq(test_case.name + " completed", completed ? 1U : 0U, 1U);
    ok &= expect_eq(test_case.name + " status done", status & HOLON_NPU_STATUS_DONE, HOLON_NPU_STATUS_DONE);
    ok &= expect_eq(test_case.name + " status error", status & HOLON_NPU_STATUS_ERROR, 0U);
    ok &= expect_eq(test_case.name + " IRQ status", axil_read(dut, HOLON_NPU_REG_IRQ_STATUS), HOLON_NPU_IRQ_DONE);
    ok &= expect_eq(test_case.name + " desc count", axil_read(dut, HOLON_NPU_REG_PERF_DESC_COUNT), 1U);
    ok &= expect_eq(test_case.name + " error count", axil_read(dut, HOLON_NPU_REG_PERF_ERROR_COUNT), 0U);
    ok &= expect_eq(test_case.name + " perf cycles nonzero",
                    axil_read(dut, HOLON_NPU_REG_PERF_CYCLES_LO) != 0 ? 1U : 0U, 1U);
    ok &= expect_eq(test_case.name + " perf busy nonzero",
                    axil_read(dut, HOLON_NPU_REG_PERF_BUSY_LO) != 0 ? 1U : 0U, 1U);

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

bool test_invalid_descriptor_reaches_control(Vnpu_top& dut) {
    reset(dut);
    AxiMemory memory(65536);

    auto desc = make_descriptor(1, 1, 1, 0x2000, 0x3000, 0x4000, 16, 16, 16);
    desc.opcode = 2;
    memory.store_descriptor(0x1000, desc);

    bool ok = true;
    ok &= expect_eq("invalid enable IRQ", axil_write(dut, HOLON_NPU_REG_IRQ_ENABLE, HOLON_NPU_IRQ_ERROR), kRespOkay);
    ok &= expect_eq("invalid desc lo", axil_write(dut, HOLON_NPU_REG_DESC_ADDR_LO, 0x1000), kRespOkay);
    ok &= expect_eq("invalid desc hi", axil_write(dut, HOLON_NPU_REG_DESC_ADDR_HI, 0), kRespOkay);
    ok &= expect_eq("invalid doorbell", axil_write(dut, HOLON_NPU_REG_DOORBELL, HOLON_NPU_DOORBELL_START), kRespOkay);

    bool completed = false;
    for (int cycle = 0; cycle < 2048; ++cycle) {
        if (dut.irq_o) {
            completed = true;
            break;
        }
        memory.step(dut);
    }

    const auto status = axil_read(dut, HOLON_NPU_REG_STATUS);
    ok &= expect_eq("invalid completed", completed ? 1U : 0U, 1U);
    ok &= expect_eq("invalid status error", status & HOLON_NPU_STATUS_ERROR, HOLON_NPU_STATUS_ERROR);
    ok &= expect_eq("invalid error code", axil_read(dut, HOLON_NPU_REG_ERROR_CODE), HOLON_NPU_ERR_INVALID_OPCODE);
    ok &= expect_eq("invalid IRQ status", axil_read(dut, HOLON_NPU_REG_IRQ_STATUS), HOLON_NPU_IRQ_ERROR);
    return ok;
}

bool test_top_axil_write_skew(Vnpu_top& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("skew AW-before-W", axil_write_aw_then_w(dut, HOLON_NPU_REG_DESC_ADDR_LO, 0x55667788U), kRespOkay);
    ok &= expect_eq("skew W-before-AW", axil_write_w_then_aw(dut, HOLON_NPU_REG_DESC_ADDR_HI, 0x11223344U), kRespOkay);
    ok &= expect_eq("skew desc lo readback", axil_read(dut, HOLON_NPU_REG_DESC_ADDR_LO), 0x55667788U);
    ok &= expect_eq("skew desc hi readback", axil_read(dut, HOLON_NPU_REG_DESC_ADDR_HI), 0x11223344U);
    return ok;
}

bool test_descriptor_read_error_drains_and_recovers(Vnpu_top& dut) {
    reset(dut);

    constexpr std::uint64_t kDesc = 0x1000;
    constexpr std::uint64_t kBaseA = 0x2000;
    constexpr std::uint64_t kBaseB = 0x3000;
    constexpr std::uint64_t kBaseC = 0x4000;
    AxiMemory memory(65536);
    const GemmCase test_case{1, 1, 1, 33, "top-desc-read-error"};
    const auto prepared = prepare_gemm_memory(memory, test_case, kDesc, kBaseA, kBaseB, kBaseC);

    memory.inject_read_error(0, 0);

    bool ok = true;
    ok &= start_descriptor(dut, kDesc, test_case.name);
    const bool errored = run_until_irq(dut, memory, 4096);
    const auto error_status = axil_read(dut, HOLON_NPU_REG_STATUS);
    ok &= expect_eq("descriptor read error completed", errored ? 1U : 0U, 1U);
    ok &= expect_eq("descriptor read error used", memory.read_error_used() ? 1U : 0U, 1U);
    ok &= expect_eq("descriptor read status error", error_status & HOLON_NPU_STATUS_ERROR, HOLON_NPU_STATUS_ERROR);
    ok &= expect_eq("descriptor read error code", axil_read(dut, HOLON_NPU_REG_ERROR_CODE), HOLON_NPU_ERR_AXI_READ);
    ok &= expect_eq("descriptor read IRQ status", axil_read(dut, HOLON_NPU_REG_IRQ_STATUS), HOLON_NPU_IRQ_ERROR);

    ok &= expect_eq("descriptor read clear error", axil_write(dut, HOLON_NPU_REG_CLEAR, HOLON_NPU_CLEAR_ERROR), kRespOkay);
    ok &= expect_eq("descriptor read status idle", axil_read(dut, HOLON_NPU_REG_STATUS), HOLON_NPU_STATUS_IDLE);
    ok &= expect_eq("descriptor read IRQ cleared", axil_read(dut, HOLON_NPU_REG_IRQ_STATUS), 0U);

    ok &= start_descriptor(dut, kDesc, "top-desc-read-retry");
    const bool completed = run_until_irq(dut, memory, 4096);
    const auto done_status = axil_read(dut, HOLON_NPU_REG_STATUS);
    ok &= expect_eq("descriptor read retry completed", completed ? 1U : 0U, 1U);
    ok &= expect_eq("descriptor read retry done", done_status & HOLON_NPU_STATUS_DONE, HOLON_NPU_STATUS_DONE);
    ok &= expect_eq("descriptor read retry error", done_status & HOLON_NPU_STATUS_ERROR, 0U);
    ok &= expect_eq("descriptor read retry error code", axil_read(dut, HOLON_NPU_REG_ERROR_CODE), HOLON_NPU_ERR_NONE);
    ok &= check_gemm_results(memory, prepared, test_case, kBaseC);
    return ok;
}

bool test_gemm_read_error_reaches_control(Vnpu_top& dut) {
    reset(dut);

    constexpr std::uint64_t kDesc = 0x1000;
    constexpr std::uint64_t kBaseA = 0x2000;
    constexpr std::uint64_t kBaseB = 0x3000;
    constexpr std::uint64_t kBaseC = 0x4000;
    AxiMemory memory(65536);
    const GemmCase test_case{1, 1, 1, 44, "top-gemm-read-error"};
    (void)prepare_gemm_memory(memory, test_case, kDesc, kBaseA, kBaseB, kBaseC);

    memory.inject_read_error(1, 0);

    bool ok = true;
    ok &= start_descriptor(dut, kDesc, test_case.name);
    const bool completed = run_until_irq(dut, memory, 4096);
    const auto status = axil_read(dut, HOLON_NPU_REG_STATUS);
    ok &= expect_eq("GEMM read error completed", completed ? 1U : 0U, 1U);
    ok &= expect_eq("GEMM read error used", memory.read_error_used() ? 1U : 0U, 1U);
    ok &= expect_eq("GEMM read status error", status & HOLON_NPU_STATUS_ERROR, HOLON_NPU_STATUS_ERROR);
    ok &= expect_eq("GEMM read error code", axil_read(dut, HOLON_NPU_REG_ERROR_CODE), HOLON_NPU_ERR_AXI_READ);
    ok &= expect_eq("GEMM read IRQ status", axil_read(dut, HOLON_NPU_REG_IRQ_STATUS), HOLON_NPU_IRQ_ERROR);
    return ok;
}

bool test_gemm_write_error_reaches_control(Vnpu_top& dut) {
    reset(dut);

    constexpr std::uint64_t kDesc = 0x1000;
    constexpr std::uint64_t kBaseA = 0x2000;
    constexpr std::uint64_t kBaseB = 0x3000;
    constexpr std::uint64_t kBaseC = 0x4000;
    AxiMemory memory(65536);
    const GemmCase test_case{1, 1, 1, 55, "top-gemm-write-error"};
    (void)prepare_gemm_memory(memory, test_case, kDesc, kBaseA, kBaseB, kBaseC);

    memory.inject_next_write_error();

    bool ok = true;
    ok &= start_descriptor(dut, kDesc, test_case.name);
    const bool completed = run_until_irq(dut, memory, 4096);
    const auto status = axil_read(dut, HOLON_NPU_REG_STATUS);
    ok &= expect_eq("GEMM write error completed", completed ? 1U : 0U, 1U);
    ok &= expect_eq("GEMM write error used", memory.write_error_used() ? 1U : 0U, 1U);
    ok &= expect_eq("GEMM write status error", status & HOLON_NPU_STATUS_ERROR, HOLON_NPU_STATUS_ERROR);
    ok &= expect_eq("GEMM write error code", axil_read(dut, HOLON_NPU_REG_ERROR_CODE), HOLON_NPU_ERR_AXI_WRITE);
    ok &= expect_eq("GEMM write IRQ status", axil_read(dut, HOLON_NPU_REG_IRQ_STATUS), HOLON_NPU_IRQ_ERROR);
    return ok;
}

bool test_top_soft_reset_in_flight(Vnpu_top& dut) {
    reset(dut);

    constexpr std::uint32_t kStageCompute = 3;
    constexpr std::uint64_t kDesc = 0x1000;
    constexpr std::uint64_t kBaseA = 0x4000;
    constexpr std::uint64_t kBaseB = 0x20000;
    constexpr std::uint64_t kBaseC = 0x50000;
    constexpr std::uint64_t kRetryDesc = 0x1800;
    constexpr std::uint64_t kRetryA = 0x8000;
    constexpr std::uint64_t kRetryB = 0x9000;
    constexpr std::uint64_t kRetryC = 0xA000;
    AxiMemory memory(1024 * 1024);
    const GemmCase in_flight_case{16, 16, 16, 66, "top-soft-reset-active"};
    const GemmCase retry_case{1, 1, 1, 77, "top-soft-reset-retry"};
    (void)prepare_gemm_memory(memory, in_flight_case, kDesc, kBaseA, kBaseB, kBaseC);

    bool ok = true;
    ok &= start_descriptor(dut, kDesc, in_flight_case.name);
    const bool reached_compute = run_until_stage(dut, memory, kStageCompute, 50000);
    ok &= expect_eq("soft reset reached compute", reached_compute ? 1U : 0U, 1U);
    ok &= expect_eq("soft reset write", axil_write(dut, HOLON_NPU_REG_CONTROL, HOLON_NPU_CONTROL_SOFT_RESET), kRespOkay);
    ok &= expect_eq("soft reset status idle", axil_read(dut, HOLON_NPU_REG_STATUS), HOLON_NPU_STATUS_IDLE);
    ok &= expect_eq("soft reset error code clear", axil_read(dut, HOLON_NPU_REG_ERROR_CODE), HOLON_NPU_ERR_NONE);
    ok &= expect_eq("soft reset IRQ clear", dut.irq_o, 0U);

    const auto prepared_retry = prepare_gemm_memory(memory, retry_case, kRetryDesc, kRetryA, kRetryB, kRetryC);
    ok &= start_descriptor(dut, kRetryDesc, retry_case.name);
    const bool completed = run_until_irq(dut, memory, 4096);
    const auto retry_status = axil_read(dut, HOLON_NPU_REG_STATUS);
    ok &= expect_eq("soft reset retry completed", completed ? 1U : 0U, 1U);
    ok &= expect_eq("soft reset retry done", retry_status & HOLON_NPU_STATUS_DONE, HOLON_NPU_STATUS_DONE);
    ok &= expect_eq("soft reset retry error", retry_status & HOLON_NPU_STATUS_ERROR, 0U);
    ok &= check_gemm_results(memory, prepared_retry, retry_case, kRetryC);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Vnpu_top dut;
    bool ok = true;

    ok &= run_top_gemm_case(dut, GemmCase{1, 1, 1, 11, "top-1x1x1"});
    ok &= run_top_gemm_case(dut, GemmCase{16, 16, 16, 18, "top-16x16x16"});
    ok &= run_top_gemm_case(dut, GemmCase{17, 19, 23, 22, "top-17x19x23"});
    ok &= run_top_gemm_case(dut, GemmCase{64, 64, 64, 64, "top-64x64x64"});
    ok &= test_invalid_descriptor_reaches_control(dut);
    ok &= test_top_axil_write_skew(dut);
    ok &= test_descriptor_read_error_drains_and_recovers(dut);
    ok &= test_gemm_read_error_reaches_control(dut);
    ok &= test_gemm_write_error_reaches_control(dut);
    ok &= test_top_soft_reset_in_flight(dut);

    dut.final();
    return ok ? 0 : 1;
}
