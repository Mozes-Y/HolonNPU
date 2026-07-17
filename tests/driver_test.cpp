#include "holon_npu_driver.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {

constexpr std::size_t kRegisterBytes = 0x100;
using RegFile = std::array<std::uint32_t, kRegisterBytes / sizeof(std::uint32_t)>;

static_assert(HOLON_NPU_ABI_VERSION_RESET == 0x0003'0000U);
static_assert(HOLON_NPU_PROGRAM_FORMAT_HOLON == 1U);
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
static_assert(offsetof(holon_npu_program_desc_t, flags) ==
              HOLON_NPU_PROGRAM_DESC_OFF_FLAGS);
static_assert(sizeof(holon_npu_completion_record_t) == HOLON_NPU_COMPLETION_RECORD_SIZE);
static_assert(offsetof(holon_npu_completion_record_t, abi_version) ==
              HOLON_NPU_COMPLETION_OFF_ABI_VERSION);
static_assert(offsetof(holon_npu_completion_record_t, status) ==
              HOLON_NPU_COMPLETION_OFF_STATUS);
static_assert(offsetof(holon_npu_completion_record_t, fault_code) ==
              HOLON_NPU_COMPLETION_OFF_FAULT_CODE);
static_assert(offsetof(holon_npu_completion_record_t, debug_pc) ==
              HOLON_NPU_COMPLETION_OFF_DEBUG_PC);
static_assert(offsetof(holon_npu_completion_record_t, cycle_count) ==
              HOLON_NPU_COMPLETION_OFF_CYCLE_COUNT);
static_assert(offsetof(holon_npu_completion_record_t, instret) ==
              HOLON_NPU_COMPLETION_OFF_INSTRET);
static_assert(HOLON_NPU_PROGRAM_OP_CLASS_MATRIX != 0U);
static_assert(HOLON_NPU_PROGRAM_OP_CLASS_VECTOR != 0U);
static_assert(HOLON_NPU_DOORBELL_START == 1U);

constexpr std::size_t idx(std::uint32_t offset) {
    return offset / sizeof(std::uint32_t);
}

template <typename Actual, typename Expected>
bool expect_eq(std::string_view name, Actual actual, Expected expected) {
    using Common = std::common_type_t<Actual, Expected>;
    if (static_cast<Common>(actual) == static_cast<Common>(expected)) {
        return true;
    }
    std::cerr << name << ": expected " << static_cast<Common>(expected)
              << ", got " << static_cast<Common>(actual) << '\n';
    return false;
}

void reset_regs(RegFile& regs) {
    regs.fill(0);
    regs.at(idx(HOLON_NPU_REG_DEVICE_ID)) = HOLON_NPU_RESET_DEVICE_ID;
    regs.at(idx(HOLON_NPU_REG_ABI_VERSION)) = HOLON_NPU_RESET_ABI_VERSION;
    regs.at(idx(HOLON_NPU_REG_ISA_VERSION)) = HOLON_NPU_RESET_ISA_VERSION;
    regs.at(idx(HOLON_NPU_REG_CAP0_LO)) = HOLON_NPU_RESET_CAP0_LO;
    regs.at(idx(HOLON_NPU_REG_CAP0_HI)) = HOLON_NPU_RESET_CAP0_HI;
    regs.at(idx(HOLON_NPU_REG_OP_CLASS_LO)) = HOLON_NPU_RESET_OP_CLASS_LO;
    regs.at(idx(HOLON_NPU_REG_OP_CLASS_HI)) = HOLON_NPU_RESET_OP_CLASS_HI;
    regs.at(idx(HOLON_NPU_REG_PROGRAM_MEM_BYTES)) = HOLON_NPU_RESET_PROGRAM_MEM_BYTES;
    regs.at(idx(HOLON_NPU_REG_LOCAL_MEM_BYTES)) = HOLON_NPU_RESET_LOCAL_MEM_BYTES;
    regs.at(idx(HOLON_NPU_REG_VECTOR_CAP0)) = HOLON_NPU_RESET_VECTOR_CAP0;
    regs.at(idx(HOLON_NPU_REG_MATRIX_CAP0)) = HOLON_NPU_RESET_MATRIX_CAP0;
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_IDLE;
}

holon_npu_program_config_t valid_config() {
    return holon_npu_program_config_t{
        .required_caps = HOLON_NPU_CAP_PROGRAM_DESCRIPTOR |
                         HOLON_NPU_CAP_LOCAL_PROGRAM_MEMORY |
                         HOLON_NPU_CAP_INTEGER_VECTOR_BASE |
                         HOLON_NPU_CAP_MATRIX_MICRO_OP,
        .required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
                               HOLON_NPU_PROGRAM_OP_CLASS_MATRIX |
                               HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        .code_addr = 0x2000,
        .code_size_bytes = 64,
        .entry_pc = 4,
        .arg_addr = 0x3000,
        .arg_size_bytes = 32,
        .local_mem_bytes = 256,
        .program_mem_bytes = 64,
        .stack_bytes = 128,
        .completion_addr = 0x4000,
        .flags = HOLON_NPU_PROGRAM_FLAG_IRQ_ON_DONE |
                 HOLON_NPU_PROGRAM_FLAG_IRQ_ON_FAULT |
                 HOLON_NPU_PROGRAM_FLAG_CLEAR_PERF_ON_START |
                 HOLON_NPU_PROGRAM_FLAG_DEBUG_SNAPSHOT_ON_FAULT,
    };
}

bool descriptor_reserved_zero(const holon_npu_program_desc_t& desc) {
    return desc.reserved_4c == 0U && desc.reserved_50 == 0U &&
           desc.reserved_58 == 0U && desc.reserved_60 == 0U &&
           desc.reserved_68 == 0U && desc.reserved_70 == 0U &&
           desc.reserved_78 == 0U;
}

bool test_init_and_caps() {
    RegFile regs;
    reset_regs(regs);
    regs.at(idx(HOLON_NPU_REG_CAP0_HI)) = 0x1234'5678U;
    regs.at(idx(HOLON_NPU_REG_OP_CLASS_HI)) = 0x8765'4321U;

    holon_npu_dev_t dev{};
    holon_npu_caps_t caps{};
    bool ok = true;
    ok &= expect_eq("init", holon_npu_init(&dev, regs.data()), HOLON_NPU_OK);
    ok &= expect_eq("init null dev", holon_npu_init(nullptr, regs.data()), HOLON_NPU_E_ARG);
    ok &= expect_eq("init null base", holon_npu_init(&dev, nullptr), HOLON_NPU_E_ARG);
    ok &= expect_eq("caps", holon_npu_get_caps(&dev, &caps), HOLON_NPU_OK);
    ok &= expect_eq("device id", caps.device_id, HOLON_NPU_RESET_DEVICE_ID);
    ok &= expect_eq("ABI", caps.abi_version, HOLON_NPU_ABI_VERSION_RESET);
    ok &= expect_eq("ISA", caps.isa_version, HOLON_NPU_RESET_ISA_VERSION);
    ok &= expect_eq(
        "caps64",
        caps.capabilities,
        (std::uint64_t{0x1234'5678U} << 32U) | HOLON_NPU_RESET_CAP0_LO
    );
    ok &= expect_eq(
        "op classes64",
        caps.operation_classes,
        (std::uint64_t{0x8765'4321U} << 32U) | HOLON_NPU_RESET_OP_CLASS_LO
    );
    ok &= expect_eq("program memory", caps.program_mem_bytes, HOLON_NPU_PROGRAM_MEM_MAX_BYTES);
    ok &= expect_eq("local memory", caps.local_mem_bytes, HOLON_NPU_LOCAL_MEM_MAX_BYTES);
    ok &= expect_eq("vector caps", caps.vector_cap0, HOLON_NPU_RESET_VECTOR_CAP0);
    ok &= expect_eq("matrix caps", caps.matrix_cap0, HOLON_NPU_RESET_MATRIX_CAP0);
    ok &= expect_eq("caps null", holon_npu_get_caps(&dev, nullptr), HOLON_NPU_E_ARG);
    return ok;
}

bool test_build_descriptor() {
    auto config = valid_config();
    holon_npu_program_desc_t desc{};
    bool ok = true;

    ok &= expect_eq("build", holon_npu_build_program_desc(&desc, &config), HOLON_NPU_OK);
    ok &= expect_eq("size", desc.size_bytes, HOLON_NPU_PROGRAM_DESC_SIZE);
    ok &= expect_eq("version", desc.version, HOLON_NPU_ABI_MAJOR);
    ok &= expect_eq("format", desc.program_format, HOLON_NPU_PROGRAM_FORMAT_HOLON);
    ok &= expect_eq("ISA major", desc.holon_isa_major, HOLON_NPU_ISA_MAJOR);
    ok &= expect_eq("ISA minor", desc.holon_isa_minor, HOLON_NPU_ISA_MINOR);
    ok &= expect_eq("required caps", desc.required_caps, config.required_caps);
    ok &= expect_eq("required classes", desc.required_op_classes, config.required_op_classes);
    ok &= expect_eq("code", desc.code_addr, config.code_addr);
    ok &= expect_eq("code size", desc.code_size_bytes, config.code_size_bytes);
    ok &= expect_eq("entry", desc.entry_pc, config.entry_pc);
    ok &= expect_eq("args", desc.arg_addr, config.arg_addr);
    ok &= expect_eq("args size", desc.arg_size_bytes, config.arg_size_bytes);
    ok &= expect_eq("local memory", desc.local_mem_bytes, config.local_mem_bytes);
    ok &= expect_eq("program memory", desc.program_mem_bytes, config.program_mem_bytes);
    ok &= expect_eq("stack", desc.stack_bytes, config.stack_bytes);
    ok &= expect_eq("completion", desc.completion_addr, config.completion_addr);
    ok &= expect_eq("flags", desc.flags, config.flags);
    ok &= expect_eq("reserved zero", descriptor_reserved_zero(desc), true);
    ok &= expect_eq("null desc", holon_npu_build_program_desc(nullptr, &config), HOLON_NPU_E_ARG);
    ok &= expect_eq("null config", holon_npu_build_program_desc(&desc, nullptr), HOLON_NPU_E_ARG);

    auto bad = config;
    bad.flags = HOLON_NPU_PROGRAM_FLAG_VALID_MASK << 1U;
    ok &= expect_eq("bad flags", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.code_addr += 1;
    ok &= expect_eq("bad code address", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.code_size_bytes = 0;
    ok &= expect_eq("zero code", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.code_size_bytes += 2;
    ok &= expect_eq("bad code size alignment", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.entry_pc = bad.code_size_bytes;
    ok &= expect_eq("entry outside code", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.arg_addr += 4;
    ok &= expect_eq("bad argument address", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.arg_size_bytes += 4;
    ok &= expect_eq("bad argument size", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.completion_addr += 4;
    ok &= expect_eq("bad completion", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.program_mem_bytes = bad.code_size_bytes - 4;
    ok &= expect_eq("short program memory", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.local_mem_bytes = bad.arg_size_bytes - 1;
    ok &= expect_eq("short local memory", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.stack_bytes = HOLON_NPU_PROGRAM_STACK_MAX_BYTES + 1U;
    ok &= expect_eq("large stack", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.stack_bytes = bad.local_mem_bytes - bad.arg_size_bytes + 1U;
    ok &= expect_eq("argument and stack overlap", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.code_addr = std::numeric_limits<std::uint64_t>::max() - 15U;
    ok &= expect_eq("code address overflow", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.arg_addr = std::numeric_limits<std::uint64_t>::max() - 15U;
    ok &= expect_eq("argument address overflow", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    bad = config;
    bad.completion_addr = std::numeric_limits<std::uint64_t>::max() - 15U;
    ok &= expect_eq("completion address overflow", holon_npu_build_program_desc(&desc, &bad), HOLON_NPU_E_ARG);
    return ok;
}

bool test_submit_poll_wait() {
    RegFile regs;
    reset_regs(regs);
    holon_npu_dev_t dev{};
    bool ok = true;
    ok &= expect_eq("init", holon_npu_init(&dev, regs.data()), HOLON_NPU_OK);

    constexpr std::uint64_t desc_pa = 0x1234'5678'9ABC'DEF0ULL;
    ok &= expect_eq("submit", holon_npu_submit_program(&dev, desc_pa), HOLON_NPU_OK);
    ok &= expect_eq("desc lo", regs.at(idx(HOLON_NPU_REG_PROGRAM_DESC_ADDR_LO)), 0x9ABC'DEF0U);
    ok &= expect_eq("desc hi", regs.at(idx(HOLON_NPU_REG_PROGRAM_DESC_ADDR_HI)), 0x1234'5678U);
    ok &= expect_eq("doorbell", regs.at(idx(HOLON_NPU_REG_DOORBELL)), HOLON_NPU_DOORBELL_START);
    ok &= expect_eq("unaligned submit", holon_npu_submit_program(&dev, desc_pa + 1U), HOLON_NPU_E_ARG);
    ok &= expect_eq(
        "overflowing submit",
        holon_npu_submit_program(&dev, std::numeric_limits<std::uint64_t>::max() - 15U),
        HOLON_NPU_E_ARG
    );

    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_LOADING;
    ok &= expect_eq("loading submit", holon_npu_submit_program(&dev, desc_pa), HOLON_NPU_E_BUSY);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_DONE;
    ok &= expect_eq("terminal submit", holon_npu_submit_program(&dev, desc_pa), HOLON_NPU_E_STATE);

    holon_npu_status_t status{};
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_RUNNING;
    ok &= expect_eq("poll", holon_npu_poll(&dev, &status), HOLON_NPU_OK);
    ok &= expect_eq("running decoded", status.running, 1);
    ok &= expect_eq("wait timeout", holon_npu_wait(&dev, 2, &status), HOLON_NPU_E_TIMEOUT);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_DONE |
                                             HOLON_NPU_STATUS_IRQ_PENDING;
    ok &= expect_eq("wait done", holon_npu_wait(&dev, 0, &status), HOLON_NPU_OK);
    ok &= expect_eq("done decoded", status.done, 1);
    ok &= expect_eq("IRQ decoded", status.irq_pending, 1);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_FAULT;
    ok &= expect_eq("wait fault", holon_npu_wait(&dev, 0, &status), HOLON_NPU_OK);
    ok &= expect_eq("fault decoded", status.fault, 1);
    ok &= expect_eq("poll null", holon_npu_poll(&dev, nullptr), HOLON_NPU_E_ARG);
    return ok;
}

bool test_lifecycle_irq_fault_perf() {
    RegFile regs;
    reset_regs(regs);
    holon_npu_dev_t dev{};
    bool ok = true;
    ok &= expect_eq("init", holon_npu_init(&dev, regs.data()), HOLON_NPU_OK);

    ok &= expect_eq("halt wrong state", holon_npu_halt(&dev), HOLON_NPU_E_STATE);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_RUNNING;
    ok &= expect_eq("halt", holon_npu_halt(&dev), HOLON_NPU_OK);
    ok &= expect_eq("halt command", regs.at(idx(HOLON_NPU_REG_CONTROL)), HOLON_NPU_CONTROL_HALT);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_HALTED;
    ok &= expect_eq("resume", holon_npu_resume(&dev), HOLON_NPU_OK);
    ok &= expect_eq("resume command", regs.at(idx(HOLON_NPU_REG_CONTROL)), HOLON_NPU_CONTROL_RESUME);
    ok &= expect_eq("debug step", holon_npu_debug_step(&dev), HOLON_NPU_OK);
    ok &= expect_eq("debug command", regs.at(idx(HOLON_NPU_REG_CONTROL)), HOLON_NPU_CONTROL_DEBUG_STEP);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_DONE;
    ok &= expect_eq("clear terminal", holon_npu_clear_terminal(&dev), HOLON_NPU_OK);
    ok &= expect_eq("clear command", regs.at(idx(HOLON_NPU_REG_CONTROL)), HOLON_NPU_CONTROL_CLEAR_TERMINAL);
    ok &= expect_eq("soft reset", holon_npu_soft_reset(&dev), HOLON_NPU_OK);
    ok &= expect_eq("reset command", regs.at(idx(HOLON_NPU_REG_CONTROL)), HOLON_NPU_CONTROL_SOFT_RESET);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_RESETTING;
    holon_npu_status_t reset_status{};
    ok &= expect_eq("poll resetting", holon_npu_poll(&dev, &reset_status), HOLON_NPU_OK);
    ok &= expect_eq("resetting decoded", reset_status.resetting, 1);
    ok &= expect_eq("repeat reset state", holon_npu_soft_reset(&dev), HOLON_NPU_E_STATE);
    ok &= expect_eq("wait idle timeout", holon_npu_wait_idle(&dev, 1, &reset_status), HOLON_NPU_E_TIMEOUT);
    regs.at(idx(HOLON_NPU_REG_STATUS)) = HOLON_NPU_STATUS_IDLE;
    ok &= expect_eq("wait idle", holon_npu_wait_idle(&dev, 0, &reset_status), HOLON_NPU_OK);

    regs.at(idx(HOLON_NPU_REG_FAULT_CODE)) = HOLON_NPU_FAULT_AXI_WRITE;
    regs.at(idx(HOLON_NPU_REG_DEBUG_PC)) = 0x80U;
    holon_npu_fault_snapshot_t fault{};
    ok &= expect_eq("fault snapshot", holon_npu_get_fault_snapshot(&dev, &fault), HOLON_NPU_OK);
    ok &= expect_eq("fault code", fault.code, HOLON_NPU_FAULT_AXI_WRITE);
    ok &= expect_eq("fault pc", fault.pc, 0x80U);

    const auto irq_mask = HOLON_NPU_IRQ_DONE | HOLON_NPU_IRQ_FAULT;
    ok &= expect_eq("enable IRQ", holon_npu_set_irq_enable(&dev, irq_mask), HOLON_NPU_OK);
    ok &= expect_eq("IRQ enable register", regs.at(idx(HOLON_NPU_REG_IRQ_ENABLE)), irq_mask);
    ok &= expect_eq("bad IRQ enable", holon_npu_set_irq_enable(&dev, 0x10U), HOLON_NPU_E_ARG);
    regs.at(idx(HOLON_NPU_REG_IRQ_STATUS)) = irq_mask | 0x8000'0000U;
    std::uint32_t irq_status = 0;
    ok &= expect_eq("get IRQ", holon_npu_get_irq_status(&dev, &irq_status), HOLON_NPU_OK);
    ok &= expect_eq("IRQ masked", irq_status, irq_mask);
    ok &= expect_eq("clear IRQ", holon_npu_clear_irq(&dev, irq_mask), HOLON_NPU_OK);
    ok &= expect_eq("IRQ clear register", regs.at(idx(HOLON_NPU_REG_IRQ_CLEAR)), irq_mask);

    regs.at(idx(HOLON_NPU_REG_PERF_CYCLE_LO)) = 0xFFFF'FFF0U;
    regs.at(idx(HOLON_NPU_REG_PERF_CYCLE_HI)) = 1U;
    regs.at(idx(HOLON_NPU_REG_PERF_INSTRET_LO)) = 0x1234U;
    regs.at(idx(HOLON_NPU_REG_PERF_INSTRET_HI)) = 2U;
    holon_npu_perf_t perf{};
    ok &= expect_eq("perf", holon_npu_read_perf(&dev, &perf), HOLON_NPU_OK);
    ok &= expect_eq("cycles", perf.cycles, 0x0000'0001'FFFF'FFF0ULL);
    ok &= expect_eq("instret", perf.instructions_retired, 0x0000'0002'0000'1234ULL);
    ok &= expect_eq("perf null", holon_npu_read_perf(&dev, nullptr), HOLON_NPU_E_ARG);
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_init_and_caps();
    ok &= test_build_descriptor();
    ok &= test_submit_poll_wait();
    ok &= test_lifecycle_irq_fault_perf();
    return ok ? 0 : 1;
}
