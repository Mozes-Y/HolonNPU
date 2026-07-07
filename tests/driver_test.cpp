#include "holon_npu_driver.h"
#include "holon_npu_isa.h"
#include "holon_npu_program.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>

namespace {

static_assert(sizeof(holon_npu_gemm_desc_t) == HOLON_NPU_DESC_SIZE);
static_assert(offsetof(holon_npu_gemm_desc_t, size_bytes) == HOLON_NPU_DESC_OFF_SIZE_BYTES);
static_assert(offsetof(holon_npu_gemm_desc_t, version) == HOLON_NPU_DESC_OFF_VERSION);
static_assert(offsetof(holon_npu_gemm_desc_t, opcode) == HOLON_NPU_DESC_OFF_OPCODE);
static_assert(offsetof(holon_npu_gemm_desc_t, flags) == HOLON_NPU_DESC_OFF_FLAGS);
static_assert(offsetof(holon_npu_gemm_desc_t, m) == HOLON_NPU_DESC_OFF_M);
static_assert(offsetof(holon_npu_gemm_desc_t, n) == HOLON_NPU_DESC_OFF_N);
static_assert(offsetof(holon_npu_gemm_desc_t, k) == HOLON_NPU_DESC_OFF_K);
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_14) == HOLON_NPU_DESC_OFF_RESERVED_14);
static_assert(offsetof(holon_npu_gemm_desc_t, a_addr) == HOLON_NPU_DESC_OFF_A_ADDR);
static_assert(offsetof(holon_npu_gemm_desc_t, b_addr) == HOLON_NPU_DESC_OFF_B_ADDR);
static_assert(offsetof(holon_npu_gemm_desc_t, c_addr) == HOLON_NPU_DESC_OFF_C_ADDR);
static_assert(offsetof(holon_npu_gemm_desc_t, a_row_stride_bytes) == HOLON_NPU_DESC_OFF_A_STRIDE);
static_assert(offsetof(holon_npu_gemm_desc_t, b_row_stride_bytes) == HOLON_NPU_DESC_OFF_B_STRIDE);
static_assert(offsetof(holon_npu_gemm_desc_t, c_row_stride_bytes) == HOLON_NPU_DESC_OFF_C_STRIDE);
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_3c) == HOLON_NPU_DESC_OFF_RESERVED_3C);
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_40) == HOLON_NPU_DESC_OFF_RESERVED_TAIL);
static_assert(offsetof(holon_npu_gemm_desc_t, reserved_78) == 0x78);
static_assert(HOLON_NPU_ABI_MAJOR == 2);
static_assert(HOLON_NPU_ABI_MINOR == 0);
static_assert(HOLON_NPU_ARRAY_K == 16);
static_assert(HOLON_NPU_ARRAY_N == 16);
static_assert((HOLON_NPU_CAP1_RESET & 0xFFU) == HOLON_NPU_ARRAY_K);
static_assert(((HOLON_NPU_CAP1_RESET >> 8U) & 0xFFU) == HOLON_NPU_ARRAY_N);
static_assert(HOLON_NPU_ISA_MAJOR == 1);
static_assert(HOLON_NPU_ISA_MINOR == 0);
static_assert(HOLON_NPU_ISA_INSTRUCTION_BITS == 32);
static_assert(HOLON_NPU_ISA_INSTRUCTION_BYTES == 4);
static_assert((HOLON_NPU_ISA_CLASS_MATRIX & HOLON_NPU_ISA_CLASS_MATRIX_MASK) ==
              HOLON_NPU_ISA_CLASS_MATRIX);
static_assert((HOLON_NPU_ISA_CLASS_RESERVED_F & HOLON_NPU_ISA_CLASS_SYSTEM_MASK) !=
              HOLON_NPU_ISA_CLASS_SYSTEM);
