#include "Vnpu_v2_loader.h"

#include "tb_coverage.hpp"

#include "holon_npu_program.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <string_view>
#include <vector>

#include <verilated.h>

namespace {

constexpr std::uint32_t kRespOkay = 0;
constexpr std::uint32_t kRespSlvErr = 2;
constexpr std::uint64_t kDescAddr = 0x1000;

struct Burst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
    std::uint32_t index = 0;
};

struct ObservedBurst {
    std::uint64_t addr = 0;
    std::uint32_t beats = 0;
    std::uint32_t size = 0;
};

void eval(Vnpu_v2_loader& dut) {
    dut.eval();
}

void tick(Vnpu_v2_loader& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_v2_loader& dut) {
    dut.soft_reset_i = 0;
    dut.start_i = 0;
    dut.desc_addr_i = 0;
    dut.m_axi_arready_i = 0;
    dut.m_axi_rdata_lo_i = 0;
    dut.m_axi_rdata_hi_i = 0;
    dut.m_axi_rresp_i = kRespOkay;
    dut.m_axi_rlast_i = 0;
    dut.m_axi_rvalid_i = 0;
}

void reset(Vnpu_v2_loader& dut) {
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
                         HOLON_NPU_V2_CAP_INTEGER_QUANT_VECTOR,
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

class DescriptorMemory {
public:
    explicit DescriptorMemory(std::size_t byte_count) : memory_(byte_count) {}

    void store(std::uint64_t addr, const holon_npu_program_desc_t& desc) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&desc);
        for (std::size_t i = 0; i < sizeof(desc); ++i) {
            memory_.at(addr + i) = bytes[i];
        }
    }

    void set_error_on_first_beat(bool enabled) {
        error_on_first_beat_ = enabled;
    }

    [[nodiscard]] const std::vector<ObservedBurst>& observed_bursts() const {
        return observed_bursts_;
    }

    void step(Vnpu_v2_loader& dut) {
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
            dut.m_axi_rresp_i = (error_on_first_beat_ && active_burst_.index == 0) ? kRespSlvErr : kRespOkay;
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
            pending_.push_back(Burst{ar_addr, ar_beats, 0});
            observed_bursts_.push_back(ObservedBurst{ar_addr, ar_beats, ar_size});
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
    Burst active_burst_{};
};

void start_loader(Vnpu_v2_loader& dut, std::uint64_t desc_addr) {
    dut.desc_addr_i = desc_addr;
    dut.start_i = 1;
    tick(dut);
    dut.start_i = 0;
    eval(dut);
}

bool run_until_terminal(Vnpu_v2_loader& dut, DescriptorMemory& memory, int max_cycles = 80) {
    for (int cycle = 0; cycle < max_cycles; ++cycle) {
        if (dut.done_o || dut.fault_o) {
            return true;
        }
        memory.step(dut);
    }
    return dut.done_o || dut.fault_o;
}

bool test_valid_descriptor(Vnpu_v2_loader& dut) {
    reset(dut);
    DescriptorMemory memory(0x8000);
    const auto desc = valid_descriptor();
    memory.store(kDescAddr, desc);

    start_loader(dut, kDescAddr);
    bool ok = run_until_terminal(dut, memory);
    ok &= expect_eq("valid done", dut.done_o, 1);
    ok &= expect_eq("valid fault", dut.fault_o, 0);
    ok &= expect_eq("burst count", memory.observed_bursts().size(), 1);
    if (!memory.observed_bursts().empty()) {
        ok &= expect_eq("burst addr", memory.observed_bursts().front().addr, kDescAddr);
        ok &= expect_eq("burst beats", memory.observed_bursts().front().beats, 8);
        ok &= expect_eq("burst size", memory.observed_bursts().front().size, 4);
    }
    ok &= expect_eq("program format", dut.program_format_o, desc.program_format);
    ok &= expect_eq("isa major", dut.holon_isa_major_o, desc.holon_isa_major);
    ok &= expect_eq("isa minor", dut.holon_isa_minor_o, desc.holon_isa_minor);
    ok &= expect_eq("required caps", dut.required_caps_o, desc.required_caps);
    ok &= expect_eq("required op classes", dut.required_op_classes_o, desc.required_op_classes);
    ok &= expect_eq("code addr", dut.code_addr_o, desc.code_addr);
    ok &= expect_eq("code size", dut.code_size_bytes_o, desc.code_size_bytes);
    ok &= expect_eq("entry pc", dut.entry_pc_o, desc.entry_pc);
    ok &= expect_eq("arg addr", dut.arg_addr_o, desc.arg_addr);
    ok &= expect_eq("arg size", dut.arg_size_bytes_o, desc.arg_size_bytes);
    ok &= expect_eq("local mem", dut.local_mem_bytes_o, desc.local_mem_bytes);
    ok &= expect_eq("program mem", dut.program_mem_bytes_o, desc.program_mem_bytes);
    ok &= expect_eq("stack bytes", dut.stack_bytes_o, desc.stack_bytes);
    ok &= expect_eq("completion addr", dut.completion_addr_o, desc.completion_addr);
    ok &= expect_eq("flags", dut.flags_o, desc.flags);
    return ok;
}

bool expect_descriptor_fault(
    Vnpu_v2_loader& dut,
    std::string_view name,
    holon_npu_program_desc_t desc,
    std::uint32_t expected_fault
) {
    reset(dut);
    DescriptorMemory memory(0x8000);
    memory.store(kDescAddr, desc);
    start_loader(dut, kDescAddr);

    bool ok = run_until_terminal(dut, memory);
    ok &= expect_eq(name, dut.fault_o, 1);
    ok &= expect_eq("fault code", dut.fault_code_o, expected_fault);
    ok &= expect_eq("no done on fault", dut.done_o, 0);
    return ok;
}

