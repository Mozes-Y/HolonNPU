#include "Vnpu_command_processor_test_top.h"

#include "my_npu_desc.h"
#include "my_npu_regs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <verilated.h>

namespace {

constexpr std::uint32_t kRespOkay = 0;
constexpr std::uint32_t kRespSlvErr = 2;
constexpr std::uint32_t kErrInvalidVersion = MY_NPU_ERR_INVALID_DESC_VERSION;
constexpr std::uint32_t kErrInvalidOpcode = MY_NPU_ERR_INVALID_OPCODE;
constexpr std::uint32_t kErrInvalidSize = MY_NPU_ERR_INVALID_DESC_SIZE;
constexpr std::uint32_t kErrInvalidFlags = MY_NPU_ERR_INVALID_FLAGS;
constexpr std::uint32_t kErrUnsupportedAlignment = MY_NPU_ERR_UNSUPPORTED_ALIGNMENT;
constexpr std::uint32_t kErrAxiRead = MY_NPU_ERR_AXI_READ;
constexpr std::uint32_t kErrReservedNonzero = MY_NPU_ERR_RESERVED_NONZERO;
constexpr std::uint32_t kErrDimensionZero = MY_NPU_ERR_DIMENSION_ZERO;
constexpr std::uint32_t kErrDimensionUnsupported = MY_NPU_ERR_DIMENSION_UNSUPPORTED;

struct Burst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
    std::uint32_t index = 0;
};

struct ObservedBurst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
};

using Descriptor = std::array<std::uint8_t, MY_NPU_DESC_SIZE>;

void eval(Vnpu_command_processor_test_top& dut) {
    dut.eval();
}

void tick(Vnpu_command_processor_test_top& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_command_processor_test_top& dut) {
    dut.start_i = 0;
    dut.soft_reset_i = 0;
    dut.desc_addr_i = 0;
    dut.m_axi_arready_i = 0;
    dut.m_axi_rdata_lo_i = 0;
    dut.m_axi_rdata_hi_i = 0;
    dut.m_axi_rresp_i = kRespOkay;
    dut.m_axi_rlast_i = 0;
    dut.m_axi_rvalid_i = 0;
    dut.command_ready_i = 0;
}

void reset(Vnpu_command_processor_test_top& dut) {
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

void put16(Descriptor& desc, std::size_t offset, std::uint16_t value) {
    desc.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
    desc.at(offset + 1) = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
}

void put32(Descriptor& desc, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        desc.at(offset + i) = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFU);
    }
}

void put64(Descriptor& desc, std::size_t offset, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        desc.at(offset + i) = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFU);
    }
}

Descriptor make_valid_descriptor() {
    Descriptor desc{};
    put16(desc, offsetof(my_npu_gemm_desc_t, size_bytes), MY_NPU_DESC_SIZE);
    desc.at(offsetof(my_npu_gemm_desc_t, version)) = MY_NPU_ABI_MAJOR;
    desc.at(offsetof(my_npu_gemm_desc_t, opcode)) = MY_NPU_OPCODE_GEMM_I8I8I32;
    put32(desc, offsetof(my_npu_gemm_desc_t, flags), MY_NPU_DESC_FLAG_VALID_MASK);
    put32(desc, offsetof(my_npu_gemm_desc_t, m), 17);
    put32(desc, offsetof(my_npu_gemm_desc_t, n), 19);
    put32(desc, offsetof(my_npu_gemm_desc_t, k), 23);
    put64(desc, offsetof(my_npu_gemm_desc_t, a_addr), 0x1000);
    put64(desc, offsetof(my_npu_gemm_desc_t, b_addr), 0x2000);
    put64(desc, offsetof(my_npu_gemm_desc_t, c_addr), 0x3000);
    put32(desc, offsetof(my_npu_gemm_desc_t, a_row_stride_bytes), 32);
    put32(desc, offsetof(my_npu_gemm_desc_t, b_row_stride_bytes), 32);
    put32(desc, offsetof(my_npu_gemm_desc_t, c_row_stride_bytes), 80);
    return desc;
}

class DescriptorMemoryModel {
public:
    explicit DescriptorMemoryModel(std::size_t size) : memory_(size) {}

    void store_descriptor(std::uint64_t addr, const Descriptor& desc) {
        for (std::size_t i = 0; i < desc.size(); ++i) {
            memory_.at(addr + i) = desc.at(i);
        }
    }

    void set_error_on_first_beat(bool enabled) {
        error_on_first_beat_ = enabled;
    }

    const std::vector<ObservedBurst>& observed_bursts() const {
        return observed_bursts_;
    }