static_assert(HOLON_NPU_V2_ABI_MAJOR == 3);
static_assert(HOLON_NPU_V2_ABI_MINOR == 0);
static_assert(HOLON_NPU_V2_ABI_VERSION_RESET == 0x0003'0000U);
static_assert(HOLON_NPU_PROGRAM_FORMAT_HOLON_V2 == 1);
static_assert(sizeof(holon_npu_program_desc_t) == HOLON_NPU_PROGRAM_DESC_SIZE);
static_assert(offsetof(holon_npu_program_desc_t, size_bytes) ==
              HOLON_NPU_PROGRAM_DESC_OFF_SIZE_BYTES);
static_assert(offsetof(holon_npu_program_desc_t, version) ==
              HOLON_NPU_PROGRAM_DESC_OFF_VERSION);
static_assert(offsetof(holon_npu_program_desc_t, program_format) ==
              HOLON_NPU_PROGRAM_DESC_OFF_PROGRAM_FORMAT);
static_assert(offsetof(holon_npu_program_desc_t, required_caps) ==
              HOLON_NPU_PROGRAM_DESC_OFF_REQUIRED_CAPS);
static_assert(offsetof(holon_npu_program_desc_t, required_op_classes) ==
              HOLON_NPU_PROGRAM_DESC_OFF_REQUIRED_OP_CLASSES);
static_assert(offsetof(holon_npu_program_desc_t, code_addr) ==
              HOLON_NPU_PROGRAM_DESC_OFF_CODE_ADDR);
static_assert(offsetof(holon_npu_program_desc_t, flags) == HOLON_NPU_PROGRAM_DESC_OFF_FLAGS);
static_assert(offsetof(holon_npu_program_desc_t, reserved_78) == 0x78);
static_assert(HOLON_NPU_PROGRAM_OP_CLASS_MATRIX != 0);
static_assert(HOLON_NPU_PROGRAM_OP_CLASS_VECTOR != 0);
static_assert(HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR != 0);
static_assert(HOLON_NPU_V2_CAP_INTEGER_QUANT_VECTOR != 0);
static_assert(HOLON_NPU_V2_STATUS_IDLE == 0x0000'0001U);
static_assert(HOLON_NPU_V2_STATUS_FAULT == 0x0000'0020U);
static_assert(HOLON_NPU_V2_FAULT_UNSUPPORTED_ABI_OR_ISA == 2);
static_assert(HOLON_NPU_V2_FAULT_ILLEGAL_INSTRUCTION == 8);

using RegFile = std::array<std::uint32_t, 256>;

std::size_t idx(std::uint32_t offset) {
    return offset / sizeof(std::uint32_t);
}

void reset_regs(RegFile& regs) {
    regs.fill(0);
    regs.at(idx(HOLON_NPU_REG_DEVICE_ID)) = HOLON_NPU_DEVICE_ID_RESET;
    regs.at(idx(HOLON_NPU_REG_ABI_VERSION)) = HOLON_NPU_ABI_VERSION_RESET;
    regs.at(idx(HOLON_NPU_REG_CAP0)) = HOLON_NPU_CAP0_RESET;
    regs.at(idx(HOLON_NPU_REG_CAP1)) = HOLON_NPU_CAP1_RESET;
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_IDLE;
}

bool expect_eq(std::string_view name, std::uint64_t actual, std::uint64_t expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

holon_npu_gemm_config_t valid_config() {
    return holon_npu_gemm_config_t{
        17,
        19,
        23,
        HOLON_NPU_DESC_FLAG_IRQ_ON_DONE | HOLON_NPU_DESC_FLAG_CLEAR_PERF_ON_START,
        0x1000,
        0x2000,
        0x3000,
        32,
        32,
        80,
    };
}

bool reserved_tail_is_zero(const holon_npu_gemm_desc_t& desc) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&desc);
    for (std::size_t offset = 0x40; offset < HOLON_NPU_DESC_SIZE; ++offset) {
        if (bytes[offset] != 0) {
            return false;
        }
    }
    return desc.reserved_14 == 0 && desc.reserved_3c == 0;
}

bool test_init_and_caps() {
    RegFile regs;
    reset_regs(regs);

    holon_npu_dev_t dev{};
    holon_npu_caps_t caps{};

    bool ok = true;
    ok &= expect_eq("init null dev", holon_npu_init(nullptr, regs.data()), HOLON_NPU_E_ARG);
    ok &= expect_eq("init null base", holon_npu_init(&dev, nullptr), HOLON_NPU_E_ARG);
    ok &= expect_eq("init", holon_npu_init(&dev, regs.data()), HOLON_NPU_OK);
    ok &= expect_eq("get caps", holon_npu_get_caps(&dev, &caps), HOLON_NPU_OK);
    ok &= expect_eq("device id", caps.device_id, HOLON_NPU_DEVICE_ID_RESET);
    ok &= expect_eq("abi version", caps.abi_version, HOLON_NPU_ABI_VERSION_RESET);
    ok &= expect_eq("cap0", caps.cap0, HOLON_NPU_CAP0_RESET);
    ok &= expect_eq("cap1", caps.cap1, HOLON_NPU_CAP1_RESET);
    ok &= expect_eq("cap1 array k", caps.cap1 & 0xFFU, HOLON_NPU_ARRAY_K);
    ok &= expect_eq("cap1 array n", (caps.cap1 >> 8U) & 0xFFU, HOLON_NPU_ARRAY_N);
    ok &= expect_eq("caps null", holon_npu_get_caps(&dev, nullptr), HOLON_NPU_E_ARG);
    return ok;
}

bool test_build_descriptor() {
    holon_npu_gemm_desc_t desc{};
    auto cfg = valid_config();

    bool ok = true;
    ok &= expect_eq("build desc", holon_npu_build_gemm_desc(&desc, &cfg), HOLON_NPU_OK);
    ok &= expect_eq("desc size", desc.size_bytes, HOLON_NPU_DESC_SIZE);
    ok &= expect_eq("desc version", desc.version, HOLON_NPU_ABI_MAJOR);
    ok &= expect_eq("desc opcode", desc.opcode, HOLON_NPU_OPCODE_GEMM_I8I8I32);
    ok &= expect_eq("desc flags", desc.flags, cfg.flags);
    ok &= expect_eq("desc m", desc.m, cfg.m);
    ok &= expect_eq("desc n", desc.n, cfg.n);
    ok &= expect_eq("desc k", desc.k, cfg.k);
    ok &= expect_eq("desc a", desc.a_addr, cfg.a_addr);
    ok &= expect_eq("desc b", desc.b_addr, cfg.b_addr);
    ok &= expect_eq("desc c", desc.c_addr, cfg.c_addr);
    ok &= expect_eq("desc a stride", desc.a_row_stride_bytes, cfg.a_row_stride_bytes);
    ok &= expect_eq("desc b stride", desc.b_row_stride_bytes, cfg.b_row_stride_bytes);
    ok &= expect_eq("desc c stride", desc.c_row_stride_bytes, cfg.c_row_stride_bytes);
    ok &= expect_eq("desc reserved zero", reserved_tail_is_zero(desc) ? 1U : 0U, 1U);

    ok &= expect_eq("build null desc", holon_npu_build_gemm_desc(nullptr, &cfg), HOLON_NPU_E_ARG);
    ok &= expect_eq("build null cfg", holon_npu_build_gemm_desc(&desc, nullptr), HOLON_NPU_E_ARG);

    auto bad = cfg;
    bad.flags = 0x8;
    ok &= expect_eq("bad flags", holon_npu_build_gemm_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = cfg;
    bad.m = 0;
    ok &= expect_eq("zero dim", holon_npu_build_gemm_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = cfg;
    bad.n = 65536;
    ok &= expect_eq("unsupported dim", holon_npu_build_gemm_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = cfg;
    bad.a_addr = 0x1001;
    ok &= expect_eq("bad address alignment", holon_npu_build_gemm_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = cfg;
    bad.b_row_stride_bytes = 20;
    ok &= expect_eq("bad stride alignment", holon_npu_build_gemm_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = cfg;
    bad.c_row_stride_bytes = 64;
    ok &= expect_eq("short c stride", holon_npu_build_gemm_desc(&desc, &bad), HOLON_NPU_E_ARG);
    return ok;
}

bool test_submit_poll_wait() {
    RegFile regs;
    reset_regs(regs);

    holon_npu_dev_t dev{};
    bool ok = true;
    ok &= expect_eq("init submit dev", holon_npu_init(&dev, regs.data()), HOLON_NPU_OK);

    const std::uint64_t desc_pa = 0x1234'5678'9ABC'DEF0ULL;
    ok &= expect_eq("submit", holon_npu_submit(&dev, desc_pa), HOLON_NPU_OK);
    ok &= expect_eq("desc lo", regs.at(idx(HOLON_NPU_REG_DESC_ADDR_LO)), 0x9ABC'DEF0U);
    ok &= expect_eq("desc hi", regs.at(idx(HOLON_NPU_REG_DESC_ADDR_HI)), 0x1234'5678U);
    ok &= expect_eq("doorbell", regs.at(idx(HOLON_NPU_REG_DOORBELL)), HOLON_NPU_DOORBELL_START);

    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_BUSY;
    regs.at(idx(HOLON_NPU_REG_DESC_ADDR_LO)) = 0x1111'1111U;
    ok &= expect_eq("submit busy", holon_npu_submit(&dev, desc_pa), HOLON_NPU_E_BUSY);
    ok &= expect_eq("busy submit no desc write", regs.at(idx(HOLON_NPU_REG_DESC_ADDR_LO)), 0x1111'1111U);
    ok &= expect_eq("submit unaligned", holon_npu_submit(&dev, desc_pa + 1), HOLON_NPU_E_ARG);

    holon_npu_status_t status{};
    ok &= expect_eq("poll busy", holon_npu_poll(&dev, &status), HOLON_NPU_OK);
    ok &= expect_eq("poll busy raw", status.raw, HOLON_NPU_STATUS_BUSY);
    ok &= expect_eq("poll busy decoded", status.busy, 1U);

    ok &= expect_eq("wait timeout", holon_npu_wait(&dev, 2, &status), HOLON_NPU_E_TIMEOUT);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_IDLE | HOLON_NPU_STATUS_DONE | HOLON_NPU_STATUS_IRQ_PENDING;
    ok &= expect_eq("wait done", holon_npu_wait(&dev, 0, &status), HOLON_NPU_OK);
    ok &= expect_eq("wait done decoded", status.done, 1U);
    ok &= expect_eq("wait irq decoded", status.irq_pending, 1U);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_IDLE | HOLON_NPU_STATUS_ERROR;
    ok &= expect_eq("wait error", holon_npu_wait(&dev, 0, &status), HOLON_NPU_OK);
    ok &= expect_eq("wait error decoded", status.error, 1U);
    return ok;
}

bool test_error_clear_perf() {
    RegFile regs;
    reset_regs(regs);

    holon_npu_dev_t dev{};
    bool ok = true;
    ok &= expect_eq("init clear dev", holon_npu_init(&dev, regs.data()), HOLON_NPU_OK);

    regs.at(idx(HOLON_NPU_REG_ERROR_CODE)) = HOLON_NPU_ERR_AXI_READ;
    std::uint32_t error = 0;
    ok &= expect_eq("read error", holon_npu_error(&dev, &error), HOLON_NPU_OK);
    ok &= expect_eq("error code", error, HOLON_NPU_ERR_AXI_READ);
    ok &= expect_eq("read error null", holon_npu_error(&dev, nullptr), HOLON_NPU_E_ARG);

    regs.at(idx(HOLON_NPU_REG_CLEAR)) = 0;
    regs.at(idx(HOLON_NPU_REG_IRQ_STATUS)) = 0;
    ok &= expect_eq(
        "clear",
        holon_npu_clear(&dev, HOLON_NPU_CLEAR_DONE | HOLON_NPU_CLEAR_ERROR | HOLON_NPU_CLEAR_PERF),
        HOLON_NPU_OK
    );
    ok &= expect_eq("clear register", regs.at(idx(HOLON_NPU_REG_CLEAR)), 0x7U);
    ok &= expect_eq("irq status not directly written", regs.at(idx(HOLON_NPU_REG_IRQ_STATUS)), 0x0U);
    ok &= expect_eq("bad clear mask", holon_npu_clear(&dev, 0x8000'0000U), HOLON_NPU_E_ARG);

    regs.at(idx(HOLON_NPU_REG_PERF_CYCLES_LO)) = 0xFFFF'FFF0U;
    regs.at(idx(HOLON_NPU_REG_PERF_CYCLES_HI)) = 0x0000'0001U;
    regs.at(idx(HOLON_NPU_REG_PERF_BUSY_LO)) = 0x0000'1234U;
    regs.at(idx(HOLON_NPU_REG_PERF_BUSY_HI)) = 0x0000'0002U;
    regs.at(idx(HOLON_NPU_REG_PERF_DESC_COUNT)) = 7;
    regs.at(idx(HOLON_NPU_REG_PERF_ERROR_COUNT)) = 3;

    holon_npu_perf_t perf{};
    ok &= expect_eq("read perf", holon_npu_read_perf(&dev, &perf), HOLON_NPU_OK);
    ok &= expect_eq("perf cycles", perf.cycles, 0x0000'0001'FFFF'FFF0ULL);
    ok &= expect_eq("perf busy", perf.busy_cycles, 0x0000'0002'0000'1234ULL);
    ok &= expect_eq("perf desc", perf.desc_count, 7U);
    ok &= expect_eq("perf error", perf.error_count, 3U);
    ok &= expect_eq("read perf null", holon_npu_read_perf(&dev, nullptr), HOLON_NPU_E_ARG);
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_init_and_caps();
    ok &= test_build_descriptor();
    ok &= test_submit_poll_wait();
    ok &= test_error_clear_perf();
    return ok ? 0 : 1;
}
