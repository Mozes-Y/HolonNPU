#include "Vnpu_control_regs.h"

#include "holon_npu_regs.h"

#include <cstdint>
#include <iostream>
#include <string_view>

#include <verilated.h>

namespace {

constexpr std::uint32_t kOkay = 0;
constexpr std::uint32_t kSlvErr = 2;

constexpr std::uint32_t kDeviceId = HOLON_NPU_REG_DEVICE_ID;
constexpr std::uint32_t kAbiVersion = HOLON_NPU_REG_ABI_VERSION;
constexpr std::uint32_t kCap0 = HOLON_NPU_REG_CAP0;
constexpr std::uint32_t kCap1 = HOLON_NPU_REG_CAP1;
constexpr std::uint32_t kControl = HOLON_NPU_REG_CONTROL;
constexpr std::uint32_t kStatus = HOLON_NPU_REG_STATUS;
constexpr std::uint32_t kErrorCode = HOLON_NPU_REG_ERROR_CODE;
constexpr std::uint32_t kIrqEnable = HOLON_NPU_REG_IRQ_ENABLE;
constexpr std::uint32_t kIrqStatus = HOLON_NPU_REG_IRQ_STATUS;
constexpr std::uint32_t kDoorbell = HOLON_NPU_REG_DOORBELL;
constexpr std::uint32_t kDescAddrLo = HOLON_NPU_REG_DESC_ADDR_LO;
constexpr std::uint32_t kDescAddrHi = HOLON_NPU_REG_DESC_ADDR_HI;
constexpr std::uint32_t kClear = HOLON_NPU_REG_CLEAR;
constexpr std::uint32_t kReserved034 = 0x034;
constexpr std::uint32_t kPerfCyclesLo = HOLON_NPU_REG_PERF_CYCLES_LO;
constexpr std::uint32_t kPerfBusyLo = HOLON_NPU_REG_PERF_BUSY_LO;
constexpr std::uint32_t kPerfDescCount = HOLON_NPU_REG_PERF_DESC_COUNT;
constexpr std::uint32_t kPerfErrorCount = HOLON_NPU_REG_PERF_ERROR_COUNT;

void eval(Vnpu_control_regs& dut) {
    dut.eval();
}

void tick(Vnpu_control_regs& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_control_regs& dut) {
    dut.s_axil_awaddr_i = 0;
    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wdata_i = 0;
    dut.s_axil_wstrb_i = 0;
    dut.s_axil_wvalid_i = 0;
    dut.s_axil_bready_i = 1;
    dut.s_axil_araddr_i = 0;
    dut.s_axil_arvalid_i = 0;
    dut.s_axil_rready_i = 1;

    dut.backend_done_i = 0;
    dut.backend_error_i = 0;
    dut.backend_error_code_i = 0;
    dut.backend_busy_i = 0;
    dut.backend_clear_perf_i = 0;
    dut.backend_irq_on_done_i = 0;
    dut.backend_irq_on_error_i = 0;
}

void reset(Vnpu_control_regs& dut) {
    dut.clk_i = 0;
    dut.rst_ni = 0;
    clear_inputs(dut);
    tick(dut);
    dut.rst_ni = 1;
    eval(dut);
}

bool expect_eq(std::string_view name, std::uint32_t actual, std::uint32_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

std::uint32_t axil_write(
    Vnpu_control_regs& dut,
    std::uint32_t addr,
    std::uint32_t data,
    std::uint32_t strb = 0xF,
    std::uint32_t* command_start_sample = nullptr,
    std::uint32_t* soft_reset_sample = nullptr,
    std::uint32_t* clear_perf_sample = nullptr
) {
    dut.s_axil_awaddr_i = static_cast<std::uint16_t>(addr);
    dut.s_axil_awvalid_i = 1;
    dut.s_axil_wdata_i = data;
    dut.s_axil_wstrb_i = static_cast<std::uint8_t>(strb);
    dut.s_axil_wvalid_i = 1;
    dut.s_axil_bready_i = 1;
    tick(dut);

    const auto resp = static_cast<std::uint32_t>(dut.s_axil_bresp_o);
    if (command_start_sample != nullptr) {
        *command_start_sample = dut.command_start_o ? 1U : 0U;
    }
    if (soft_reset_sample != nullptr) {
        *soft_reset_sample = dut.soft_reset_o ? 1U : 0U;
    }
    if (clear_perf_sample != nullptr) {
        *clear_perf_sample = dut.clear_perf_o ? 1U : 0U;
    }

    dut.s_axil_awvalid_i = 0;
    dut.s_axil_wvalid_i = 0;
    tick(dut);
    return resp;
}

std::uint32_t axil_write_aw_then_w(
    Vnpu_control_regs& dut,
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
    Vnpu_control_regs& dut,
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
    Vnpu_control_regs& dut,
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

void backend_done(Vnpu_control_regs& dut, bool irq_on_done) {
    dut.backend_busy_i = 1;
    dut.backend_irq_on_done_i = irq_on_done ? 1 : 0;
    dut.backend_done_i = 1;
    tick(dut);
    dut.backend_busy_i = 0;
    dut.backend_done_i = 0;
    dut.backend_irq_on_done_i = 0;
    eval(dut);
}

void backend_error(Vnpu_control_regs& dut, std::uint32_t code, bool irq_on_error) {
    dut.backend_busy_i = 1;
    dut.backend_error_code_i = code;
    dut.backend_irq_on_error_i = irq_on_error ? 1 : 0;
    dut.backend_error_i = 1;
    tick(dut);
    dut.backend_busy_i = 0;
    dut.backend_error_i = 0;
    dut.backend_irq_on_error_i = 0;
    eval(dut);
}

bool test_reset_values(Vnpu_control_regs& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("DEVICE_ID", axil_read(dut, kDeviceId), 0x4E50'5501U);
    ok &= expect_eq("ABI_VERSION", axil_read(dut, kAbiVersion), 0x0001'0000U);
    ok &= expect_eq("CAP0", axil_read(dut, kCap0), 0x0000'003FU);
    ok &= expect_eq("CAP1", axil_read(dut, kCap1), 0x0820'1010U);
    ok &= expect_eq("STATUS reset", axil_read(dut, kStatus), 0x0000'0001U);
    ok &= expect_eq("ERROR_CODE reset", axil_read(dut, kErrorCode), 0x0000'0000U);
    ok &= expect_eq("IRQ_ENABLE reset", axil_read(dut, kIrqEnable), 0x0000'0000U);
    ok &= expect_eq("IRQ_STATUS reset", axil_read(dut, kIrqStatus), 0x0000'0000U);
    ok &= expect_eq("CONTROL read zero", axil_read(dut, kControl), 0x0000'0000U);
    ok &= expect_eq("DOORBELL read zero", axil_read(dut, kDoorbell), 0x0000'0000U);
    ok &= expect_eq("CLEAR read zero", axil_read(dut, kClear), 0x0000'0000U);
    return ok;
}

bool test_rw_and_illegal_access(Vnpu_control_regs& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("write DESC_ADDR_LO", axil_write(dut, kDescAddrLo, 0x89AB'CDEFU), kOkay);
    ok &= expect_eq("write DESC_ADDR_HI", axil_write(dut, kDescAddrHi, 0x0123'4567U), kOkay);
    ok &= expect_eq("read DESC_ADDR_LO", axil_read(dut, kDescAddrLo), 0x89AB'CDEFU);
    ok &= expect_eq("read DESC_ADDR_HI", axil_read(dut, kDescAddrHi), 0x0123'4567U);
    ok &= expect_eq("command desc addr lo output", static_cast<std::uint32_t>(dut.command_desc_addr_o), 0x89AB'CDEFU);
    ok &= expect_eq("AW before W write", axil_write_aw_then_w(dut, kDescAddrLo, 0x1357'9BDFU), kOkay);
    ok &= expect_eq("AW before W readback", axil_read(dut, kDescAddrLo), 0x1357'9BDFU);
    ok &= expect_eq("W before AW write", axil_write_w_then_aw(dut, kDescAddrHi, 0x2468'ACE0U), kOkay);
    ok &= expect_eq("W before AW readback", axil_read(dut, kDescAddrHi), 0x2468'ACE0U);

    ok &= expect_eq("write IRQ_ENABLE", axil_write(dut, kIrqEnable, 0x0000'0003U), kOkay);
    ok &= expect_eq("read IRQ_ENABLE", axil_read(dut, kIrqEnable), 0x0000'0003U);
    ok &= expect_eq("reject IRQ_ENABLE reserved", axil_write(dut, kIrqEnable, 0x0000'0004U), kSlvErr);
    ok &= expect_eq("IRQ_ENABLE unchanged after reserved write", axil_read(dut, kIrqEnable), 0x0000'0003U);

    ok &= expect_eq("reject RO write", axil_write(dut, kDeviceId, 0), kSlvErr);
    ok &= expect_eq("DEVICE_ID unchanged", axil_read(dut, kDeviceId), 0x4E50'5501U);
    ok &= expect_eq("reserved read zero", axil_read(dut, kReserved034), 0U);
    ok &= expect_eq("reserved write rejected", axil_write(dut, kReserved034, 1), kSlvErr);

    std::uint32_t resp = 0;
    ok &= expect_eq("unmapped read data", axil_read(dut, 0x100, &resp), 0U);
    ok &= expect_eq("unmapped read response", resp, kSlvErr);
    ok &= expect_eq("unmapped write response", axil_write(dut, 0x100, 0), kSlvErr);
    ok &= expect_eq("partial CONTROL write rejected", axil_write(dut, kControl, 1, 0x1), kSlvErr);
    ok &= expect_eq("DOORBELL reserved bits rejected", axil_write(dut, kDoorbell, 0x2), kSlvErr);
    return ok;
}

bool test_done_flow(Vnpu_control_regs& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("enable done IRQ", axil_write(dut, kIrqEnable, 0x1), kOkay);
    std::uint32_t command_start = 0;
    ok &= expect_eq("doorbell accepted", axil_write(dut, kDoorbell, 0x1, 0xF, &command_start), kOkay);
    ok &= expect_eq("command start pulse", command_start, 1U);
    ok &= expect_eq("status busy", axil_read(dut, kStatus), 0x0000'0002U);
    ok &= expect_eq("doorbell while busy rejected", axil_write(dut, kDoorbell, 0x1), kSlvErr);

    backend_done(dut, true);
    ok &= expect_eq("status done IRQ pending", axil_read(dut, kStatus), 0x0000'0015U);
    ok &= expect_eq("IRQ_STATUS done", axil_read(dut, kIrqStatus), 0x0000'0001U);
    ok &= expect_eq("irq asserted", dut.irq_o, 1U);
    ok &= expect_eq("desc count", axil_read(dut, kPerfDescCount), 1U);
    ok &= expect_eq("busy cycles nonzero", axil_read(dut, kPerfBusyLo) > 0 ? 1U : 0U, 1U);

    ok &= expect_eq("clear done", axil_write(dut, kClear, 0x1), kOkay);
    ok &= expect_eq("status idle after done clear", axil_read(dut, kStatus), 0x0000'0001U);
    ok &= expect_eq("IRQ_STATUS clear done", axil_read(dut, kIrqStatus), 0x0000'0000U);
    ok &= expect_eq("irq deasserted", dut.irq_o, 0U);
    return ok;
}

bool test_error_and_soft_reset(Vnpu_control_regs& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("enable error IRQ", axil_write(dut, kIrqEnable, 0x2), kOkay);
    ok &= expect_eq("doorbell accepted before error", axil_write(dut, kDoorbell, 0x1), kOkay);

    backend_error(dut, 6, true);
    ok &= expect_eq("status error IRQ pending", axil_read(dut, kStatus), 0x0000'0019U);
    ok &= expect_eq("error code", axil_read(dut, kErrorCode), 6U);
    ok &= expect_eq("IRQ_STATUS error", axil_read(dut, kIrqStatus), 0x0000'0002U);
    ok &= expect_eq("error count", axil_read(dut, kPerfErrorCount), 1U);

    ok &= expect_eq("clear error", axil_write(dut, kClear, 0x2), kOkay);
    ok &= expect_eq("status idle after error clear", axil_read(dut, kStatus), 0x0000'0001U);
    ok &= expect_eq("error code cleared", axil_read(dut, kErrorCode), 0U);

    ok &= expect_eq("write desc before reset", axil_write(dut, kDescAddrLo, 0xDEAD'BEEFU), kOkay);
    ok &= expect_eq("write irq before reset", axil_write(dut, kIrqEnable, 0x3), kOkay);
    std::uint32_t soft_reset = 0;
    std::uint32_t clear_perf = 0;
    ok &= expect_eq("soft reset", axil_write(dut, kControl, 0x1, 0xF, nullptr, &soft_reset, &clear_perf), kOkay);
    ok &= expect_eq("soft reset pulse", soft_reset, 1U);
    ok &= expect_eq("soft reset perf clear pulse", clear_perf, 1U);
    ok &= expect_eq("status after soft reset", axil_read(dut, kStatus), 0x0000'0001U);
    ok &= expect_eq("desc lo after soft reset", axil_read(dut, kDescAddrLo), 0U);
    ok &= expect_eq("irq enable after soft reset", axil_read(dut, kIrqEnable), 0U);
    ok &= expect_eq("perf cycles after soft reset", axil_read(dut, kPerfCyclesLo), 0U);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Vnpu_control_regs dut;
    bool ok = true;

    ok &= test_reset_values(dut);
    ok &= test_rw_and_illegal_access(dut);
    ok &= test_done_flow(dut);
    ok &= test_error_and_soft_reset(dut);

    dut.final();
    return ok ? 0 : 1;
}