    void step(Vnpu_command_processor_test_top& dut) {
        dut.m_axi_arready_i = 1;

        if (!active_ && !pending_.empty()) {
            active_ = true;
            active_burst_ = pending_.front();
            pending_.pop_front();
        }

        if (active_) {
            const auto beat_addr = active_burst_.addr + (static_cast<std::uint64_t>(active_burst_.index) * 16U);
            dut.m_axi_rvalid_i = 1;
            dut.m_axi_rdata_lo_i = load64(beat_addr);
            dut.m_axi_rdata_hi_i = load64(beat_addr + 8U);
            dut.m_axi_rlast_i = ((active_burst_.index + 1U) == active_burst_.beats) ? 1 : 0;
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
        const auto ar_addr = dut.m_axi_araddr_o;
        const auto ar_beats = static_cast<std::uint32_t>(dut.m_axi_arlen_o) + 1U;

        tick(dut);

        if (ar_fire) {
            pending_.push_back(Burst{ar_addr, ar_beats, 0});
            observed_bursts_.push_back(ObservedBurst{ar_addr, ar_beats});
        }

        if (r_fire && active_) {
            if (error_on_first_beat_ && !error_sent_) {
                error_sent_ = true;
            } else {
                active_burst_.index += 1U;
                if (active_burst_.index == active_burst_.beats) {
                    active_ = false;
                }
            }
        }
    }

private:
    std::uint64_t load64(std::uint64_t addr) const {
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value |= static_cast<std::uint64_t>(memory_.at(addr + i)) << (i * 8);
        }
        return value;
    }

    std::vector<std::uint8_t> memory_;
    std::deque<Burst> pending_;
    std::vector<ObservedBurst> observed_bursts_;
    bool active_ = false;
    bool error_on_first_beat_ = false;
    bool error_sent_ = false;
    Burst active_burst_;
};

void start_command(Vnpu_command_processor_test_top& dut, std::uint64_t desc_addr) {
    dut.desc_addr_i = desc_addr;
    dut.start_i = 1;
    tick(dut);
    dut.start_i = 0;
    eval(dut);
}

bool run_until_terminal_or_issue(
    Vnpu_command_processor_test_top& dut,
    DescriptorMemoryModel& memory,
    std::uint64_t desc_addr
) {
    start_command(dut, desc_addr);

    for (int cycle = 0; cycle < 512; ++cycle) {
        if (dut.command_valid_o || dut.done_o || dut.error_o) {
            return true;
        }
        memory.step(dut);
    }

    std::cerr << "command processor timed out\n";
    return false;
}

bool test_valid_descriptor(Vnpu_command_processor_test_top& dut) {
    reset(dut);
    DescriptorMemoryModel memory(8192);
    memory.store_descriptor(0x100, make_valid_descriptor());

    bool ok = run_until_terminal_or_issue(dut, memory, 0x100);
    ok &= expect_eq("valid command issued", dut.command_valid_o, 1U);
    ok &= expect_eq("valid command error", dut.error_o, 0U);
    ok &= expect_eq("descriptor burst count", memory.observed_bursts().size(), 1U);
    ok &= expect_eq("descriptor burst addr", memory.observed_bursts().at(0).addr, 0x100U);
    ok &= expect_eq("descriptor burst beats", memory.observed_bursts().at(0).beats, 8U);
    ok &= expect_eq("command m", dut.command_m_o, 17U);
    ok &= expect_eq("command n", dut.command_n_o, 19U);
    ok &= expect_eq("command k", dut.command_k_o, 23U);
    ok &= expect_eq("command a addr", dut.command_a_addr_o, 0x1000U);
    ok &= expect_eq("command b addr", dut.command_b_addr_o, 0x2000U);
    ok &= expect_eq("command c addr", dut.command_c_addr_o, 0x3000U);
    ok &= expect_eq("command a stride", dut.command_a_stride_o, 32U);
    ok &= expect_eq("command b stride", dut.command_b_stride_o, 32U);
    ok &= expect_eq("command c stride", dut.command_c_stride_o, 80U);
    ok &= expect_eq("irq on done", dut.irq_on_done_o, 1U);
    ok &= expect_eq("irq on error", dut.irq_on_error_o, 1U);
    ok &= expect_eq("clear perf on start", dut.clear_perf_on_start_o, 1U);

    dut.command_ready_i = 1;
    tick(dut);
    dut.command_ready_i = 0;
    ok &= expect_eq("valid command done", dut.done_o, 1U);
    return ok;
}

bool run_invalid_descriptor_case(
    Vnpu_command_processor_test_top& dut,
    Descriptor desc,
    std::uint32_t expected_error,
    std::string_view name
) {
    reset(dut);
    DescriptorMemoryModel memory(8192);
    memory.store_descriptor(0x100, desc);

    bool ok = run_until_terminal_or_issue(dut, memory, 0x100);
    ok &= expect_eq(std::string{name}.append(" no command").c_str(), dut.command_valid_o, 0U);
    ok &= expect_eq(std::string{name}.append(" error").c_str(), dut.error_o, 1U);
    ok &= expect_eq(std::string{name}.append(" error code").c_str(), dut.error_code_o, expected_error);
    return ok;
}

