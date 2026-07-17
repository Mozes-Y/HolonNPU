#include "Vnpu_v2_top.h"

#include "tb_coverage.hpp"

#include "holon_npu_isa.h"
#include "holon_npu_program.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include <verilated.h>

namespace {

constexpr std::uint32_t kOkay = 0;
constexpr std::uint64_t kDescriptorAddress = 0x1000;
constexpr std::uint64_t kProgramAddress = 0x2000;
constexpr std::uint64_t kCompletionAddress = 0x3000;

struct Burst {
    std::uint64_t address = 0;
    std::uint32_t beats = 0;
    std::uint32_t size = 0;
    std::uint32_t index = 0;
};

void eval(Vnpu_v2_top& dut) {
    dut.eval();
}

void tick(Vnpu_v2_top& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_v2_top& dut) {
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
    for (std::size_t word = 0; word < 4; ++word) {
        dut.m_axi_rdata_i[word] = 0;
    }
    dut.m_axi_rresp_i = kOkay;
    dut.m_axi_rlast_i = 0;
    dut.m_axi_rvalid_i = 0;
    dut.m_axi_awready_i = 0;
    dut.m_axi_wready_i = 0;
    dut.m_axi_bresp_i = kOkay;
    dut.m_axi_bvalid_i = 0;
}

void reset(Vnpu_v2_top& dut) {
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
    explicit AxiMemory(std::size_t byte_count) : bytes_(byte_count) {}

    template <typename T>
    void store_object(std::uint64_t address, const T& object) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&object);
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            bytes_.at(address + index) = bytes[index];
        }
    }

    void store_words(std::uint64_t address, std::span<const std::uint32_t> words) {
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto word_address = address + index * sizeof(std::uint32_t);
            for (std::size_t byte = 0; byte < sizeof(std::uint32_t); ++byte) {
                bytes_.at(word_address + byte) =
                    static_cast<std::uint8_t>((words[index] >> (byte * 8U)) & 0xFFU);
            }
        }
    }

    template <typename T>
    T read_object(std::uint64_t address) const {
        T object{};
        auto* bytes = reinterpret_cast<std::uint8_t*>(&object);
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            bytes[index] = bytes_.at(address + index);
        }
        return object;
    }

    [[nodiscard]] bool write_response_pending() const {
        return write_response_pending_;
    }

    [[nodiscard]] bool write_profile_valid() const {
        return write_profile_valid_;
    }

    void step(Vnpu_v2_top& dut, bool allow_write_response = true) {
        dut.m_axi_arready_i = 1;
        dut.m_axi_awready_i = 1;
        dut.m_axi_wready_i = 1;
        dut.m_axi_bvalid_i = (write_response_pending_ && allow_write_response) ? 1 : 0;
        dut.m_axi_bresp_i = kOkay;

        if (!active_ && !pending_.empty()) {
            active_ = true;
            burst_ = pending_.front();
            pending_.pop_front();
        }

        if (active_) {
            const auto beat_bytes = std::uint64_t{1} << burst_.size;
            const auto beat_address = burst_.address + burst_.index * beat_bytes;
            const auto bus_address = beat_address & ~std::uint64_t{0x0F};
            for (std::size_t word = 0; word < 4; ++word) {
                dut.m_axi_rdata_i[word] = load32(bus_address + word * sizeof(std::uint32_t));
            }
            dut.m_axi_rvalid_i = 1;
            dut.m_axi_rlast_i = (burst_.index + 1U == burst_.beats) ? 1 : 0;
            dut.m_axi_rresp_i = kOkay;
        } else {
            for (std::size_t word = 0; word < 4; ++word) {
                dut.m_axi_rdata_i[word] = 0;
            }
            dut.m_axi_rvalid_i = 0;
            dut.m_axi_rlast_i = 0;
            dut.m_axi_rresp_i = kOkay;
        }

        eval(dut);
        const bool ar_fire = dut.m_axi_arvalid_o && dut.m_axi_arready_i;
        const bool r_fire = dut.m_axi_rvalid_i && dut.m_axi_rready_o;
        const bool aw_fire = dut.m_axi_awvalid_o && dut.m_axi_awready_i;
        const bool w_fire = dut.m_axi_wvalid_o && dut.m_axi_wready_i;
        const bool b_fire = dut.m_axi_bvalid_i && dut.m_axi_bready_o;
        const auto ar_address = static_cast<std::uint64_t>(dut.m_axi_araddr_o);
        const auto ar_beats = static_cast<std::uint32_t>(dut.m_axi_arlen_o) + 1U;
        const auto ar_size = static_cast<std::uint32_t>(dut.m_axi_arsize_o);
        const auto aw_address = static_cast<std::uint64_t>(dut.m_axi_awaddr_o);
        const auto aw_beats = static_cast<std::uint32_t>(dut.m_axi_awlen_o) + 1U;
        const auto aw_size = static_cast<std::uint32_t>(dut.m_axi_awsize_o);
        const auto aw_burst = static_cast<std::uint32_t>(dut.m_axi_awburst_o);
        const auto write_strobe = static_cast<std::uint32_t>(dut.m_axi_wstrb_o);
        const bool write_last = dut.m_axi_wlast_o != 0;
        std::array<std::uint32_t, 4> write_data{};
        for (std::size_t word = 0; word < write_data.size(); ++word) {
            write_data[word] = dut.m_axi_wdata_o[word];
        }
        tick(dut);

        if (ar_fire) {
            pending_.push_back(Burst{ar_address, ar_beats, ar_size, 0});
        }
        if (r_fire && active_) {
            ++burst_.index;
            if (burst_.index == burst_.beats) {
                active_ = false;
            }
        }
        if (aw_fire) {
            write_active_ = true;
            write_address_ = aw_address;
            write_size_ = aw_size;
            write_beats_ = aw_beats;
            write_index_ = 0;
            write_profile_valid_ = write_profile_valid_ &&
                                   (aw_address == kCompletionAddress) &&
                                   (aw_beats == 2U) &&
                                   (aw_size == 4U) &&
                                   (aw_burst == 1U);
        }
        if (w_fire && write_active_) {
            const auto beat_bytes = std::uint64_t{1} << write_size_;
            const auto beat_address = write_address_ + write_index_ * beat_bytes;
            store_write_beat(beat_address, write_data, write_strobe);
            ++write_index_;
            write_profile_valid_ = write_profile_valid_ &&
                                   (write_strobe == 0xFFFFU) &&
                                   (write_last == (write_index_ == write_beats_));
            if (write_last) {
                write_active_ = false;
                write_response_pending_ = true;
            }
        }
        if (b_fire) {
            write_response_pending_ = false;
        }
    }

