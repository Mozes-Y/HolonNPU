#include "Vnpu_v2_control_regs.h"

#include "tb_coverage.hpp"

#include "holon_npu_program.h"

#include <cstdint>
#include <iostream>
#include <string_view>

#include <verilated.h>

namespace {

constexpr std::uint32_t kOkay = 0;
constexpr std::uint32_t kSlvErr = 2;

void eval(Vnpu_v2_control_regs& dut) {
    dut.eval();
}

void tick(Vnpu_v2_control_regs& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_v2_control_regs& dut) {
    dut.s_axil_awaddr_i = 0;
    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wdata_i = 0;
    dut.s_axil_wstrb_i = 0;
    dut.s_axil_wvalid_i = 0;
    dut.s_axil_bready_i = 1;
    dut.s_axil_araddr_i = 0;
    dut.s_axil_arvalid_i = 0;
    dut.s_axil_rready_i = 1;

    dut.loader_done_i = 0;
    dut.frontend_done_i = 0;
    dut.frontend_fault_i = 0;
    dut.frontend_fault_code_i = 0;
    dut.frontend_halted_i = 0;
    dut.frontend_debug_pc_i = 0;
    dut.frontend_instret_i = 0;
}

void reset(Vnpu_v2_control_regs& dut) {
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

std::uint32_t axil_write(
    Vnpu_v2_control_regs& dut,
    std::uint32_t addr,
    std::uint32_t data,
    std::uint32_t strb = 0xF,
    std::uint32_t* program_start_sample = nullptr,
    std::uint32_t* soft_reset_sample = nullptr,
    std::uint32_t* halt_sample = nullptr,
    std::uint32_t* resume_sample = nullptr,
    std::uint32_t* debug_step_sample = nullptr
) {
    dut.s_axil_awaddr_i = static_cast<std::uint16_t>(addr);
    dut.s_axil_awvalid_i = 1;
    dut.s_axil_wdata_i = data;
    dut.s_axil_wstrb_i = static_cast<std::uint8_t>(strb);
    dut.s_axil_wvalid_i = 1;
    dut.s_axil_bready_i = 1;
    tick(dut);

    const auto resp = static_cast<std::uint32_t>(dut.s_axil_bresp_o);
    if (program_start_sample != nullptr) {
        *program_start_sample = dut.program_start_o ? 1U : 0U;
    }
    if (soft_reset_sample != nullptr) {
        *soft_reset_sample = dut.soft_reset_o ? 1U : 0U;
    }
    if (halt_sample != nullptr) {
        *halt_sample = dut.halt_request_o ? 1U : 0U;
    }
    if (resume_sample != nullptr) {
        *resume_sample = dut.resume_o ? 1U : 0U;
    }
    if (debug_step_sample != nullptr) {
        *debug_step_sample = dut.debug_step_o ? 1U : 0U;
    }

    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wvalid_i = 0;
    tick(dut);
    return resp;
}

std::uint32_t axil_write_aw_then_w(
    Vnpu_v2_control_regs& dut,
    std::uint32_t addr,
    std::uint32_t data,
    std::uint32_t strb = 0xF
) {
    dut.s_axil_awaddr_i = static_cast<std::uint16_t>(addr);
    dut.s_axil_awvalid_i = 1;
    dut.s_axil_wvalid_i = 0;
    dut.s_axil_bready_i = 1;
    tick(dut);

    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wdata_i = data;
    dut.s_axil_wstrb_i = static_cast<std::uint8_t>(strb);
    dut.s_axil_wvalid_i = 1;
    tick(dut);

    const auto resp = static_cast<std::uint32_t>(dut.s_axil_bresp_o);
    dut.s_axil_wvalid_i = 0;
    tick(dut);
    return resp;
}

std::uint32_t axil_write_w_then_aw(
    Vnpu_v2_control_regs& dut,
    std::uint32_t addr,
    std::uint32_t data,
    std::uint32_t strb = 0xF
) {
    dut.s_axil_wdata_i = data;
    dut.s_axil_wstrb_i = static_cast<std::uint8_t>(strb);
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

std::uint32_t axil_read(
    Vnpu_v2_control_regs& dut,
    std::uint32_t addr,
    std::uint32_t* resp = nullptr
) {
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

void loader_done(Vnpu_v2_control_regs& dut) {
    dut.loader_done_i = 1;
    tick(dut);
    dut.loader_done_i = 0;
    eval(dut);
}

void frontend_halted(Vnpu_v2_control_regs& dut, std::uint32_t pc) {
    dut.frontend_debug_pc_i = pc;
    dut.frontend_halted_i = 1;
    tick(dut);
    dut.frontend_halted_i = 0;
    eval(dut);
}

void frontend_done(Vnpu_v2_control_regs& dut, std::uint32_t pc, std::uint64_t instret) {
    dut.frontend_debug_pc_i = pc;
    dut.frontend_instret_i = instret;
    dut.frontend_done_i = 1;
    tick(dut);
    dut.frontend_done_i = 0;
    eval(dut);
}

void frontend_fault(Vnpu_v2_control_regs& dut, std::uint32_t code, std::uint32_t pc) {
    dut.frontend_fault_code_i = code;
    dut.frontend_debug_pc_i = pc;
    dut.frontend_fault_i = 1;
    tick(dut);
    dut.frontend_fault_i = 0;
    eval(dut);
}

bool test_reset_values(Vnpu_v2_control_regs& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("DEVICE_ID", axil_read(dut, HOLON_NPU_V2_REG_DEVICE_ID), 0x4E50'5502U);
    ok &= expect_eq("ABI_VERSION", axil_read(dut, HOLON_NPU_V2_REG_ABI_VERSION), HOLON_NPU_V2_ABI_VERSION_RESET);
    ok &= expect_eq("ISA_VERSION", axil_read(dut, HOLON_NPU_V2_REG_ISA_VERSION), 0x0001'0000U);
    ok &= expect_eq("CAP0_LO", axil_read(dut, HOLON_NPU_V2_REG_CAP0_LO), 0x0000'003FU);
    ok &= expect_eq("OP_CLASS_LO", axil_read(dut, HOLON_NPU_V2_REG_OP_CLASS_LO), 0x0000'01FFU);
    ok &= expect_eq("PROGRAM_MEM_BYTES", axil_read(dut, HOLON_NPU_V2_REG_PROGRAM_MEM_BYTES), 0x0001'0000U);
    ok &= expect_eq("LOCAL_MEM_BYTES", axil_read(dut, HOLON_NPU_V2_REG_LOCAL_MEM_BYTES), 0x0004'0000U);
    ok &= expect_eq("VECTOR_CAP0", axil_read(dut, HOLON_NPU_V2_REG_VECTOR_CAP0), 0x0810'0100U);
    ok &= expect_eq("MATRIX_CAP0", axil_read(dut, HOLON_NPU_V2_REG_MATRIX_CAP0), 0x0820'1010U);
    ok &= expect_eq("STATUS reset", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_IDLE);
    ok &= expect_eq("FAULT_CODE reset", axil_read(dut, HOLON_NPU_V2_REG_FAULT_CODE), HOLON_NPU_V2_FAULT_NONE);
    ok &= expect_eq("IRQ_ENABLE reset", axil_read(dut, HOLON_NPU_V2_REG_IRQ_ENABLE), 0U);
    ok &= expect_eq("IRQ_STATUS reset", axil_read(dut, HOLON_NPU_V2_REG_IRQ_STATUS), 0U);
    ok &= expect_eq("DESC_ADDR_LO reset", axil_read(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO), 0U);
    ok &= expect_eq("DESC_ADDR_HI reset", axil_read(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_HI), 0U);
    return ok;
}

bool test_register_access_and_skew(Vnpu_v2_control_regs& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("write desc lo", axil_write(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO, 0x0000'1000U), kOkay);
    ok &= expect_eq("write desc hi", axil_write(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_HI, 0x1234'5678U), kOkay);
    ok &= expect_eq("read desc lo", axil_read(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO), 0x0000'1000U);
    ok &= expect_eq("read desc hi", axil_read(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_HI), 0x1234'5678U);
    ok &= expect_eq("desc addr output", static_cast<std::uint64_t>(dut.program_desc_addr_o), 0x1234'5678'0000'1000ULL);

    ok &= expect_eq("AW before W", axil_write_aw_then_w(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO, 0x0000'2000U), kOkay);
    ok &= expect_eq("AW before W readback", axil_read(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO), 0x0000'2000U);
    ok &= expect_eq("W before AW", axil_write_w_then_aw(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_HI, 0x8765'4321U), kOkay);
    ok &= expect_eq("W before AW readback", axil_read(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_HI), 0x8765'4321U);

    ok &= expect_eq("write IRQ enable", axil_write(dut, HOLON_NPU_V2_REG_IRQ_ENABLE, HOLON_NPU_V2_IRQ_VALID_MASK), kOkay);
    ok &= expect_eq("read IRQ enable", axil_read(dut, HOLON_NPU_V2_REG_IRQ_ENABLE), HOLON_NPU_V2_IRQ_VALID_MASK);
    ok &= expect_eq("reject IRQ reserved bit", axil_write(dut, HOLON_NPU_V2_REG_IRQ_ENABLE, 0x10U), kSlvErr);
    ok &= expect_eq("reject RO write", axil_write(dut, HOLON_NPU_V2_REG_DEVICE_ID, 0), kSlvErr);
    ok &= expect_eq("reject IRQ_STATUS write", axil_write(dut, HOLON_NPU_V2_REG_IRQ_STATUS, 1), kSlvErr);
    ok &= expect_eq("reject partial control", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_SOFT_RESET, 0x1), kSlvErr);
    ok &= expect_eq("reject multi-command control",
                    axil_write(dut, HOLON_NPU_V2_REG_CONTROL,
                               HOLON_NPU_V2_CONTROL_SOFT_RESET | HOLON_NPU_V2_CONTROL_HALT),
                    kSlvErr);
    ok &= expect_eq("multi-command control no reset side effect",
                    axil_read(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO), 0x0000'2000U);

    std::uint32_t resp = 0;
    ok &= expect_eq("unmapped read data", axil_read(dut, 0x100, &resp), 0U);
    ok &= expect_eq("unmapped read response", resp, kSlvErr);
    ok &= expect_eq("unmapped write response", axil_write(dut, 0x100, 0), kSlvErr);
    return ok;
}

bool test_lifecycle_done_halt_debug(Vnpu_v2_control_regs& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("enable IRQs", axil_write(dut, HOLON_NPU_V2_REG_IRQ_ENABLE, HOLON_NPU_V2_IRQ_VALID_MASK), kOkay);
    ok &= expect_eq("desc lo", axil_write(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO, 0x0000'4000U), kOkay);

    std::uint32_t start = 0;
    ok &= expect_eq("doorbell", axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, 1, 0xF, &start), kOkay);
    ok &= expect_eq("start pulse", start, 1U);
    ok &= expect_eq("status loading", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_LOADING);
    ok &= expect_eq("doorbell while loading rejected", axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, 1), kSlvErr);

    loader_done(dut);
    ok &= expect_eq("status running", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_RUNNING);

    std::uint32_t halt = 0;
    ok &= expect_eq("halt request", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_HALT, 0xF, nullptr, nullptr, &halt), kOkay);
    ok &= expect_eq("halt pulse", halt, 1U);
    frontend_halted(dut, 0x24);
    ok &= expect_eq("status halted irq", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_HALTED | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    ok &= expect_eq("debug pc halted", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 0x24U);

    std::uint32_t debug_step = 0;
    ok &= expect_eq("debug step", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_DEBUG_STEP,
                                             0xF, nullptr, nullptr, nullptr, nullptr, &debug_step), kOkay);
    ok &= expect_eq("debug step pulse", debug_step, 1U);
    ok &= expect_eq("IRQ halted debug", axil_read(dut, HOLON_NPU_V2_REG_IRQ_STATUS),
                    HOLON_NPU_V2_IRQ_HALTED | HOLON_NPU_V2_IRQ_DEBUG_STEP);

    std::uint32_t resume = 0;
    ok &= expect_eq("resume", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_RESUME,
                                         0xF, nullptr, nullptr, nullptr, &resume), kOkay);
    ok &= expect_eq("resume pulse", resume, 1U);
    ok &= expect_eq("status running irq", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_RUNNING | HOLON_NPU_V2_STATUS_IRQ_PENDING);

    frontend_done(dut, 0x40, 7);
    ok &= expect_eq("status done irq", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_DONE | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    ok &= expect_eq("debug pc done", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 0x40U);
    ok &= expect_eq("instret", axil_read(dut, HOLON_NPU_V2_REG_PERF_INSTRET_LO), 7U);
    ok &= expect_eq("IRQ all expected", axil_read(dut, HOLON_NPU_V2_REG_IRQ_STATUS),
                    HOLON_NPU_V2_IRQ_DONE | HOLON_NPU_V2_IRQ_HALTED | HOLON_NPU_V2_IRQ_DEBUG_STEP);
    ok &= expect_eq("clear IRQ", axil_write(dut, HOLON_NPU_V2_REG_IRQ_CLEAR, HOLON_NPU_V2_IRQ_VALID_MASK), kOkay);
    ok &= expect_eq("IRQ cleared", axil_read(dut, HOLON_NPU_V2_REG_IRQ_STATUS), 0U);
    ok &= expect_eq("clear terminal", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_CLEAR_TERMINAL), kOkay);
    ok &= expect_eq("status idle after clear", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_IDLE);
    return ok;
}

bool test_faults_and_soft_reset(Vnpu_v2_control_regs& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("enable fault IRQ", axil_write(dut, HOLON_NPU_V2_REG_IRQ_ENABLE, HOLON_NPU_V2_IRQ_FAULT), kOkay);
    ok &= expect_eq("unaligned desc", axil_write(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO, 0x1003U), kOkay);
    std::uint32_t start = 0;
    ok &= expect_eq("doorbell unaligned accepted into fault",
                    axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, 1, 0xF, &start), kOkay);
    ok &= expect_eq("no start on unaligned", start, 0U);
    ok &= expect_eq("status alignment fault", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_FAULT | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    ok &= expect_eq("alignment fault code", axil_read(dut, HOLON_NPU_V2_REG_FAULT_CODE), HOLON_NPU_V2_FAULT_ALIGNMENT);

    std::uint32_t soft_reset = 0;
    ok &= expect_eq("soft reset", axil_write(dut, HOLON_NPU_V2_REG_CONTROL, HOLON_NPU_V2_CONTROL_SOFT_RESET,
                                             0xF, nullptr, &soft_reset), kOkay);
    ok &= expect_eq("soft reset pulse", soft_reset, 1U);
    ok &= expect_eq("status after reset", axil_read(dut, HOLON_NPU_V2_REG_STATUS), HOLON_NPU_V2_STATUS_IDLE);
    ok &= expect_eq("desc after reset", axil_read(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO), 0U);
    ok &= expect_eq("IRQ enable after reset", axil_read(dut, HOLON_NPU_V2_REG_IRQ_ENABLE), 0U);

    ok &= expect_eq("desc aligned", axil_write(dut, HOLON_NPU_V2_REG_PROGRAM_DESC_ADDR_LO, 0x2000U), kOkay);
    ok &= expect_eq("enable fault IRQ again", axil_write(dut, HOLON_NPU_V2_REG_IRQ_ENABLE, HOLON_NPU_V2_IRQ_FAULT), kOkay);
    ok &= expect_eq("doorbell for runtime fault", axil_write(dut, HOLON_NPU_V2_REG_DOORBELL, 1), kOkay);
    loader_done(dut);
    frontend_fault(dut, HOLON_NPU_V2_FAULT_DMA_REQUEST, 0x80);
    ok &= expect_eq("runtime fault status", axil_read(dut, HOLON_NPU_V2_REG_STATUS),
                    HOLON_NPU_V2_STATUS_FAULT | HOLON_NPU_V2_STATUS_IRQ_PENDING);
    ok &= expect_eq("runtime fault code", axil_read(dut, HOLON_NPU_V2_REG_FAULT_CODE), HOLON_NPU_V2_FAULT_DMA_REQUEST);
    ok &= expect_eq("runtime fault pc", axil_read(dut, HOLON_NPU_V2_REG_DEBUG_PC), 0x80U);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_v2_control", argc, argv};

    Vnpu_v2_control_regs dut;
    bool ok = true;

    ok &= test_reset_values(dut);
    ok &= test_register_access_and_skew(dut);
    ok &= test_lifecycle_done_halt_debug(dut);
    ok &= test_faults_and_soft_reset(dut);

    dut.final();
    using enum holon_npu_tb::coverage_point;
    test.cover({axi_lite_aw_w_skew, control_done_irq, control_error_irq, control_soft_reset});
    return test.finish(ok);
}