bool test_descriptor_validation_faults(Vnpu_v2_loader& dut) {
    bool ok = true;
    auto desc = valid_descriptor();

    desc.size_bytes = 64;
    ok &= expect_descriptor_fault(dut, "bad size", desc, HOLON_NPU_V2_FAULT_INVALID_PROGRAM_DESCRIPTOR);

    desc = valid_descriptor();
    desc.version = 2;
    ok &= expect_descriptor_fault(dut, "bad version", desc, HOLON_NPU_V2_FAULT_UNSUPPORTED_ABI_OR_ISA);

    desc = valid_descriptor();
    desc.holon_isa_major = 2;
    ok &= expect_descriptor_fault(dut, "bad ISA major", desc, HOLON_NPU_V2_FAULT_UNSUPPORTED_ABI_OR_ISA);

    desc = valid_descriptor();
    desc.holon_isa_minor = static_cast<std::uint16_t>(HOLON_NPU_ISA_MINOR + 1U);
    ok &= expect_descriptor_fault(dut, "bad ISA minor", desc, HOLON_NPU_V2_FAULT_UNSUPPORTED_ABI_OR_ISA);

    desc = valid_descriptor();
    desc.program_format = 0;
    ok &= expect_descriptor_fault(dut, "bad program format", desc, HOLON_NPU_V2_FAULT_UNSUPPORTED_PROGRAM_FORMAT);

    desc = valid_descriptor();
    desc.required_caps = std::uint64_t{1} << 63U;
    ok &= expect_descriptor_fault(dut, "bad capability", desc, HOLON_NPU_V2_FAULT_UNSUPPORTED_CAPABILITY);

    desc = valid_descriptor();
    desc.required_op_classes = std::uint64_t{1} << 63U;
    ok &= expect_descriptor_fault(dut, "bad op class", desc, HOLON_NPU_V2_FAULT_UNSUPPORTED_OPERATION_CLASS);

    desc = valid_descriptor();
    desc.flags = HOLON_NPU_PROGRAM_FLAG_VALID_MASK << 1U;
    ok &= expect_descriptor_fault(dut, "bad flags", desc, HOLON_NPU_V2_FAULT_INVALID_PROGRAM_DESCRIPTOR);

    desc = valid_descriptor();
    desc.reserved_50 = 1;
    ok &= expect_descriptor_fault(dut, "bad reserved", desc, HOLON_NPU_V2_FAULT_INVALID_PROGRAM_DESCRIPTOR);

    desc = valid_descriptor();
    desc.code_addr = 0x2002;
    ok &= expect_descriptor_fault(dut, "bad code alignment", desc, HOLON_NPU_V2_FAULT_ALIGNMENT);

    desc = valid_descriptor();
    desc.arg_size_bytes = 4;
    ok &= expect_descriptor_fault(dut, "bad argument size alignment", desc, HOLON_NPU_V2_FAULT_ALIGNMENT);

    desc = valid_descriptor();
    desc.code_size_bytes = 0;
    ok &= expect_descriptor_fault(dut, "zero code size", desc, HOLON_NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);

    desc = valid_descriptor();
    desc.entry_pc = desc.code_size_bytes;
    ok &= expect_descriptor_fault(dut, "entry out of bounds", desc, HOLON_NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);

    desc = valid_descriptor();
    desc.local_mem_bytes = HOLON_NPU_LOCAL_MEM_MAX_BYTES + 16U;
    ok &= expect_descriptor_fault(dut, "local memory too large", desc, HOLON_NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS);

    return ok;
}

bool test_descriptor_address_alignment(Vnpu_v2_loader& dut) {
    reset(dut);
    DescriptorMemory memory(0x8000);
    memory.store(kDescAddr, valid_descriptor());
    start_loader(dut, kDescAddr + 4);

    bool ok = run_until_terminal(dut, memory);
    ok &= expect_eq("unaligned desc fault", dut.fault_o, 1);
    ok &= expect_eq("unaligned desc fault code", dut.fault_code_o, HOLON_NPU_V2_FAULT_ALIGNMENT);
    ok &= expect_eq("unaligned desc no AXI", memory.observed_bursts().size(), 0);
    return ok;
}

bool test_axi_error(Vnpu_v2_loader& dut) {
    reset(dut);
    DescriptorMemory memory(0x8000);
    memory.store(kDescAddr, valid_descriptor());
    memory.set_error_on_first_beat(true);
    start_loader(dut, kDescAddr);

    bool ok = run_until_terminal(dut, memory);
    ok &= expect_eq("AXI read fault", dut.fault_o, 1);
    ok &= expect_eq("AXI read fault code", dut.fault_code_o, HOLON_NPU_V2_FAULT_AXI_READ);
    ok &= expect_eq("AXI read burst observed", memory.observed_bursts().size(), 1);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_v2_loader", argc, argv};

    Vnpu_v2_loader dut;
    bool ok = true;

    ok &= test_valid_descriptor(dut);
    ok &= test_descriptor_validation_faults(dut);
    ok &= test_descriptor_address_alignment(dut);
    ok &= test_axi_error(dut);

    dut.final();
    using enum holon_npu_tb::coverage_point;
    test.cover({
        descriptor_valid,
        descriptor_invalid_size,
        descriptor_invalid_version,
        descriptor_invalid_flags,
        descriptor_reserved_nonzero,
        dma_read_axi_error,
    });
    return test.finish(ok);
}