private:
    std::uint32_t load32(std::uint64_t address) const {
        std::uint32_t result = 0;
        for (std::size_t byte = 0; byte < sizeof(result); ++byte) {
            result |= static_cast<std::uint32_t>(bytes_.at(address + byte)) << (byte * 8U);
        }
        return result;
    }

    void store_write_beat(
        std::uint64_t address,
        const std::array<std::uint32_t, 4>& data,
        std::uint32_t strobe
    ) {
        for (std::size_t byte = 0; byte < 16; ++byte) {
            if (((strobe >> byte) & 1U) != 0U) {
                const auto word = byte / sizeof(std::uint32_t);
                const auto lane = byte % sizeof(std::uint32_t);
                bytes_.at(address + byte) =
                    static_cast<std::uint8_t>((data[word] >> (lane * 8U)) & 0xFFU);
            }
        }
    }

    std::vector<std::uint8_t> bytes_;
    std::deque<Burst> pending_;
    bool active_ = false;
    Burst burst_{};
    bool write_active_ = false;
    bool write_response_pending_ = false;
    bool write_profile_valid_ = true;
    std::uint64_t write_address_ = 0;
    std::uint32_t write_size_ = 0;
    std::uint32_t write_beats_ = 0;
    std::uint32_t write_index_ = 0;
};

std::uint32_t axil_write(Vnpu_v2_top& dut, std::uint32_t address, std::uint32_t data) {
    dut.s_axil_awaddr_i = static_cast<std::uint16_t>(address);
    dut.s_axil_awvalid_i = 1;
    dut.s_axil_wdata_i = data;
    dut.s_axil_wstrb_i = 0xF;
    dut.s_axil_wvalid_i = 1;
    tick(dut);
    const auto response = static_cast<std::uint32_t>(dut.s_axil_bresp_o);
    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wvalid_i = 0;
    tick(dut);
    return response;
}

std::uint32_t axil_read(Vnpu_v2_top& dut, std::uint32_t address) {
    dut.s_axil_araddr_i = static_cast<std::uint16_t>(address);
    dut.s_axil_arvalid_i = 1;
    tick(dut);
    const auto data = static_cast<std::uint32_t>(dut.s_axil_rdata_o);
    dut.s_axil_arvalid_i = 0;
    tick(dut);
    return data;
}

