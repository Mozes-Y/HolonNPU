#include "Vnpu_v2_frontend_tile.h"

#include "tb_coverage.hpp"

#include "holon_npu_isa.h"
#include "holon_npu_program.h"
#include "holon_npu_v2_model.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <bit>
#include <iostream>
#include <random>
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

std::uint32_t encode_system(std::uint32_t opcode, std::uint32_t imm) {
    return HOLON_NPU_ISA_CLASS_SYSTEM |
           ((opcode & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_OPCODE_SHIFT) |
           (imm & HOLON_NPU_ISA_IMM_MASK);
}

std::uint32_t encode_system_exit() {
    return encode_system(HOLON_NPU_ISA_OPCODE_SYSTEM_EXIT, 0);
}

std::uint32_t encode_system_fault(std::uint32_t fault_code) {
    return encode_system(HOLON_NPU_ISA_OPCODE_SYSTEM_FAULT, fault_code);
}

void eval(Vnpu_v2_frontend_tile& dut) {
    dut.eval();
}

void tick(Vnpu_v2_frontend_tile& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_v2_frontend_tile& dut) {
    dut.s_axil_awaddr_i = 0;
    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wdata_i = 0;
    dut.s_axil_wstrb_i = 0;
    dut.s_axil_wvalid_i = 0;
    dut.s_axil_bready_i = 1;
    dut.s_axil_araddr_i = 0;
    dut.s_axil_arvalid_i = 0;
    dut.s_axil_rready_i = 1;

    dut.data_rd_valid_i = 0;
    dut.data_rd_addr_i = 0;

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

void idle_axi_memory_bus(Vnpu_v2_frontend_tile& dut) {
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

void reset(Vnpu_v2_frontend_tile& dut) {
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

    template <typename T>
    T read_object(std::uint64_t addr) const {
        T object{};
        auto* bytes = reinterpret_cast<std::uint8_t*>(&object);
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            bytes[index] = memory_.at(addr + index);
        }
        return object;
    }

    void set_next_write_response(std::uint32_t response) {
        next_write_resp_ = response;
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

    void step(Vnpu_v2_frontend_tile& dut) {
        dut.m_axi_arready_i = 1;
        dut.m_axi_awready_i = 1;
        dut.m_axi_wready_i = 1;
        dut.m_axi_bvalid_i = pending_b_valid_ ? 1 : 0;
        dut.m_axi_bresp_i = pending_b_valid_ ? pending_b_resp_ : kRespOkay;

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
        const bool aw_fire = dut.m_axi_awvalid_o && dut.m_axi_awready_i;
        const bool w_fire = dut.m_axi_wvalid_o && dut.m_axi_wready_i;
        const bool b_fire = dut.m_axi_bvalid_i && dut.m_axi_bready_o;
        const auto ar_addr = static_cast<std::uint64_t>(dut.m_axi_araddr_o);
        const auto ar_beats = static_cast<std::uint32_t>(dut.m_axi_arlen_o) + 1U;
        const auto ar_size = static_cast<std::uint32_t>(dut.m_axi_arsize_o);
        const auto aw_addr = static_cast<std::uint64_t>(dut.m_axi_awaddr_o);
        const auto aw_size = static_cast<std::uint32_t>(dut.m_axi_awsize_o);
        const auto wdata_lo = static_cast<std::uint64_t>(dut.m_axi_wdata_lo_o);
        const auto wdata_hi = static_cast<std::uint64_t>(dut.m_axi_wdata_hi_o);
        const auto wstrb = static_cast<std::uint32_t>(dut.m_axi_wstrb_o);
        const bool wlast = dut.m_axi_wlast_o != 0;
        tick(dut);

        if (ar_fire) {
            pending_.push_back(Burst{ar_addr, ar_beats, ar_size, 0});
        }
        if (aw_fire) {
            write_active_ = true;
            write_addr_ = aw_addr;
            write_size_ = aw_size;
            write_index_ = 0;
        }
        if (w_fire && write_active_) {
            const auto beat_bytes = std::uint64_t{1} << write_size_;
            const auto beat_addr = write_addr_ + (static_cast<std::uint64_t>(write_index_) * beat_bytes);
            store_bus_word(beat_addr & ~std::uint64_t{0x0F}, wdata_lo, wdata_hi, wstrb);
            write_index_ += 1U;
            if (wlast) {
                write_active_ = false;
                pending_b_valid_ = true;
                pending_b_resp_ = next_write_resp_;
                next_write_resp_ = kRespOkay;
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
    std::deque<Burst> pending_;
    bool active_ = false;
    Burst active_burst_{};
    bool write_active_ = false;
    std::uint64_t write_addr_ = 0;
    std::uint32_t write_size_ = 0;
    std::uint32_t write_index_ = 0;
    bool pending_b_valid_ = false;
    std::uint32_t pending_b_resp_ = kRespOkay;
    std::uint32_t next_write_resp_ = kRespOkay;
};

holon_npu_program_desc_t descriptor_for(
    std::span<const std::uint32_t> program_words,
    std::uint64_t required_caps = HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
                                  HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
    std::uint64_t required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
    std::uint32_t local_mem_bytes = 16,
    std::span<const std::uint32_t> argument_words = {},
    std::uint64_t completion_addr = 0
) {
    return holon_npu_program_desc_t{
        .size_bytes = HOLON_NPU_PROGRAM_DESC_SIZE,
        .version = HOLON_NPU_V2_ABI_MAJOR,
        .program_format = HOLON_NPU_PROGRAM_FORMAT_HOLON_V2,
        .holon_isa_major = HOLON_NPU_ISA_MAJOR,
        .holon_isa_minor = HOLON_NPU_ISA_MINOR,
        .required_caps = required_caps,
        .required_op_classes = required_op_classes,
        .code_addr = 0x2000,
        .code_size_bytes = static_cast<std::uint32_t>(program_words.size_bytes()),
        .entry_pc = 0,
        .arg_addr = argument_words.empty() ? 0u : 0x3000u,
        .arg_size_bytes = static_cast<std::uint32_t>(argument_words.size_bytes()),
        .local_mem_bytes = local_mem_bytes,
        .program_mem_bytes = static_cast<std::uint32_t>(program_words.size_bytes()),
        .stack_bytes = 0,
        .completion_addr = completion_addr,
        .flags = HOLON_NPU_PROGRAM_FLAG_IRQ_ON_DONE |
                 HOLON_NPU_PROGRAM_FLAG_IRQ_ON_FAULT |
                 HOLON_NPU_PROGRAM_FLAG_DEBUG_SNAPSHOT_ON_FAULT,
        .reserved_4c = 0,
        .reserved_50 = 0,
        .reserved_58 = 0,
        .reserved_60 = 0,
        .reserved_68 = 0,
        .reserved_70 = 0,
        .reserved_78 = 0,
    };
}

std::uint32_t axil_write(Vnpu_v2_frontend_tile& dut, std::uint32_t addr, std::uint32_t data) {
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

std::uint32_t axil_read(Vnpu_v2_frontend_tile& dut, std::uint32_t addr) {
    dut.s_axil_araddr_i = static_cast<std::uint16_t>(addr);
    dut.s_axil_arvalid_i = 1;
    dut.s_axil_rready_i = 1;
    tick(dut);

    const auto data = static_cast<std::uint32_t>(dut.s_axil_rdata_o);
    dut.s_axil_arvalid_i = 0;
    tick(dut);
    return data;
}

struct LocalReadResult {
    bool valid = false;
    bool error = false;
    std::uint32_t data = 0;
};

LocalReadResult data_read(Vnpu_v2_frontend_tile& dut, std::uint32_t addr) {
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
    LocalReadResult result{
        .valid = dut.data_rd_valid_o != 0,
        .error = dut.data_rd_error_o != 0,
        .data = static_cast<std::uint32_t>(dut.data_rd_data_o),
    };
    dut.data_rd_valid_i = 0;
    tick(dut);
    return result;
}

bool run_until_frontend_terminal(
    Vnpu_v2_frontend_tile& dut,
    AxiMemory& memory,
    int max_cycles = 2000
) {
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        memory.step(dut);
        if (dut.irq_o || dut.loader_fault_o) {
            memory.step(dut);
            memory.step(dut);
            return true;
        }
    }
    std::cerr << "frontend terminal timeout: loader_busy=" << static_cast<int>(dut.loader_busy_o)
              << " loader_done=" << static_cast<int>(dut.loader_done_o)
              << " loader_fault=" << static_cast<int>(dut.loader_fault_o)
              << " frontend_running=" << static_cast<int>(dut.frontend_running_o)
              << " frontend_done=" << static_cast<int>(dut.frontend_done_o)
              << " frontend_fault=" << static_cast<int>(dut.frontend_fault_o)
              << " arvalid=" << static_cast<int>(dut.m_axi_arvalid_o)
              << " rready=" << static_cast<int>(dut.m_axi_rready_o)
              << '\n';
    return dut.irq_o || dut.loader_fault_o;
}

bool launch_program(
    Vnpu_v2_frontend_tile& dut,
    AxiMemory& memory,
    std::span<const std::uint32_t> program_words,
    std::uint64_t required_caps = HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
                                  HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
    std::uint64_t required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
    std::uint32_t local_mem_bytes = 16,
    std::span<const std::uint32_t> argument_words = {},
    std::uint64_t completion_addr = 0,
    int max_cycles = 2000
) {
    const auto desc = descriptor_for(
        program_words,
        required_caps,
        required_op_classes,
        local_mem_bytes,
        argument_words,
        completion_addr
    );
    memory.store(kDescAddr, desc);
    memory.store_words(desc.code_addr, program_words);
    if (!argument_words.empty()) {
        memory.store_words(desc.arg_addr, argument_words);
    }
    idle_axi_memory_bus(dut);

    bool ok = true;
    ok &= expect_eq("IRQ enable", axil_write(dut, HOLON_NPU_V2_REG_IRQ_ENABLE, HOLON_NPU_V2_IRQ_VALID_MASK), kOkay);
    ok &= expect_eq("desc lo", axil_write(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO, static_cast<std::uint32_t>(kDescAddr)), kOkay);
    ok &= expect_eq("doorbell", axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, 1), kOkay);
    ok &= expect_eq("status loading", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_LOADING);
    ok &= expect_eq("terminal", run_until_frontend_terminal(dut, memory, max_cycles), true);
    ok &= expect_eq("loader done", dut.loader_done_o, 1);
    ok &= expect_eq("loader fault", dut.loader_fault_o, 0);
    return ok;
}

bool start_program(
    Vnpu_v2_frontend_tile& dut,
    AxiMemory& memory,
    std::span<const std::uint32_t> program_words,
    std::uint64_t required_caps,
    std::uint64_t required_op_classes,
    std::uint32_t local_mem_bytes
) {
    const auto desc = descriptor_for(
        program_words,
        required_caps,
        required_op_classes,
        local_mem_bytes
    );
    memory.store(kDescAddr, desc);
    memory.store_words(desc.code_addr, program_words);
    idle_axi_memory_bus(dut);

    bool ok = true;
    ok &= expect_eq("debug IRQ enable", axil_write(dut, HOLON_NPU_V2_REG_IRQ_ENABLE, HOLON_NPU_V2_IRQ_VALID_MASK), kOkay);
    ok &= expect_eq("debug desc lo", axil_write(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO, static_cast<std::uint32_t>(kDescAddr)), kOkay);
    ok &= expect_eq("debug doorbell", axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, 1), kOkay);
    ok &= expect_eq("debug status loading", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_LOADING);
    return ok;
}

bool test_system_exit(Vnpu_v2_frontend_tile& dut) {
    reset(dut);
    AxiMemory memory(0x8000);
    const std::array<std::uint32_t, 1> program{encode_system_exit()};

    bool ok = launch_program(dut, memory, program);
    ok &= expect_eq("frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("frontend fault", dut.frontend_fault_o, 0);
    ok &= expect_eq("status done irq", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_DONE | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    ok &= expect_eq("debug pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 4U);
    ok &= expect_eq("instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 1U);
    ok &= expect_eq("clear IRQ", axil_write(dut, HOLON_NPU_V2_REG_IRQ_CLEAR, HOLON_NPU_V2_IRQ_VALID_MASK), kOkay);
    ok &= expect_eq("clear terminal", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_CLEAR_TERMINAL), kOkay);
    ok &= expect_eq("status idle", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_IDLE);
    ok &= launch_program(dut, memory, program);
    ok &= expect_eq("restart done", dut.frontend_done_o, 1);
    ok &= expect_eq("restart debug pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 4U);
    return ok;
}

bool test_system_fault(Vnpu_v2_frontend_tile& dut) {
    reset(dut);
    AxiMemory memory(0x8000);
    const std::array<std::uint32_t, 1> program{
        encode_system_fault(HOLON_NPU_V2_FAULT_EXPLICIT_PROGRAM_FAULT),
    };

    bool ok = launch_program(dut, memory, program);
    ok &= expect_eq("frontend fault", dut.frontend_fault_o, 1);
    ok &= expect_eq("frontend fault code", dut.frontend_fault_code_o, HOLON_NPU_V2_FAULT_EXPLICIT_PROGRAM_FAULT);
    ok &= expect_eq("status fault irq", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_FAULT | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    ok &= expect_eq("fault code", axil_read(dut, HOLON_NPU_V2_REG_FAULT_CODE),
                    HOLON_NPU_V2_FAULT_EXPLICIT_PROGRAM_FAULT);
    ok &= expect_eq("fault pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 0U);
    ok &= expect_eq("fault instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 0U);
    return ok;
}

bool test_completion_record(Vnpu_v2_frontend_tile& dut) {
    constexpr std::uint64_t completion_addr = 0x4000;
    bool ok = true;
    {
        reset(dut);
        AxiMemory memory(0x8000);
        const std::array<std::uint32_t, 1> program{encode_system_exit()};
        ok &= launch_program(
            dut,
            memory,
            program,
            HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
                HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
            16,
            {},
            completion_addr
        );
        const auto record = memory.read_object<holon_npu_completion_record_t>(completion_addr);
        ok &= expect_eq("completion ABI", record.abi_version, HOLON_NPU_V2_ABI_VERSION_RESET);
        ok &= expect_eq("completion done status", record.status, HOLON_NPU_COMPLETION_STATUS_DONE);
        ok &= expect_eq("completion done fault", record.fault_code, HOLON_NPU_V2_FAULT_NONE);
        ok &= expect_eq("completion done PC", record.debug_pc, 4U);
        ok &= expect_eq("completion done instret", record.instret, 1U);
        ok &= expect_eq("completion cycle nonzero", record.cycle_count != 0, true);
        ok &= expect_eq("completion published status", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                        HOLON_NPU_V2_STATUS_DONE | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    }
    {
        reset(dut);
        AxiMemory memory(0x8000);
        const std::array<std::uint32_t, 1> program{
            encode_system_fault(HOLON_NPU_V2_FAULT_EXPLICIT_PROGRAM_FAULT),
        };
        ok &= launch_program(
            dut,
            memory,
            program,
            HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
                HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
            16,
            {},
            completion_addr
        );
        const auto record = memory.read_object<holon_npu_completion_record_t>(completion_addr);
        ok &= expect_eq("completion fault status", record.status, HOLON_NPU_COMPLETION_STATUS_FAULT);
        ok &= expect_eq("completion fault code", record.fault_code,
                        HOLON_NPU_V2_FAULT_EXPLICIT_PROGRAM_FAULT);
        ok &= expect_eq("completion fault PC", record.debug_pc, 0U);
        ok &= expect_eq("completion fault instret", record.instret, 0U);
        ok &= expect_eq("completion fault published", axil_read(dut, HOLON_NPU_V2_REG_FAULT_CODE),
                        HOLON_NPU_V2_FAULT_EXPLICIT_PROGRAM_FAULT);
    }
    {
        reset(dut);
        AxiMemory memory(0x8000);
        memory.set_next_write_response(2U);
        const std::array<std::uint32_t, 1> program{encode_system_exit()};
        ok &= launch_program(
            dut,
            memory,
            program,
            HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
                HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
            16,
            {},
            completion_addr
        );
        ok &= expect_eq("completion write fault status", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                        HOLON_NPU_V2_STATUS_FAULT | HOLON_NPU_V2_STATUS_IRQ_PENDING);
        ok &= expect_eq("completion write fault code", axil_read(dut, HOLON_NPU_V2_REG_FAULT_CODE),
                        HOLON_NPU_V2_FAULT_AXI_WRITE);
    }
    return ok;
}

bool test_scalar_control_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::lifecycle_state;
    using holon_npu::v2::model::machine;
    using holon_npu::v2::model::program_builder;

    reset(dut);
    AxiMemory memory(0x8000);
    program_builder program;
    program.movi(1, 3)
        .movi(2, 0)
        .movi(3, 2)
        .scalar_add(2, 2, 3)
        .scalar_addi(1, 1, -1)
        .bne(1, 0, -2)
        .scalar_store(2, 0, 0)
        .scalar_load(4, 0, 0)
        .beq(2, 4, 2)
        .fault(holon_npu::v2::model::model_error::explicit_program_fault)
        .exit();

    machine reference(64, 16);
    reference.load_program(program.span());
    bool ok = expect_eq("reference scalar done", reference.run(32).state == lifecycle_state::done, true);
    const auto expected = reference.read_i32(0, 1).at(0);
    ok &= launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
        HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        64
    );
    ok &= expect_eq("Holon frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("Holon frontend fault", dut.frontend_fault_o, 0);
    ok &= expect_eq("scalar program instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 16U);
    const auto result = data_read(dut, 0);
    ok &= expect_eq("scalar result valid", result.valid, true);
    ok &= expect_eq("scalar result error", result.error, false);
    ok &= expect_eq("scalar result", result.data, std::bit_cast<std::uint32_t>(expected));
    return ok;
}

bool test_csr_read_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::csr;
    using holon_npu::v2::model::program_builder;

    reset(dut);
    AxiMemory memory(0x8000);
    program_builder program;
    program.csr_read(1, csr::pc)
        .scalar_store(1, 0, 0)
        .csr_read(2, csr::instret_lo)
        .scalar_store(2, 0, 4)
        .csr_read(3, csr::program_size_bytes)
        .scalar_store(3, 0, 8)
        .csr_read(4, csr::local_mem_bytes)
        .scalar_store(4, 0, 12)
        .exit();

    bool ok = launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
        HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
            HOLON_NPU_PROGRAM_OP_CLASS_CSR_DEBUG |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        64
    );
    ok &= expect_eq("CSR frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("CSR frontend fault", dut.frontend_fault_o, 0);
    const std::array<std::uint32_t, 4> expected{
        0,
        2,
        static_cast<std::uint32_t>(program.size() * HOLON_NPU_ISA_INSTRUCTION_BYTES),
        64,
    };
    for (std::uint32_t index = 0; index < expected.size(); ++index) {
        const auto result = data_read(dut, index * 4U);
        ok &= expect_eq("CSR result valid", result.valid, true);
        ok &= expect_eq("CSR result error", result.error, false);
        ok &= expect_eq("CSR result", result.data, expected[index]);
    }
    return ok;
}

bool test_scalar_bounds_fault(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::program_builder;

    reset(dut);
    AxiMemory memory(0x8000);
    program_builder program;
    program.movi(1, 63).scalar_load(2, 1, 0).exit();
    bool ok = launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
        HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        64
    );
    ok &= expect_eq("scalar bounds terminal", dut.frontend_fault_o, 1);
    ok &= expect_eq("scalar bounds code", dut.frontend_fault_code_o, HOLON_NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);
    ok &= expect_eq("scalar bounds pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 4U);
    ok &= expect_eq("scalar bounds instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 1U);
    return ok;
}

bool test_debug_step(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::program_builder;

    reset(dut);
    AxiMemory memory(0x8000);
    program_builder program;
    program.movi(1, 1);
    for (int instruction = 0; instruction < 64; ++instruction) {
        program.scalar_addi(0, 0, 0);
    }
    program.exit();

    bool ok = start_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY,
        HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        64
    );
    bool saw_running = false;
    for (int cycle = 0; cycle < 160 && !saw_running; ++cycle) {
        memory.step(dut);
        saw_running = dut.frontend_running_o != 0;
    }
    ok &= expect_eq("debug saw running", saw_running, true);
    ok &= expect_eq("halt request", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_HALT), kOkay);

    bool saw_halted = false;
    for (int cycle = 0; cycle < 32 && !saw_halted; ++cycle) {
        memory.step(dut);
        saw_halted = dut.frontend_halted_o != 0;
    }
    ok &= expect_eq("debug saw halted", saw_halted, true);
    memory.step(dut);
    ok &= expect_eq("debug halted status", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_HALTED | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    const auto pc_before = axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC);
    const auto instret_before = axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO);

    ok &= expect_eq("debug step request",
                    axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_DEBUG_STEP), kOkay);
    bool step_completed = false;
    bool left_halted = false;
    for (int cycle = 0; cycle < 32 && !step_completed; ++cycle) {
        memory.step(dut);
        left_halted = left_halted || (dut.frontend_halted_o == 0);
        step_completed = left_halted && (dut.frontend_halted_o != 0);
    }
    ok &= expect_eq("debug step completed", step_completed, true);
    memory.step(dut);
    const auto pc_after = axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC);
    const auto instret_after = axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO);
    ok &= expect_eq("debug step pc delta", pc_after, pc_before + HOLON_NPU_ISA_INSTRUCTION_BYTES);
    ok &= expect_eq("debug step instret delta", instret_after, instret_before + 1U);

    ok &= expect_eq("clear debug IRQ",
                    axil_write(dut, HOLON_NPU_V2_REG_IRQ_CLEAR,
                               HOLON_NPU_V2_IRQ_HALTED | HOLON_NPU_V2_IRQ_DEBUG_STEP),
                    kOkay);
    ok &= expect_eq("debug IRQ cleared", axil_read(dut, HOLON_NPU_V2_REG_IRQ_STATUS), 0U);
    ok &= expect_eq("resume request", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_RESUME), kOkay);
    ok &= expect_eq("debug terminal", run_until_frontend_terminal(dut, memory, 1000), true);
    ok &= expect_eq("debug resumed status", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_DONE | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    ok &= expect_eq("debug resumed done", dut.frontend_done_o, 1);
    ok &= expect_eq("debug resumed fault", dut.frontend_fault_o, 0);
    return ok;
}

bool test_illegal_instruction(Vnpu_v2_frontend_tile& dut) {
    reset(dut);
    AxiMemory memory(0x8000);
    const std::array<std::uint32_t, 1> program{HOLON_NPU_ISA_CLASS_RESERVED_D};

    bool ok = launch_program(dut, memory, program);
    ok &= expect_eq("frontend fault", dut.frontend_fault_o, 1);
    ok &= expect_eq("illegal fault code", dut.frontend_fault_code_o, HOLON_NPU_V2_FAULT_ILLEGAL_INSTRUCTION);
    ok &= expect_eq("fault code", axil_read(dut, HOLON_NPU_V2_REG_FAULT_CODE),
                    HOLON_NPU_V2_FAULT_ILLEGAL_INSTRUCTION);
    ok &= expect_eq("illegal pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 0U);
    return ok;
}

bool test_dma_load_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::program_builder;

    reset(dut);
    AxiMemory memory(0x8000);
    const std::uint32_t system_addr = 0x5000;
    const std::array<std::uint32_t, 2> source_words{
        0x5151'0001U,
        0x6262'0002U,
    };
    const std::array<std::uint32_t, 4> argument_words{
        system_addr,
        0,
        0,
        0,
    };
    program_builder program;
    program.scalar_load(1, 0, 0)
        .scalar_load(2, 0, 4)
        .movi(3, 16)
        .dma_load(1, 2, 3, static_cast<std::uint16_t>(source_words.size()))
        .exit();
    memory.store_words(system_addr, source_words);

    bool ok = launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
            HOLON_NPU_V2_CAP_IN_ORDER_DMA_QUEUE,
        HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
            HOLON_NPU_PROGRAM_OP_CLASS_DMA |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        64,
        argument_words
    );
    ok &= expect_eq("frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("frontend fault", dut.frontend_fault_o, 0);
    ok &= expect_eq("dma program status", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_DONE | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    ok &= expect_eq("dma program pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 20U);
    ok &= expect_eq("dma program instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 5U);

    const auto first = data_read(dut, 16);
    const auto second = data_read(dut, 20);
    ok &= expect_eq("dma read first valid", first.valid, true);
    ok &= expect_eq("dma read first error", first.error, false);
    ok &= expect_eq("dma read first data", first.data, source_words[0]);
    ok &= expect_eq("dma read second valid", second.valid, true);
    ok &= expect_eq("dma read second error", second.error, false);
    ok &= expect_eq("dma read second data", second.data, source_words[1]);
    return ok;
}

bool test_dma_store_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::program_builder;

    reset(dut);
    AxiMemory memory(0x8000);
    const std::uint32_t system_addr = 0x6000;
    const std::array<std::uint32_t, 2> source_words{
        0x7373'0003U,
        0x8484'0004U,
    };
    const std::array<std::uint32_t, 4> argument_words{
        system_addr,
        0,
        source_words[0],
        source_words[1],
    };
    program_builder program;
    program.scalar_load(1, 0, 0)
        .scalar_load(2, 0, 4)
        .movi(3, 8)
        .dma_store(1, 2, 3, static_cast<std::uint16_t>(source_words.size()))
        .exit();

    bool ok = launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
            HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
            HOLON_NPU_V2_CAP_IN_ORDER_DMA_QUEUE,
        HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
            HOLON_NPU_PROGRAM_OP_CLASS_DMA |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        64,
        argument_words
    );
    ok &= expect_eq("store frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("store frontend fault", dut.frontend_fault_o, 0);
    ok &= expect_eq("store program pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 20U);
    ok &= expect_eq("store program instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 5U);
    const auto stored = memory.read_words(system_addr, source_words.size());
    ok &= expect_eq("store result 0", stored.at(0), source_words[0]);
    ok &= expect_eq("store result 1", stored.at(1), source_words[1]);
    return ok;
}

bool test_sync_order_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::program_builder;

    reset(dut);
    AxiMemory memory(0x8000);
    constexpr std::uint32_t source_addr = 0x5000;
    constexpr std::uint32_t store_addr = 0x6000;
    const std::array<std::uint32_t, 2> source_words{
        0x5151'1001U,
        0x6262'2002U,
    };
    const std::array<std::uint32_t, 4> argument_words{
        source_addr,
        0,
        store_addr,
        0,
    };
    program_builder program;
    program.scalar_load(1, 0, 0)
        .scalar_load(2, 0, 4)
        .movi(3, 16)
        .dma_load(1, 2, 3, static_cast<std::uint16_t>(source_words.size()))
        .wait_dma()
        .fence_local()
        .scalar_load(1, 0, 8)
        .scalar_load(2, 0, 12)
        .dma_store(1, 2, 3, static_cast<std::uint16_t>(source_words.size()))
        .fence_dma()
        .exit();

    memory.store_words(source_addr, source_words);
    bool ok = launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
            HOLON_NPU_V2_CAP_IN_ORDER_DMA_QUEUE,
        HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
            HOLON_NPU_PROGRAM_OP_CLASS_DMA |
            HOLON_NPU_PROGRAM_OP_CLASS_SYNC |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        64,
        argument_words
    );
    ok &= expect_eq("sync frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("sync frontend fault", dut.frontend_fault_o, 0);
    ok &= expect_eq("sync program pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 44U);
    ok &= expect_eq("sync program instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 11U);
    const auto stored = memory.read_words(store_addr, source_words.size());
    ok &= expect_eq("sync store result 0", stored.at(0), source_words[0]);
    ok &= expect_eq("sync store result 1", stored.at(1), source_words[1]);
    return ok;
}

bool test_vector_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::lifecycle_state;
    using holon_npu::v2::model::machine;
    using holon_npu::v2::model::program_builder;

    reset(dut);
    AxiMemory memory(0x8000);
    const std::array<std::int32_t, 8> operands{
        1, -5, 0x7FFF'FFFF, -16,
        2, 7, 1, 2,
    };
    std::array<std::uint32_t, 16> argument_words{};
    for (std::size_t index = 0; index < operands.size(); ++index) {
        argument_words[index] = std::bit_cast<std::uint32_t>(operands[index]);
    }
    argument_words[8] = 100;
    argument_words[9] = 200;
    argument_words[10] = 300;
    argument_words[11] = 400;
    argument_words[12] = 0x5;

    program_builder program;
    program.configure(4, holon_npu::v2::model::vector_element_width::bits_32, true)
        .load(1, 0)
        .load(2, 16)
        .load(3, 32)
        .predicate_load(0, 48)
        .add(3, 1, 2, 0)
        .store(3, 32, 0)
        .exit();

    machine reference(64, 16);
    reference.load_program(program.span());
    const auto argument_bytes = std::as_bytes(std::span{argument_words});
    bool ok = reference.load_arguments(argument_bytes, 0);
    const auto reference_result = reference.run(32);
    ok &= expect_eq("reference vector done", reference_result.state == lifecycle_state::done, true);
    const auto expected = reference.read_i32(32, 4);

    ok &= launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
            HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
            HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE,
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
            HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        64,
        argument_words
    );
    ok &= expect_eq("vector frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("vector frontend fault", dut.frontend_fault_o, 0);
    ok &= expect_eq("vector program pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 32U);
    ok &= expect_eq("vector program instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 8U);
    for (std::uint32_t lane = 0; lane < expected.size(); ++lane) {
        const auto result = data_read(dut, 32U + lane * 4U);
        ok &= expect_eq("vector result valid", result.valid, true);
        ok &= expect_eq("vector result error", result.error, false);
        ok &= expect_eq(
            "vector result lane",
            result.data,
            std::bit_cast<std::uint32_t>(expected[lane])
        );
    }
    return ok;
}

bool test_vector_helper_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::lifecycle_state;
    using holon_npu::v2::model::machine;
    using holon_npu::v2::model::program_builder;
    using holon_npu::v2::model::vector_element_width;

    constexpr auto command_offset = std::uint16_t{64};
    reset(dut);
    AxiMemory memory(0x8000);
    std::array<std::uint32_t, 64> argument_words{};
    const std::array<std::int32_t, 4> lhs{3, 5, -3, 100};
    const std::array<std::int32_t, 4> rhs{-1, -2, -3, -4};
    const std::array<std::int32_t, 4> indices{3, 0, 2, 1};
    const std::array<std::int32_t, 6> quant_command{1, 1, 0, -2, 3, 0};
    for (std::size_t lane = 0; lane < lhs.size(); ++lane) {
        argument_words[lane] = std::bit_cast<std::uint32_t>(lhs[lane]);
        argument_words[4 + lane] = std::bit_cast<std::uint32_t>(rhs[lane]);
        argument_words[12 + lane] = std::bit_cast<std::uint32_t>(indices[lane]);
    }
    argument_words[8] = 0x5;
    for (std::size_t word = 0; word < quant_command.size(); ++word) {
        argument_words[(command_offset / 4) + word] =
            std::bit_cast<std::uint32_t>(quant_command[word]);
    }

    program_builder program;
    program.configure(4, vector_element_width::bits_32, true)
        .load(1, 0)
        .load(2, 16)
        .predicate_load(0, 32)
        .select(3, 1, 2, 0)
        .predicate_ptrue(0)
        .load(4, 48)
        .gather(5, 1, 4, 0)
        .reduce_sum(6, 1, 0)
        .reduce_min(7, 1, 0)
        .reduce_max(8, 1, 0)
        .requantize(9, 1, 0, command_offset)
        .store(3, 96, 0)
        .store(5, 112, 0)
        .store(9, 144, 0)
        .configure(1, vector_element_width::bits_32, true)
        .store(6, 128, 0)
        .store(7, 132, 0)
        .store(8, 136, 0)
        .exit();

    const auto argument_bytes = std::as_bytes(std::span{argument_words});
    machine reference(256, 16);
    reference.load_program(program.span());
    bool ok = reference.load_arguments(argument_bytes, 0);
    ok &= expect_eq("reference helper done", reference.run(64).state == lifecycle_state::done, true);
    const auto expected_select = reference.read_i32(96, 4);
    const auto expected_gather = reference.read_i32(112, 4);
    const auto expected_reductions = reference.read_i32(128, 3);
    const auto expected_requant = reference.read_i32(144, 4);

    ok &= launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
            HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
            HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE |
            HOLON_NPU_V2_CAP_QUANT_VECTOR,
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
            HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
            HOLON_NPU_PROGRAM_OP_CLASS_QUANTIZATION |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        256,
        argument_words
    );
    ok &= expect_eq("helper frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("helper frontend fault", dut.frontend_fault_o, 0);
    ok &= expect_eq(
        "helper program pc",
        axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC),
        static_cast<std::uint32_t>(program.size() * sizeof(std::uint32_t))
    );
    ok &= expect_eq(
        "helper program instret",
        axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO),
        static_cast<std::uint32_t>(program.size())
    );

    const auto compare_words = [&](std::string_view name, std::uint32_t base, const auto& expected) {
        for (std::size_t index = 0; index < expected.size(); ++index) {
            const auto result = data_read(dut, base + static_cast<std::uint32_t>(index * 4));
            ok &= expect_eq(std::string{name} + " valid", result.valid, true);
            ok &= expect_eq(std::string{name} + " error", result.error, false);
            ok &= expect_eq(
                name,
                result.data,
                std::bit_cast<std::uint32_t>(expected[index])
            );
        }
    };
    compare_words("helper select", 96, expected_select);
    compare_words("helper gather", 112, expected_gather);
    compare_words("helper reduction", 128, expected_reductions);
    compare_words("helper requant", 144, expected_requant);
    return ok;
}

bool test_vector_fault_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::program_builder;

    reset(dut);
    AxiMemory memory(0x8000);
    program_builder program;
    program.configure(0, holon_npu::v2::model::vector_element_width::bits_32, true).exit();

    bool ok = launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
            HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE,
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        64
    );
    ok &= expect_eq("vector fault terminal", dut.frontend_fault_o, 1);
    ok &= expect_eq("vector config fault", dut.frontend_fault_code_o, HOLON_NPU_V2_FAULT_VECTOR_CONFIG);
    ok &= expect_eq("vector fault pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 0U);
    ok &= expect_eq("vector fault instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 0U);
    return ok;
}

bool test_vector_extended_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::lifecycle_state;
    using holon_npu::v2::model::machine;
    using holon_npu::v2::model::program_builder;
    using holon_npu::v2::model::vector_element_width;
    using holon_npu::v2::model::vector_rounding;

    constexpr auto saturate_lhs_offset = std::uint16_t{448};
    constexpr auto saturate_rhs_offset = std::uint16_t{452};
    reset(dut);
    AxiMemory memory(0x8000);
    std::array<std::uint32_t, 116> argument_words{};
    for (std::uint32_t lane = 0; lane < 16; ++lane) {
        argument_words[lane] = lane;
        argument_words[16U + lane] = 100U + lane;
    }
    argument_words[saturate_lhs_offset / 4U] = 0x8088'7F78U;
    argument_words[saturate_rhs_offset / 4U] = 0x01EC'0114U;

    program_builder program;
    program.configure(16, vector_element_width::bits_32, true)
        .predicate_ptrue(0)
        .load(1, 0)
        .load(2, 64)
        .zip_lo(3, 1, 2)
        .zip_hi(4, 1, 2)
        .unzip_even(5, 3, 4)
        .unzip_odd(6, 3, 4)
        .transpose4(7, 1)
        .store(3, 128)
        .store(4, 192)
        .store(5, 256)
        .store(6, 320)
        .store(7, 384)
        .configure(4, vector_element_width::bits_8, true,
                   vector_rounding::nearest_even, true)
        .load(8, saturate_lhs_offset)
        .load(9, saturate_rhs_offset)
        .add(10, 8, 9)
        .sub(11, 8, 9)
        .store(10, 456)
        .store(11, 460)
        .exit();

    const auto argument_bytes = std::as_bytes(std::span{argument_words});
    machine reference(512, 16);
    reference.load_program(program.span());
    bool ok = reference.load_arguments(argument_bytes, 0);
    ok &= expect_eq("reference extended vector done",
                    reference.run(64).state == lifecycle_state::done, true);

    ok &= launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
            HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
            HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE,
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
            HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
            HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        512,
        argument_words
    );
    ok &= expect_eq("extended vector frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("extended vector frontend fault", dut.frontend_fault_o, 0);
    for (std::uint32_t address = 128; address < 448; address += 4) {
        const auto expected = reference.read_i32(address, 1).at(0);
        const auto actual = data_read(dut, address);
        ok &= expect_eq("extended vector result valid", actual.valid, true);
        ok &= expect_eq("extended vector result error", actual.error, false);
        ok &= expect_eq("extended vector result", actual.data,
                        std::bit_cast<std::uint32_t>(expected));
    }
    for (const auto address : {456U, 460U}) {
        const auto expected = reference.read_i32(address, 1).at(0);
        const auto actual = data_read(dut, address);
        ok &= expect_eq("extended saturation valid", actual.valid, true);
        ok &= expect_eq("extended saturation error", actual.error, false);
        ok &= expect_eq("extended saturation result", actual.data,
                        std::bit_cast<std::uint32_t>(expected));
    }
    return ok;
}

enum class random_vector_op : std::uint8_t {
    add,
    sub,
    min,
    max,
    equal,
    less_than,
    shift_left,
    shift_right_logical,
    shift_right_arithmetic,
};

void append_random_vector_op(
    holon_npu::v2::runtime::program_builder& program,
    random_vector_op operation
) {
    switch (operation) {
        case random_vector_op::add: program.add(3, 1, 2); break;
        case random_vector_op::sub: program.sub(3, 1, 2); break;
        case random_vector_op::min: program.min(3, 1, 2); break;
        case random_vector_op::max: program.max(3, 1, 2); break;
        case random_vector_op::equal: program.eq(3, 1, 2); break;
        case random_vector_op::less_than: program.lt(3, 1, 2); break;
        case random_vector_op::shift_left: program.shl(3, 1, 2); break;
        case random_vector_op::shift_right_logical: program.srl(3, 1, 2); break;
        case random_vector_op::shift_right_arithmetic: program.sra(3, 1, 2); break;
    }
}

bool test_random_vector_programs(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::lifecycle_state;
    using holon_npu::v2::model::machine;
    using holon_npu::v2::runtime::program_builder;
    using holon_npu::v2::runtime::vector_element_width;

    constexpr std::uint64_t seed = 0x484F'4C4F'4E56'4543ULL;
    std::mt19937_64 random{seed};
    std::uniform_int_distribution<std::uint32_t> word_distribution;
    std::uniform_int_distribution<int> vl_distribution(1, 16);
    std::uniform_int_distribution<int> operation_distribution(0, 8);
    bool ok = true;
    for (std::uint32_t case_index = 0; case_index < 24; ++case_index) {
        const auto operation = static_cast<random_vector_op>(
            case_index < 9 ? case_index : operation_distribution(random)
        );
        const auto vl = static_cast<std::uint16_t>(vl_distribution(random));
        std::array<std::uint32_t, 64> argument_words{};
        for (std::uint32_t lane = 0; lane < 16; ++lane) {
            argument_words[lane] = word_distribution(random);
            const bool shift = operation == random_vector_op::shift_left ||
                               operation == random_vector_op::shift_right_logical ||
                               operation == random_vector_op::shift_right_arithmetic;
            argument_words[16U + lane] = shift ? word_distribution(random) & 31U
                                               : word_distribution(random);
        }

        program_builder program;
        program.configure(vl, vector_element_width::bits_32, true)
            .predicate_ptrue()
            .load(1, 0)
            .load(2, 64);
        append_random_vector_op(program, operation);
        program.store(3, 128).exit();

        machine reference(256, 16);
        reference.load_program(program.span());
        ok &= reference.load_arguments(std::as_bytes(std::span{argument_words}), 0);
        ok &= expect_eq(
            "random vector reference done",
            reference.run(32).state == lifecycle_state::done,
            true
        );
        const auto expected = reference.read_i32(128, vl);

        reset(dut);
        AxiMemory memory(0x8000);
        ok &= launch_program(
            dut,
            memory,
            program.span(),
            HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
                HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
                HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
                HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE,
            HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
                HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
                HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
            256,
            argument_words
        );
        for (std::uint32_t lane = 0; lane < vl; ++lane) {
            const auto actual = data_read(dut, 128U + lane * 4U);
            ok &= expect_eq("random vector result valid", actual.valid, true);
            ok &= expect_eq("random vector result error", actual.error, false);
            ok &= expect_eq(
                "random vector result",
                actual.data,
                std::bit_cast<std::uint32_t>(expected[lane])
            );
        }
    }
    if (!ok) {
        std::cerr << "vector constrained-random seed: 0x" << std::hex << seed << std::dec << '\n';
    }
    return ok;
}

bool run_runtime_example(
    Vnpu_v2_frontend_tile& dut,
    std::string_view name,
    const holon_npu::v2::runtime::program_image& image,
    std::vector<std::uint32_t> argument_words
) {
    using holon_npu::v2::model::lifecycle_state;
    using holon_npu::v2::model::machine;

    const auto local_mem_bytes = static_cast<std::uint32_t>(
        argument_words.size() * sizeof(std::uint32_t)
    );
    machine reference(local_mem_bytes, 16);
    reference.load_program(image.span());
    bool ok = reference.load_arguments(std::as_bytes(std::span{argument_words}), 0);
    ok &= expect_eq(
        std::string{name} + " reference done",
        reference.run(image.words.size() + 8U).state == lifecycle_state::done,
        true
    );

    reset(dut);
    AxiMemory memory(0x8000);
    ok &= launch_program(
        dut,
        memory,
        image.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
            image.required_caps,
        image.required_op_classes,
        local_mem_bytes,
        argument_words
    );
    for (std::uint32_t word = 0; word < argument_words.size(); ++word) {
        const auto expected = reference.read_i32(word * 4U, 1).at(0);
        const auto actual = data_read(dut, word * 4U);
        ok &= expect_eq(std::string{name} + " result valid", actual.valid, true);
        ok &= expect_eq(std::string{name} + " result error", actual.error, false);
        ok &= expect_eq(
            std::string{name} + " result",
            actual.data,
            std::bit_cast<std::uint32_t>(expected)
        );
    }
    return ok;
}

bool test_runtime_examples(Vnpu_v2_frontend_tile& dut) {
    namespace examples = holon_npu::v2::runtime::examples;
    bool ok = true;
    {
        std::vector<std::uint32_t> words(64, 0);
        for (std::uint32_t lane = 0; lane < 16; ++lane) {
            words[lane] = lane * 3U;
            words[16U + lane] = 100U - lane;
        }
        ok &= run_runtime_example(dut, "runtime vector_add", examples::vector_add(16, 0, 64, 128), words);
    }
    {
        std::vector<std::uint32_t> words(64, 0);
        for (std::uint32_t lane = 0; lane < 16; ++lane) {
            words[lane] = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(lane) - 8);
        }
        ok &= run_runtime_example(dut, "runtime relu", examples::relu(16, 0, 64, 128), words);
        ok &= run_runtime_example(dut, "runtime reduce", examples::reduce_sum(16, 0, 128), words);
        ok &= run_runtime_example(dut, "runtime transpose", examples::transpose4(0, 128), words);
    }
    {
        std::vector<std::uint32_t> words(64, 0);
        for (std::uint32_t lane = 0; lane < 16; ++lane) {
            words[lane] = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(lane * 17U) - 100);
        }
        const std::array<std::int32_t, 6> command{3, 2, 1, -128, 127, 0};
        for (std::uint32_t index = 0; index < command.size(); ++index) {
            words[16U + index] = std::bit_cast<std::uint32_t>(command[index]);
        }
        ok &= run_runtime_example(dut, "runtime requant", examples::requant(16, 0, 128, 64), words);
    }
    return ok;
}

bool test_matrix_program(Vnpu_v2_frontend_tile& dut) {
    using holon_npu::v2::model::lifecycle_state;
    using holon_npu::v2::model::machine;
    namespace runtime = holon_npu::v2::runtime;

    constexpr auto command_offset = std::uint16_t{160};
    reset(dut);
    AxiMemory memory(0x8000);
    std::array<std::uint32_t, 48> argument_words{};
    argument_words[0] = 0x0403'0201U;
    argument_words[8] = 0x0807'0605U;
    const auto shape = std::uint32_t{2} |
        (std::uint32_t{2} << HOLON_NPU_ISA_MATRIX_SHAPE_N_SHIFT) |
        (std::uint32_t{2} << HOLON_NPU_ISA_MATRIX_SHAPE_K_SHIFT) |
        ((HOLON_NPU_ISA_MATRIX_FLAG_CLEAR | HOLON_NPU_ISA_MATRIX_FLAG_STORE)
         << HOLON_NPU_ISA_MATRIX_SHAPE_FLAGS_SHIFT);
    const std::array<std::uint32_t, 8> command{0, 32, 64, 2, 2, 8, shape, 0};
    std::ranges::copy(command, argument_words.begin() + (command_offset / 4));

    const auto program = runtime::examples::int8_gemm(0, command_offset);
    const auto argument_bytes = std::as_bytes(std::span{argument_words});
    machine reference(256, 16);
    reference.load_program(program.span());
    bool ok = reference.load_arguments(argument_bytes, 0);
    ok &= expect_eq("reference matrix done", reference.run(8).state == lifecycle_state::done, true);
    const auto expected = reference.read_i32(64, 4);

    ok &= launch_program(
        dut,
        memory,
        program.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
            program.required_caps,
        program.required_op_classes,
        256,
        argument_words
    );
    ok &= expect_eq("matrix frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("matrix frontend fault", dut.frontend_fault_o, 0);
    ok &= expect_eq("matrix program pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 8U);
    ok &= expect_eq("matrix program instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 2U);
    for (std::uint32_t index = 0; index < expected.size(); ++index) {
        const auto result = data_read(dut, 64U + index * 4U);
        ok &= expect_eq("matrix result valid", result.valid, true);
        ok &= expect_eq("matrix result error", result.error, false);
        ok &= expect_eq("matrix result", result.data, std::bit_cast<std::uint32_t>(expected[index]));
    }
    return ok;
}

bool test_tiled_matrix_program_shape(
    Vnpu_v2_frontend_tile& dut,
    std::uint32_t m,
    std::uint32_t n,
    std::uint32_t k
) {
    using holon_npu::v2::model::lifecycle_state;
    using holon_npu::v2::model::machine;
    namespace runtime = holon_npu::v2::runtime;

    constexpr std::uint32_t local_mem_bytes = 32768;
    constexpr std::uint32_t a_offset = 4096;
    constexpr std::uint32_t b_offset = 8192;
    constexpr std::uint32_t c_offset = 12288;
    const runtime::matrix_gemm_config config{
        .m = m,
        .n = n,
        .k = k,
        .a_offset = a_offset,
        .b_offset = b_offset,
        .c_offset = c_offset,
        .a_row_stride_bytes = k,
        .b_row_stride_bytes = n,
        .c_row_stride_bytes = n * static_cast<std::uint32_t>(sizeof(std::int32_t)),
        .local_mem_bytes = local_mem_bytes,
        .command_offset = 0,
    };
    const auto planned = runtime::examples::tiled_int8_gemm(config);
    if (!planned) {
        std::cerr << "failed to plan tiled matrix shape " << m << 'x' << n << 'x' << k << '\n';
        return false;
    }

    std::vector<std::uint32_t> local_words(local_mem_bytes / sizeof(std::uint32_t), 0);
    auto local_bytes = std::as_writable_bytes(std::span{local_words});
    std::vector<std::int8_t> a(static_cast<std::size_t>(m) * k);
    std::vector<std::int8_t> b(static_cast<std::size_t>(k) * n);
    std::vector<std::uint32_t> golden(static_cast<std::size_t>(m) * n, 0);
    for (std::uint32_t row = 0; row < m; ++row) {
        for (std::uint32_t kk = 0; kk < k; ++kk) {
            const auto value = static_cast<std::int8_t>(((row * 17U + kk * 13U + 5U) % 31U) - 15);
            a[row * k + kk] = value;
            local_bytes[a_offset + row * k + kk] = std::bit_cast<std::byte>(value);
        }
    }
    for (std::uint32_t kk = 0; kk < k; ++kk) {
        for (std::uint32_t col = 0; col < n; ++col) {
            const auto value = static_cast<std::int8_t>(((kk * 11U + col * 7U + 3U) % 29U) - 14);
            b[kk * n + col] = value;
            local_bytes[b_offset + kk * n + col] = std::bit_cast<std::byte>(value);
        }
    }
    for (std::uint32_t row = 0; row < m; ++row) {
        for (std::uint32_t col = 0; col < n; ++col) {
            auto& sum = golden[row * n + col];
            for (std::uint32_t kk = 0; kk < k; ++kk) {
                const auto product = static_cast<std::int32_t>(a[row * k + kk]) *
                                     static_cast<std::int32_t>(b[kk * n + col]);
                sum += static_cast<std::uint32_t>(product);
            }
        }
    }
    bool ok = planned->write_commands(local_bytes);

    machine reference(local_mem_bytes, 16);
    reference.load_program(planned->image.span());
    ok &= reference.load_arguments(std::as_bytes(std::span{local_words}), 0);
    ok &= expect_eq(
        "tiled matrix reference done",
        reference.run(planned->image.words.size() + 8U).state == lifecycle_state::done,
        true
    );
    for (std::uint32_t row = 0; row < m; ++row) {
        for (std::uint32_t col = 0; col < n; ++col) {
            const auto expected = reference.read_i32(
                c_offset + (row * n + col) * static_cast<std::uint32_t>(sizeof(std::int32_t)),
                1
            ).at(0);
            ok &= expect_eq(
                "tiled matrix reference golden",
                std::bit_cast<std::uint32_t>(expected),
                golden[row * n + col]
            );
        }
    }

    reset(dut);
    AxiMemory memory(0x20000);
    ok &= launch_program(
        dut,
        memory,
        planned->image.span(),
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
            HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
            planned->image.required_caps,
        planned->image.required_op_classes,
        local_mem_bytes,
        local_words,
        0,
        1'000'000
    );
    ok &= expect_eq("tiled matrix frontend done", dut.frontend_done_o, 1);
    ok &= expect_eq("tiled matrix frontend fault", dut.frontend_fault_o, 0);
    for (std::uint32_t row = 0; row < m; ++row) {
        for (std::uint32_t col = 0; col < n; ++col) {
            const auto actual = data_read(
                dut,
                c_offset + (row * n + col) * static_cast<std::uint32_t>(sizeof(std::int32_t))
            );
            ok &= expect_eq("tiled matrix result valid", actual.valid, true);
            ok &= expect_eq("tiled matrix result error", actual.error, false);
            ok &= expect_eq("tiled matrix result", actual.data, golden[row * n + col]);
        }
    }
    if (!ok) {
        std::cerr << "tiled matrix shape: " << m << 'x' << n << 'x' << k << '\n';
    }
    return ok;
}

bool test_tiled_matrix_program(Vnpu_v2_frontend_tile& dut) {
    bool ok = test_tiled_matrix_program_shape(dut, 17, 19, 23);
    ok &= test_tiled_matrix_program_shape(dut, 64, 64, 64);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_v2_frontend_tile", argc, argv};

    Vnpu_v2_frontend_tile dut;
    bool ok = true;

    ok &= test_system_exit(dut);
    ok &= test_system_fault(dut);
    ok &= test_completion_record(dut);
    ok &= test_scalar_control_program(dut);
    ok &= test_csr_read_program(dut);
    ok &= test_scalar_bounds_fault(dut);
    ok &= test_debug_step(dut);
    ok &= test_illegal_instruction(dut);
    ok &= test_dma_load_program(dut);
    ok &= test_dma_store_program(dut);
    ok &= test_sync_order_program(dut);
    ok &= test_vector_program(dut);
    ok &= test_vector_helper_program(dut);
    ok &= test_vector_extended_program(dut);
    ok &= test_random_vector_programs(dut);
    ok &= test_runtime_examples(dut);
    ok &= test_vector_fault_program(dut);
    ok &= test_matrix_program(dut);
    ok &= test_tiled_matrix_program(dut);

    dut.final();
    using enum holon_npu_tb::coverage_point;
    test.cover({
        v2_frontend_system_exit,
        v2_frontend_explicit_fault,
        v2_completion_record_done,
        v2_completion_record_fault,
        v2_completion_record_axi_error,
        v2_frontend_illegal_instruction,
        v2_frontend_scalar_program,
        v2_csr_debug_read,
        v2_frontend_scalar_bounds_fault,
        v2_frontend_debug_step,
        v2_frontend_tile_program_done,
        v2_frontend_dma_load_program,
        v2_frontend_dma_store_program,
        v2_frontend_sync_wait_dma,
        v2_frontend_sync_fence_local,
        v2_frontend_sync_fence_dma,
        v2_frontend_vector_program,
        v2_frontend_predicate_program,
        v2_frontend_vector_helper_program,
        v2_frontend_vector_extended_program,
        v2_vector_constrained_random,
        v2_runtime_examples_rtl,
        v2_frontend_quant_program,
        v2_frontend_vector_fault,
        v2_frontend_matrix_program,
        v2_matrix_tiled_program,
        v2_engine_data_arbiter,
        v2_control_loader_integration,
        v2_control_local_memory_integration,
        isa_class_frontend_control,
        isa_class_predicate,
        isa_class_dma,
        isa_class_csr_debug,
        isa_class_sync,
        isa_class_system,
        v2_frontend_control_movi,
        v2_frontend_control_add,
        v2_frontend_control_addi,
        v2_frontend_control_load,
        v2_frontend_control_store,
        v2_frontend_control_beq,
        v2_frontend_control_bne,
        v2_predicate_ptrue,
        v2_predicate_load,
        v2_frontend_system_fault,
    });
    return test.finish(ok);
}