bool test_invalid_descriptors(Vnpu_command_processor_test_top& dut) {
    bool ok = true;

    auto desc = make_valid_descriptor();
    put16(desc, offsetof(my_npu_gemm_desc_t, size_bytes), 64);
    ok &= run_invalid_descriptor_case(dut, desc, kErrInvalidSize, "bad size");

    desc = make_valid_descriptor();
    desc.at(offsetof(my_npu_gemm_desc_t, version)) = 2;
    ok &= run_invalid_descriptor_case(dut, desc, kErrInvalidVersion, "bad version");

    desc = make_valid_descriptor();
    desc.at(offsetof(my_npu_gemm_desc_t, opcode)) = 2;
    ok &= run_invalid_descriptor_case(dut, desc, kErrInvalidOpcode, "bad opcode");

    desc = make_valid_descriptor();
    put32(desc, offsetof(my_npu_gemm_desc_t, flags), 0x8);
    ok &= run_invalid_descriptor_case(dut, desc, kErrInvalidFlags, "bad flags");

    desc = make_valid_descriptor();
    put32(desc, offsetof(my_npu_gemm_desc_t, m), 0);
    ok &= run_invalid_descriptor_case(dut, desc, kErrDimensionZero, "zero dimension");

    desc = make_valid_descriptor();
    put32(desc, offsetof(my_npu_gemm_desc_t, n), 65536);
    ok &= run_invalid_descriptor_case(dut, desc, kErrDimensionUnsupported, "unsupported dimension");

    desc = make_valid_descriptor();
    put64(desc, offsetof(my_npu_gemm_desc_t, a_addr), 0x1003);
    ok &= run_invalid_descriptor_case(dut, desc, kErrUnsupportedAlignment, "bad tensor alignment");

    desc = make_valid_descriptor();
    put32(desc, offsetof(my_npu_gemm_desc_t, reserved_14), 1);
    ok &= run_invalid_descriptor_case(dut, desc, kErrReservedNonzero, "reserved nonzero");

    return ok;
}

bool test_descriptor_fetch_errors(Vnpu_command_processor_test_top& dut) {
    reset(dut);
    DescriptorMemoryModel memory(8192);
    memory.store_descriptor(0x100, make_valid_descriptor());

    bool ok = true;
    ok &= run_until_terminal_or_issue(dut, memory, 0x101);
    ok &= expect_eq("unaligned descriptor error", dut.error_o, 1U);
    ok &= expect_eq("unaligned descriptor error code", dut.error_code_o, kErrUnsupportedAlignment);

    reset(dut);
    DescriptorMemoryModel error_memory(8192);
    error_memory.store_descriptor(0x100, make_valid_descriptor());
    error_memory.set_error_on_first_beat(true);
    ok &= run_until_terminal_or_issue(dut, error_memory, 0x100);
    ok &= expect_eq("descriptor read error", dut.error_o, 1U);
    ok &= expect_eq("descriptor read error code", dut.error_code_o, kErrAxiRead);
    return ok;
}

bool test_descriptor_fuzz(Vnpu_command_processor_test_top& dut) {
    bool ok = true;

    for (std::uint32_t seed = 0; seed < 64; ++seed) {
        auto desc = make_valid_descriptor();
        std::uint32_t expected_error = kErrInvalidSize;
        const auto selector = ((seed * 1103515245U) + 12345U) & 7U;

        switch (selector) {
            case 0:
                put16(desc, offsetof(my_npu_gemm_desc_t, size_bytes), static_cast<std::uint16_t>(seed));
                expected_error = kErrInvalidSize;
                break;
            case 1:
                desc.at(offsetof(my_npu_gemm_desc_t, version)) = static_cast<std::uint8_t>(2U + (seed & 3U));
                expected_error = kErrInvalidVersion;
                break;
            case 2:
                desc.at(offsetof(my_npu_gemm_desc_t, opcode)) = static_cast<std::uint8_t>(2U + (seed & 7U));
                expected_error = kErrInvalidOpcode;
                break;
            case 3:
                put32(desc, offsetof(my_npu_gemm_desc_t, flags), 0x8U | (seed << 4));
                expected_error = kErrInvalidFlags;
                break;
            case 4:
                put32(desc, offsetof(my_npu_gemm_desc_t, k), 0);
                expected_error = kErrDimensionZero;
                break;
            case 5:
                put32(desc, offsetof(my_npu_gemm_desc_t, m), 65536U + seed);
                expected_error = kErrDimensionUnsupported;
                break;
            case 6:
                put64(desc, offsetof(my_npu_gemm_desc_t, c_addr), 0x3001U + (seed << 4));
                expected_error = kErrUnsupportedAlignment;
                break;
            default:
                desc.at(offsetof(my_npu_gemm_desc_t, reserved_40) + (seed % 64U)) =
                    static_cast<std::uint8_t>(seed + 1U);
                expected_error = kErrReservedNonzero;
                break;
        }

        ok &= run_invalid_descriptor_case(
            dut,
            desc,
            expected_error,
            std::string{"descriptor fuzz seed "} + std::to_string(seed)
        );
    }

    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Vnpu_command_processor_test_top dut;
    bool ok = true;

    ok &= test_valid_descriptor(dut);
    ok &= test_invalid_descriptors(dut);
    ok &= test_descriptor_fetch_errors(dut);
    ok &= test_descriptor_fuzz(dut);

    dut.final();
    return ok ? 0 : 1;
}