std::uint32_t encode_system_exit() {
    return HOLON_NPU_ISA_CLASS_SYSTEM |
           ((HOLON_NPU_ISA_OPCODE_SYSTEM_EXIT & HOLON_NPU_ISA_FIELD_MASK)
            << HOLON_NPU_ISA_OPCODE_SHIFT);
}

holon_npu_program_desc_t descriptor() {
    return holon_npu_program_desc_t{
        .size_bytes = HOLON_NPU_PROGRAM_DESC_SIZE,
        .version = HOLON_NPU_V2_ABI_MAJOR,
        .program_format = HOLON_NPU_PROGRAM_FORMAT_HOLON_V2,
        .holon_isa_major = HOLON_NPU_ISA_MAJOR,
        .holon_isa_minor = HOLON_NPU_ISA_MINOR,
        .required_caps = HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
                         HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
        .required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        .code_addr = kProgramAddress,
        .code_size_bytes = 4,
        .entry_pc = 0,
        .arg_addr = 0,
        .arg_size_bytes = 0,
        .local_mem_bytes = 16,
        .program_mem_bytes = 4,
        .stack_bytes = 0,
        .completion_addr = kCompletionAddress,
        .flags = HOLON_NPU_PROGRAM_FLAG_IRQ_ON_DONE,
        .reserved_4c = 0,
        .reserved_50 = 0,
        .reserved_58 = 0,
        .reserved_60 = 0,
        .reserved_68 = 0,
        .reserved_70 = 0,
        .reserved_78 = 0,
    };
}

bool test_product_top(Vnpu_v2_top& dut) {
    reset(dut);
    AxiMemory memory(0x5000);
    const std::array program{encode_system_exit()};
    const auto desc = descriptor();
    memory.store_object(kDescriptorAddress, desc);
    memory.store_words(kProgramAddress, program);

    bool ok = true;
    ok &= expect_eq(
        "enable done IRQ",
        axil_write(dut, HOLON_NPU_V2_REG_IRQ_ENABLE, HOLON_NPU_V2_IRQ_DONE),
        kOkay
    );
    ok &= expect_eq(
        "descriptor low",
        axil_write(
            dut,
            HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO,
            static_cast<std::uint32_t>(kDescriptorAddress)
        ),
        kOkay
    );
    ok &= expect_eq(
        "doorbell",
        axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, HOLON_NPU_V2_DOORBELL_START),
        kOkay
    );

    bool completion_write_pending = false;
    for (int cycle = 0; cycle < 600; ++cycle) {
        memory.step(dut, false);
        if (memory.write_response_pending()) {
            completion_write_pending = true;
            break;
        }
    }
    ok &= expect_eq("completion write pending", completion_write_pending, true);
    ok &= expect_eq("completion AXI profile", memory.write_profile_valid(), true);
    ok &= expect_eq("IRQ held until completion response", dut.irq_o, 0U);
    ok &= expect_eq("lifecycle held until completion response",
                    axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_RUNNING);
    for (int cycle = 0; cycle < 3; ++cycle) {
        memory.step(dut, false);
        ok &= expect_eq("IRQ remains held", dut.irq_o, 0U);
    }

    bool completed = false;
    for (int cycle = 0; cycle < 16; ++cycle) {
        memory.step(dut, true);
        if (dut.irq_o) {
            completed = true;
            break;
        }
    }
    ok &= expect_eq("completion IRQ", completed, true);
    ok &= expect_eq(
        "status",
        axil_read(dut, HOLON_NPU_V2_REG_STATUS),
        HOLON_NPU_V2_STATUS_DONE | HOLON_NPU_V2_STATUS_IRQ_PENDING
    );
    ok &= expect_eq("debug PC", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 4U);
    ok &= expect_eq("instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 1U);
    const auto completion = memory.read_object<holon_npu_completion_record_t>(kCompletionAddress);
    ok &= expect_eq("completion ABI", completion.abi_version, HOLON_NPU_V2_ABI_VERSION_RESET);
    ok &= expect_eq("completion status", completion.status, HOLON_NPU_COMPLETION_STATUS_DONE);
    ok &= expect_eq("completion fault", completion.fault_code, HOLON_NPU_V2_FAULT_NONE);
    ok &= expect_eq("completion debug PC", completion.debug_pc, 4U);
    ok &= expect_eq("completion instret", completion.instret, 1U);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_v2_top", argc, argv};
    Vnpu_v2_top dut;
    const bool ok = test_product_top(dut);
    dut.final();
    test.cover({
        holon_npu_tb::coverage_point::v2_product_top_program,
        holon_npu_tb::coverage_point::v2_product_top_completion_ordering,
    });
    return test.finish(ok);
}
