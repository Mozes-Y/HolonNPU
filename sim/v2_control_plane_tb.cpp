#include "Vnpu_v2_control_plane.h"

#include "tb_coverage.hpp"

#include "holon_npu_program.h"

#include <algorithm>
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
constexpr std::uint32_t kRespOkay = 0;
constexpr std::uint64_t kDescAddr = 0x1000;

struct Burst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
    std::uint32_t size = 0;
    std::uint32_t index = 0;
};

void eval(Vnpu_v2_control_plane& dut) {
    dut.eval();
}

void tick(Vnpu_v2_control_plane& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_v2_control_plane& dut) {
    dut.s_axil_awaddr_i = 0;
    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wdata_i = 0;
    dut.s_axil_wstrb_i = 0;
    dut.s_axil_wvalid_i = 0;
    dut.s_axil_bready_i = 1;
    dut.s_axil_araddr_i = 0;
    dut.s_axil_arvalid_i = 0;
    dut.s_axil_rready_i = 1;

    dut.frontend_done_i = 0;
    dut.frontend_fault_i = 0;
    dut.frontend_fault_code_i = 0;
    dut.frontend_halted_i = 0;
    dut.frontend_debug_pc_i = 0;
    dut.frontend_instret_i = 0;

    dut.program_rd_valid_i = 0;
    dut.program_rd_addr_i = 0;
    dut.data_rd_valid_i = 0;
    dut.data_rd_addr_i = 0;

    dut.m_axi_arready_i = 0;
    dut.m_axi_rdata_lo_i = 0;
    dut.m_axi_rdata_hi_i = 0;
    dut.m_axi_rresp_i = kRespOkay;
    dut.m_axi_rlast_i = 0;
    dut.m_axi_rvalid_i = 0;
}

void idle_axi_memory_bus(Vnpu_v2_control_plane& dut) {
    dut.m_axi_arready_i = 0;
    dut.m_axi_rdata_lo_i = 0;
    dut.m_axi_rdata_hi_i = 0;
    dut.m_axi_rresp_i = kRespOkay;
    dut.m_axi_rlast_i = 0;
    dut.m_axi_rvalid_i = 0;
}

void reset(Vnpu_v2_control_plane& dut) {
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

holon_npu_program_desc_t valid_descriptor() {
    return holon_npu_program_desc_t{
        .size_bytes = HOLON_NPU_PROGRAM_DESC_SIZE,
        .version = HOLON_NPU_V2_ABI_MAJOR,
        .program_format = HOLON_NPU_PROGRAM_FORMAT_HOLON_V2,
        .holon_isa_major = HOLON_NPU_ISA_MAJOR,
        .holon_isa_minor = HOLON_NPU_ISA_MINOR,
        .required_caps = HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
                         HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
                         HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
                         HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE,
        .required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
                               HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        .code_addr = 0x2000,
        .code_size_bytes = 16,
        .entry_pc = 0,
        .arg_addr = 0x3000,
        .arg_size_bytes = 16,
        .local_mem_bytes = 128,
        .program_mem_bytes = 16,
        .stack_bytes = 0,
        .completion_addr = 0x4000,
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

class AxiMemory {
public:
    explicit AxiMemory(std::size_t byte_count) : memory_(byte_count) {}

    void store(std::uint64_t addr, const holon_npu_program_desc_t& desc) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&desc);
        for (std::size_t index = 0; index < sizeof(desc); ++index) {
            memory_.at(addr + index) = bytes[index];
        }
    }

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

    void step(Vnpu_v2_control_plane& dut) {
        dut.m_axi_arready_i = 1;

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
            dut.m_axi_rresp_i = kRespOkay;
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
        const auto ar_addr = static_cast<std::uint64_t>(dut.m_axi_araddr_o);
        const auto ar_beats = static_cast<std::uint32_t>(dut.m_axi_arlen_o) + 1U;
        const auto ar_size = static_cast<std::uint32_t>(dut.m_axi_arsize_o);
        tick(dut);

        if (ar_fire) {
            pending_.push_back(Burst{ar_addr, ar_beats, ar_size, 0});
        }
        if (r_fire && active_) {
            active_burst_.index += 1U;
            if (active_burst_.index == active_burst_.beats) {
                active_ = false;
            }
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

    std::vector<std::uint8_t> memory_;
    std::deque<Burst> pending_;
    bool active_ = false;
    Burst active_burst_{};
};

std::uint32_t axil_write(Vnpu_v2_control_plane& dut, std::uint32_t addr, std::uint32_t data) {
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

std::uint32_t axil_read(Vnpu_v2_control_plane& dut, std::uint32_t addr) {
    dut.s_axil_araddr_i = static_cast<std::uint16_t>(addr);
    dut.s_axil_arvalid_i = 1;
    dut.s_axil_rready_i = 1;
    tick(dut);

    const auto data = static_cast<std::uint32_t>(dut.s_axil_rdata_o);
    dut.s_axil_arvalid_i = 0;
    tick(dut);
    return data;
}

void frontend_done(Vnpu_v2_control_plane& dut, std::uint32_t pc, std::uint64_t instret) {
    dut.frontend_debug_pc_i = pc;
    dut.frontend_instret_i = instret;
    dut.frontend_done_i = 1;
    tick(dut);
    dut.frontend_done_i = 0;
    eval(dut);
}

struct ReadResult {
    bool valid = false;
    bool error = false;
    std::uint32_t data = 0;
};

ReadResult program_read(Vnpu_v2_control_plane& dut, std::uint32_t addr) {
    dut.program_rd_valid_i = 1;
    dut.program_rd_addr_i = addr;
    tick(dut);
    ReadResult result{
        .valid = dut.program_rd_valid_o != 0,
        .error = dut.program_rd_error_o != 0,
        .data = static_cast<std::uint32_t>(dut.program_rd_data_o),
    };
    dut.program_rd_valid_i = 0;
    tick(dut);
    return result;
}

ReadResult data_read(Vnpu_v2_control_plane& dut, std::uint32_t addr) {
    dut.data_rd_valid_i = 1;
    dut.data_rd_addr_i = addr;
    for (int cycle = 0; cycle < 4; ++cycle) {
        eval(dut);
        if (dut.data_rd_ready_o) {
            break;
        }
        tick(dut);
    }
    tick(dut);
    ReadResult result{
        .valid = dut.data_rd_valid_o != 0,
        .error = dut.data_rd_error_o != 0,
        .data = static_cast<std::uint32_t>(dut.data_rd_data_o),
    };
    dut.data_rd_valid_i = 0;
    tick(dut);
    return result;
}

bool expect_local_words(
    Vnpu_v2_control_plane& dut,
    std::string_view name,
    std::span<const std::uint32_t> expected_words,
    bool program_space
) {
    bool ok = true;
    for (std::size_t index = 0; index < expected_words.size(); ++index) {
        const auto addr = static_cast<std::uint32_t>(index * 4U);
        const auto result = program_space ? program_read(dut, addr) : data_read(dut, addr);
        ok &= expect_eq(name, result.valid, true);
        ok &= expect_eq("local read error", result.error, false);
        ok &= expect_eq("local word", result.data, expected_words[index]);
    }
    return ok;
}

bool run_until_loader_terminal(Vnpu_v2_control_plane& dut, AxiMemory& memory, int max_cycles = 160) {
    bool saw_loader_busy = false;
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        saw_loader_busy = saw_loader_busy || (dut.loader_busy_o != 0);
        if (saw_loader_busy && (dut.loader_done_o || dut.loader_fault_o)) {
            memory.step(dut);
            return true;
        }
        memory.step(dut);
    }
    return saw_loader_busy && (dut.loader_done_o || dut.loader_fault_o);
}

bool test_valid_program_launch(Vnpu_v2_control_plane& dut) {
    reset(dut);
    AxiMemory memory(0x8000);
    const auto desc = valid_descriptor();
    const std::array<std::uint32_t, 4> program_words{
        0x1010'1010U,
        0x2020'2020U,
        0x3030'3030U,
        0x4040'4040U,
    };
    const std::array<std::uint32_t, 4> arg_words{
        0xAAAA'1001U,
        0xBBBB'1002U,
        0xCCCC'1003U,
        0xDDDD'1004U,
    };
    memory.store(kDescAddr, desc);
    memory.store_words(desc.code_addr, program_words);
    memory.store_words(desc.arg_addr, arg_words);
    idle_axi_memory_bus(dut);

    bool ok = true;
    ok &= expect_eq("IRQ enable", axil_write(dut, HOLON_NPU_V2_REG_IRQ_ENABLE, HOLON_NPU_V2_IRQ_VALID_MASK), kOkay);
    ok &= expect_eq("desc lo", axil_write(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO, static_cast<std::uint32_t>(kDescAddr)), kOkay);
    ok &= expect_eq("doorbell", axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, 1), kOkay);
    ok &= expect_eq("status loading", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_LOADING);

    ok &= expect_eq("loader terminal", run_until_loader_terminal(dut, memory), true);
    ok &= expect_eq("loader done", dut.loader_done_o, 1);
    ok &= expect_eq("loader fault", dut.loader_fault_o, 0);
    ok &= expect_eq("status running", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_RUNNING);
    ok &= expect_local_words(dut, "program readback", program_words, true);
    ok &= expect_local_words(dut, "argument readback", arg_words, false);
    ok &= expect_eq("entry pc", dut.entry_pc_o, desc.entry_pc);
    ok &= expect_eq("program format", dut.program_format_o, desc.program_format);

    frontend_done(dut, 0x40, 9);
    ok &= expect_eq("status done irq", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_DONE | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    ok &= expect_eq("debug pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 0x40U);
    ok &= expect_eq("instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 9U);
    ok &= expect_eq("clear IRQ", axil_write(dut, HOLON_NPU_V2_REG_IRQ_CLEAR, HOLON_NPU_V2_IRQ_VALID_MASK), kOkay);
    ok &= expect_eq("clear terminal", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_CLEAR_TERMINAL), kOkay);
    ok &= expect_eq("status idle", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_IDLE);
    idle_axi_memory_bus(dut);
    ok &= expect_eq("restart doorbell", axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, 1), kOkay);
    ok &= expect_eq("restart terminal", run_until_loader_terminal(dut, memory), true);
    ok &= expect_eq("restart status running", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_RUNNING);
    ok &= expect_local_words(dut, "restart program readback", program_words, true);
    ok &= expect_local_words(dut, "restart argument readback", arg_words, false);
    return ok;
}

bool test_loader_fault_propagates(Vnpu_v2_control_plane& dut) {
    reset(dut);
    AxiMemory memory(0x8000);
    auto desc = valid_descriptor();
    desc.version = 2;
    memory.store(kDescAddr, desc);
    idle_axi_memory_bus(dut);

    bool ok = true;
    ok &= expect_eq("desc lo", axil_write(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO, static_cast<std::uint32_t>(kDescAddr)), kOkay);
    ok &= expect_eq("doorbell", axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, 1), kOkay);
    ok &= expect_eq("loader terminal", run_until_loader_terminal(dut, memory), true);
    ok &= expect_eq("loader fault", dut.loader_fault_o, 1);
    ok &= expect_eq("loader fault code", dut.loader_fault_code_o, HOLON_NPU_V2_FAULT_UNSUPPORTED_ABI_OR_ISA);
    ok &= expect_eq("status fault", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_FAULT);
    ok &= expect_eq("fault code", axil_read(dut, HOLON_NPU_V2_REG_FAULT_CODE),
                    HOLON_NPU_V2_FAULT_UNSUPPORTED_ABI_OR_ISA);
    const auto program = program_read(dut, 0);
    const auto data = data_read(dut, 0);
    ok &= expect_eq("program memory read still responds", program.valid, true);
    ok &= expect_eq("data memory read still responds", data.valid, true);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_v2_control_plane", argc, argv};

    Vnpu_v2_control_plane dut;
    bool ok = true;

    ok &= test_valid_program_launch(dut);
    ok &= test_loader_fault_propagates(dut);

    dut.final();
    using enum holon_npu_tb::coverage_point;
    test.cover({
        v2_control_loader_integration,
        v2_control_local_memory_integration,
        v2_loader_program_image_copy,
        v2_loader_argument_copy,
        descriptor_invalid_version,
    });
    return test.finish(ok);
}
