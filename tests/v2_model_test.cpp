#include "holon_npu_v2_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using holon_npu::v2::model::class_name;
using holon_npu::v2::model::csr;
using holon_npu::v2::model::decode;
using holon_npu::v2::model::dma_direction;
using holon_npu::v2::model::disassemble;
using holon_npu::v2::model::encode_scalar_add;
using holon_npu::v2::model::encode_scalar_beq;
using holon_npu::v2::model::encode_scalar_load;
using holon_npu::v2::model::encode_scalar_movi;
using holon_npu::v2::model::encode_csr_read;
using holon_npu::v2::model::encode_dma_load;
using holon_npu::v2::model::encode_dma_store;
using holon_npu::v2::model::encode_matrix_gemm;
using holon_npu::v2::model::encode_sync_fence_dma;
using holon_npu::v2::model::encode_sync_fence_local;
using holon_npu::v2::model::encode_sync_wait_dma;
using holon_npu::v2::model::encode_system_exit;
using holon_npu::v2::model::encode_system_fault;
using holon_npu::v2::model::encode_predicate_load;
using holon_npu::v2::model::encode_predicate_ptrue;
using holon_npu::v2::model::encode_vector_add;
using holon_npu::v2::model::encode_vector_eq;
using holon_npu::v2::model::encode_vector_load;
using holon_npu::v2::model::encode_vector_lt;
using holon_npu::v2::model::encode_vector_max;
using holon_npu::v2::model::encode_vector_min;
using holon_npu::v2::model::encode_vector_gather;
using holon_npu::v2::model::encode_vector_reduce_sum;
using holon_npu::v2::model::encode_vector_select;
using holon_npu::v2::model::encode_quant_requantize;
using holon_npu::v2::model::encode_vector_shl;
using holon_npu::v2::model::encode_vector_store;
using holon_npu::v2::model::encode_vector_sra;
using holon_npu::v2::model::encode_vector_srl;
using holon_npu::v2::model::encode_vector_sub;
using holon_npu::v2::model::lifecycle_state;
using holon_npu::v2::model::machine;
using holon_npu::v2::model::matrix_gemm_i8_i32_op;
using holon_npu::v2::model::model_error;
using holon_npu::v2::model::program_builder;
using holon_npu::v2::model::vector_element_width;
using holon_npu::v2::model::vector_rounding;

bool expect_true(std::string_view name, bool condition) {
    if (condition) {
        return true;
    }
    std::cerr << name << " failed\n";
    return false;
}

template <typename T>
bool expect_eq(std::string_view name, T actual, T expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << name << ": expected " << static_cast<std::uint64_t>(expected)
              << ", got " << static_cast<std::uint64_t>(actual) << '\n';
    return false;
}

template <typename ActualRange, typename ExpectedRange>
bool expect_vector_eq(
    std::string_view name,
    const ActualRange& actual,
    const ExpectedRange& expected
) {
    if (actual.size() != expected.size()) {
        std::cerr << name << ": size mismatch\n";
        return false;
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (actual[index] != expected[index]) {
            std::cerr << name << "[" << index << "]: expected "
                      << static_cast<std::int64_t>(expected[index]) << ", got "
                      << static_cast<std::int64_t>(actual[index]) << '\n';
            return false;
        }
    }
    return true;
}

std::int32_t reference_wrap_add(std::int32_t lhs, std::int32_t rhs) {
    return std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs)
    );
}

std::int32_t reference_wrap_sub(std::int32_t lhs, std::int32_t rhs) {
    return std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs)
    );
}

std::uint32_t reference_shift_count(std::int32_t value) {
    return static_cast<std::uint32_t>(value) & 31U;
}

std::int32_t reference_shl(std::int32_t value, std::uint32_t count) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) << count);
}

std::int32_t reference_srl(std::int32_t value, std::uint32_t count) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) >> count);
}

std::int32_t reference_sra(std::int32_t value, std::uint32_t count) {
    if (count == 0) {
        return value;
    }
    const auto bits = static_cast<std::uint32_t>(value);
    if (value >= 0) {
        return std::bit_cast<std::int32_t>(bits >> count);
    }
    const auto sign_fill = ~std::uint32_t{0} << (32U - count);
    return std::bit_cast<std::int32_t>((bits >> count) | sign_fill);
}

holon_npu_program_desc_t valid_program_desc(
    std::span<const std::uint32_t> program,
    std::span<const std::byte> args,
    std::uint32_t local_mem_bytes = 128
) {
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
        .code_addr = 0x1000,
        .code_size_bytes = static_cast<std::uint32_t>(program.size_bytes()),
        .entry_pc = 0,
        .arg_addr = 0x2000,
        .arg_size_bytes = static_cast<std::uint32_t>(args.size()),
        .local_mem_bytes = local_mem_bytes,
        .program_mem_bytes = static_cast<std::uint32_t>(program.size_bytes()),
        .stack_bytes = 0,
        .completion_addr = 0,
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

enum class random_vector_op : std::uint8_t {
    add,
    sub,
    min,
    max,
    eq,
    lt,
    shl,
    srl,
    sra,
};

program_builder& append_random_vector_op(
    program_builder& program,
    random_vector_op op,
    std::uint8_t vd,
    std::uint8_t vs1,
    std::uint8_t vs2
) {
    switch (op) {
        case random_vector_op::add:
            return program.add(vd, vs1, vs2);
        case random_vector_op::sub:
            return program.sub(vd, vs1, vs2);
        case random_vector_op::min:
            return program.min(vd, vs1, vs2);
        case random_vector_op::max:
            return program.max(vd, vs1, vs2);
        case random_vector_op::eq:
            return program.eq(vd, vs1, vs2);
        case random_vector_op::lt:
            return program.lt(vd, vs1, vs2);
        case random_vector_op::shl:
            return program.shl(vd, vs1, vs2);
        case random_vector_op::srl:
            return program.srl(vd, vs1, vs2);
        case random_vector_op::sra:
            return program.sra(vd, vs1, vs2);
    }
    return program.fault(model_error::illegal_instruction);
}

std::int32_t reference_random_vector_op(random_vector_op op, std::int32_t lhs, std::int32_t rhs) {
    switch (op) {
        case random_vector_op::add:
            return reference_wrap_add(lhs, rhs);
        case random_vector_op::sub:
            return reference_wrap_sub(lhs, rhs);
        case random_vector_op::min:
            return std::min(lhs, rhs);
        case random_vector_op::max:
            return std::max(lhs, rhs);
        case random_vector_op::eq:
            return lhs == rhs ? 1 : 0;
        case random_vector_op::lt:
            return lhs < rhs ? 1 : 0;
        case random_vector_op::shl:
            return reference_shl(lhs, reference_shift_count(rhs));
        case random_vector_op::srl:
            return reference_srl(lhs, reference_shift_count(rhs));
        case random_vector_op::sra:
            return reference_sra(lhs, reference_shift_count(rhs));
    }
    return 0;
}

bool test_decode_and_disassemble() {
    const auto word = encode_vector_add(3, 1, 2);
    const auto inst = decode(word);

    bool ok = true;
    ok &= expect_eq("decode class", inst.isa_class, HOLON_NPU_ISA_ENUM_VECTOR_ALU);
    ok &= expect_eq("decode rd", inst.rd, std::uint8_t{3});
    ok &= expect_eq("decode rs1", inst.rs1, std::uint8_t{1});
    ok &= expect_eq("decode rs2", inst.rs2, std::uint8_t{2});
    ok &= expect_true("class name", class_name(inst.isa_class) == "vector_alu");
    ok &= expect_true("disassemble", disassemble(inst) == "vector_alu.add v3, v1, v2, p0");
    ok &= expect_true(
        "disassemble sub",
        disassemble(decode(encode_vector_sub(4, 5, 6))) == "vector_alu.sub v4, v5, v6, p0"
    );
    ok &= expect_true(
        "disassemble eq",
        disassemble(decode(encode_vector_eq(7, 8, 9))) == "vector_alu.eq v7, v8, v9, p0"
    );
    ok &= expect_true(
        "disassemble sra",
        disassemble(decode(encode_vector_sra(10, 11, 12))) == "vector_alu.sra v10, v11, v12, p0"
    );
    ok &= expect_true(
        "disassemble predicate ptrue",
        disassemble(decode(encode_predicate_ptrue(0))) == "predicate.ptrue p0"
    );
    ok &= expect_true(
        "disassemble scalar movi",
        disassemble(decode(encode_scalar_movi(1, -7))) == "frontend_control.movi s1, -7"
    );
    ok &= expect_true(
        "disassemble scalar add",
        disassemble(decode(encode_scalar_add(3, 1, 2))) ==
            "frontend_control.add s3, s1, s2"
    );
    ok &= expect_true(
        "disassemble scalar load",
        disassemble(decode(encode_scalar_load(4, 1, -4))) ==
            "frontend_control.load s4, [s1-4]"
    );
    ok &= expect_true(
        "disassemble scalar branch",
        disassemble(decode(encode_scalar_beq(1, 2, -3))) ==
            "frontend_control.beq s1, s2, -3"
    );
    ok &= expect_true(
        "disassemble predicate load",
        disassemble(decode(encode_predicate_load(0, 0x40))) == "predicate.load p0, [0x040]"
    );
    ok &= expect_true(
        "disassemble select",
        disassemble(decode(encode_vector_select(3, 1, 2, 0))) ==
            "vector_alu.select v3, v1, v2, p0"
    );
    ok &= expect_true(
        "disassemble gather",
        disassemble(decode(encode_vector_gather(4, 1, 2, 0))) ==
            "vector_permute.gather v4, v1, v2, p0"
    );
    ok &= expect_true(
        "disassemble zip lo",
        disassemble(decode(holon_npu::v2::model::encode_vector_zip_lo(4, 1, 2, 0))) ==
            "vector_permute.zip.lo v4, v1, v2, p0"
    );
    ok &= expect_true(
        "disassemble transpose4",
        disassemble(decode(holon_npu::v2::model::encode_vector_transpose4(4, 1, 0))) ==
            "vector_permute.transpose4 v4, v1, v0, p0"
    );
    ok &= expect_true(
        "disassemble reduction",
        disassemble(decode(encode_vector_reduce_sum(5, 1, 0))) ==
            "vector_reduction.sum v5, v1, p0"
    );
    ok &= expect_true(
        "disassemble requantize",
        disassemble(decode(encode_quant_requantize(6, 1, 0, 0x80))) ==
            "quantization.requantize v6, v1, p0, [0x080]"
    );
    ok &= expect_true(
        "disassemble matrix gemm",
        disassemble(decode(encode_matrix_gemm(0, 0x80))) == "matrix.gemm a0, [0x080]"
    );
    ok &= expect_true(
        "disassemble csr read",
        disassemble(decode(encode_csr_read(3, csr::instret_lo))) ==
            "csr_debug.read s3, 0x001"
    );
    ok &= expect_true(
        "disassemble dma load",
        disassemble(decode(encode_dma_load(1, 2, 3, 4))) ==
            "dma.load sys={s2,s1}, spm=s3, words=4"
    );
    ok &= expect_true(
        "disassemble dma store",
        disassemble(decode(encode_dma_store(4, 5, 6, 8))) ==
            "dma.store sys={s5,s4}, spm=s6, words=8"
    );
    ok &= expect_true(
        "disassemble sync wait dma",
        disassemble(decode(encode_sync_wait_dma())) == "sync.wait_dma"
    );
    ok &= expect_true(
        "disassemble sync fence local",
        disassemble(decode(encode_sync_fence_local())) == "sync.fence.local"
    );
    ok &= expect_true(
        "disassemble sync fence dma",
        disassemble(decode(encode_sync_fence_dma())) == "sync.fence.dma"
    );
    ok &= expect_eq("reserved decode", decode(HOLON_NPU_ISA_CLASS_RESERVED_D).isa_class,
                    HOLON_NPU_ISA_ENUM_RESERVED_D);
    return ok;
}

bool test_minimal_vector_program() {
    machine model(128, 16);
    const std::array<std::int32_t, 8> args{1, 2, 3, 4, 10, 20, 30, 40};
    const std::array<std::int32_t, 4> expected{11, 22, 33, 44};
    program_builder program;
    program.configure(4, vector_element_width::bits_32, true)
        .load(1, 0)
        .load(2, 16)
        .add(3, 1, 2)
        .store(3, 32)
        .exit();
    const auto arg_bytes = std::as_bytes(std::span(args));
    const auto desc = valid_program_desc(program.span(), arg_bytes);

    bool ok = true;
    const auto load_result = model.load_program_descriptor(desc, program.span(), arg_bytes);
    ok &= expect_eq("descriptor load state", load_result.state, lifecycle_state::idle);
    ok &= expect_eq("descriptor load fault", load_result.fault, model_error::none);
    const auto result = model.run(16);
    ok &= expect_eq("minimal state", result.state, lifecycle_state::done);
    ok &= expect_eq("minimal fault", result.fault, model_error::none);
    ok &= expect_eq("minimal retired", result.retired, std::uint64_t{6});
    ok &= expect_eq("minimal pc", result.pc, std::uint32_t{24});
    const auto actual = model.read_i32(32, expected.size());
    ok &= expect_vector_eq("minimal vector result", actual, expected);
    return ok;
}

bool test_scalar_control_program() {
    machine model(64, 16);
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
        .fault(model_error::explicit_program_fault)
        .exit();
    model.load_program(program.span());
    const auto result = model.run(32);

    bool ok = true;
    ok &= expect_eq("scalar state", result.state, lifecycle_state::done);
    ok &= expect_eq("scalar fault", result.fault, model_error::none);
    ok &= expect_eq("scalar retired", result.retired, std::uint64_t{16});
    ok &= expect_eq("scalar result memory", model.read_i32(0, 1).at(0), std::int32_t{6});
    ok &= expect_eq("scalar counter", model.scalar_register(1), std::int32_t{0});
    ok &= expect_eq("scalar accumulator", model.scalar_register(2), std::int32_t{6});
    ok &= expect_eq("scalar loaded value", model.scalar_register(4), std::int32_t{6});
    ok &= expect_eq("scalar zero register", model.scalar_register(0), std::int32_t{0});
    return ok;
}

bool test_scalar_control_faults() {
    bool ok = true;
    {
        machine model(64, 16);
        program_builder program;
        program.beq(0, 0, -1).exit();
        model.load_program(program.span());
        const auto result = model.run(4);
        ok &= expect_eq("branch target fault state", result.state, lifecycle_state::fault);
        ok &= expect_eq("branch target fault code", result.fault, model_error::illegal_instruction);
        ok &= expect_eq("branch target fault pc", result.pc, std::uint32_t{0});
    }
    {
        machine model(64, 16);
        program_builder program;
        program.movi(1, 63).scalar_load(2, 1, 0).exit();
        model.load_program(program.span());
        const auto result = model.run(4);
        ok &= expect_eq("scalar bounds fault state", result.state, lifecycle_state::fault);
        ok &= expect_eq("scalar bounds fault code", result.fault, model_error::local_memory_bounds);
        ok &= expect_eq("scalar bounds fault pc", result.pc, std::uint32_t{4});
    }
    return ok;
}

bool test_csr_read_program() {
    machine model(128, 16);
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
    model.load_program(program.span());

    bool ok = true;
    ok &= expect_eq("csr program state", model.run(32).state, lifecycle_state::done);
    const std::array<std::int32_t, 4> expected{
        0,
        2,
        static_cast<std::int32_t>(program.size() * HOLON_NPU_ISA_INSTRUCTION_BYTES),
        128,
    };
    ok &= expect_vector_eq("csr values", model.read_i32(0, expected.size()), expected);

    program_builder invalid;
    invalid.raw(encode_csr_read(1, static_cast<csr>(0xFFF))).exit();
    model.load_program(invalid.span());
    const auto fault = model.run(4);
    ok &= expect_eq("unknown csr state", fault.state, lifecycle_state::fault);
    ok &= expect_eq("unknown csr fault", fault.fault, model_error::illegal_instruction);
    ok &= expect_eq("unknown csr pc", fault.pc, std::uint32_t{0});
    return ok;
}

bool test_descriptor_validation_faults() {
    machine model(128, 16);
    const std::array<std::int32_t, 4> args{1, 2, 3, 4};
    program_builder program;
    program.configure(4, vector_element_width::bits_32, true).exit();
    const auto arg_bytes = std::as_bytes(std::span(args));
    const auto base = valid_program_desc(program.span(), arg_bytes);

    bool ok = true;
    auto expect_fault = [&](std::string_view name, holon_npu_program_desc_t desc, model_error expected) {
        const auto result = model.load_program_descriptor(desc, program.span(), arg_bytes);
        ok &= expect_eq(name, result.fault, expected);
        ok &= expect_eq("state after descriptor fault", result.state, lifecycle_state::fault);
    };

    auto bad = base;
    bad.version = 2;
    expect_fault("bad descriptor version", bad, model_error::unsupported_abi_or_isa);

    bad = base;
    bad.program_format = 0;
    expect_fault("bad program format", bad, model_error::unsupported_program_format);

    bad = base;
    bad.required_caps = std::uint64_t{1} << 63U;
    expect_fault("bad required cap", bad, model_error::unsupported_capability);

    bad = base;
    bad.required_op_classes = std::uint64_t{1} << 63U;
    expect_fault("bad required op class", bad, model_error::unsupported_operation_class);

    bad = base;
    bad.flags = HOLON_NPU_PROGRAM_FLAG_VALID_MASK << 1U;
    expect_fault("bad flags", bad, model_error::invalid_program_descriptor);

    bad = base;
    bad.reserved_50 = 1;
    expect_fault("bad reserved", bad, model_error::invalid_program_descriptor);

    bad = base;
    bad.code_addr = 0x1002;
    expect_fault("bad code alignment", bad, model_error::alignment);

    bad = base;
    bad.entry_pc = base.code_size_bytes;
    expect_fault("bad entry pc", bad, model_error::local_memory_bounds);

    bad = base;
    bad.program_mem_bytes = base.code_size_bytes - 4U;
    expect_fault("program memory too small", bad, model_error::local_memory_bounds);

    bad = base;
    bad.local_mem_bytes = base.arg_size_bytes - 4U;
    expect_fault("argument memory too small", bad, model_error::local_memory_bounds);

    bad = base;
    bad.stack_bytes = bad.local_mem_bytes - bad.arg_size_bytes + 1U;
    expect_fault("argument and stack overlap", bad, model_error::local_memory_bounds);

    bad = base;
    bad.code_addr = std::numeric_limits<std::uint64_t>::max() -
        (HOLON_NPU_PROGRAM_IMAGE_ALIGN - 1U);
    expect_fault("code address overflow", bad, model_error::invalid_program_descriptor);

    bad = base;
    bad.arg_addr = std::numeric_limits<std::uint64_t>::max() - 15U;
    expect_fault("argument address overflow", bad, model_error::invalid_program_descriptor);

    bad = base;
    bad.completion_addr = std::numeric_limits<std::uint64_t>::max() - 15U;
    expect_fault("completion address overflow", bad, model_error::invalid_program_descriptor);

    return ok;
}

bool test_dma_ordering_and_visibility() {
    machine model(128, 16);
    model.resize_system_memory(128);

    const std::array<std::int32_t, 4> source{5, 6, 7, 8};
    const std::array<std::int32_t, 4> expected_store{15, 16, 17, 18};

    bool ok = true;
    ok &= expect_true("system write", model.write_system_i32(0, source));
    ok &= expect_true("dma load", model.issue_dma_load(0, 0, source.size() * sizeof(std::int32_t)));
    const auto loaded = model.read_i32(0, source.size());
    ok &= expect_vector_eq("dma load visible", loaded, source);

    ok &= expect_true("local write", model.write_i32(32, expected_store));
    ok &= expect_true("dma store", model.issue_dma_store(32, 64, expected_store.size() * sizeof(std::int32_t)));
    const auto stored = model.read_system_i32(64, expected_store.size());
    ok &= expect_vector_eq("dma store visible", stored, expected_store);

    const auto& events = model.dma_events();
    ok &= expect_eq("dma event count", events.size(), std::size_t{2});
    ok &= expect_eq("dma event 0 sequence", events.at(0).sequence, std::uint64_t{0});
    ok &= expect_eq("dma event 0 direction", events.at(0).direction, dma_direction::system_to_local);
    ok &= expect_eq("dma event 1 sequence", events.at(1).sequence, std::uint64_t{1});
    ok &= expect_eq("dma event 1 direction", events.at(1).direction, dma_direction::local_to_system);
    model.clear_dma_events();
    ok &= expect_eq("dma event clear", model.dma_events().size(), std::size_t{0});
    return ok;
}

bool test_dma_faults() {
    machine model(32, 16);
    model.resize_system_memory(32);

    bool ok = true;
    const std::array<std::int32_t, 1> source{1};
    ok &= expect_true("dma system write", model.write_system_i32(0, source));
    ok &= expect_true("dma load local bounds false", !model.issue_dma_load(0, 24, 16));
    ok &= expect_eq("dma load local bounds state", model.state(), lifecycle_state::fault);
    ok &= expect_eq("dma load local bounds fault", model.fault(), model_error::local_memory_bounds);

    model.reset();
    model.resize_system_memory(32);
    ok &= expect_true("dma load system bounds false", !model.issue_dma_load(28, 0, 16));
    ok &= expect_eq("dma load system bounds state", model.state(), lifecycle_state::fault);
    ok &= expect_eq("dma load system bounds fault", model.fault(), model_error::dma_request);
    return ok;
}

bool test_dma_load_instruction_program() {
    machine model(128, 16);
    model.resize_system_memory(0x1100);
    const std::array<std::int32_t, 3> source{31, -7, 2048};
    constexpr std::uint32_t source_addr = 0x1020;
    const std::array<std::int32_t, 3> dma_args{
        static_cast<std::int32_t>(source_addr),
        0,
        16,
    };
    program_builder program;
    program.scalar_load(1, 0, 0)
        .scalar_load(2, 0, 4)
        .scalar_load(3, 0, 8)
        .dma_load(1, 2, 3, static_cast<std::uint16_t>(source.size()))
        .exit();

    bool ok = true;
    ok &= expect_true("dma instruction args write", model.write_i32(0, dma_args));
    ok &= expect_true("dma instruction system write", model.write_system_i32(source_addr, source));
    model.load_program(program.span());
    const auto result = model.run(8);
    ok &= expect_eq("dma instruction state", result.state, lifecycle_state::done);
    ok &= expect_eq("dma instruction fault", result.fault, model_error::none);
    ok &= expect_eq("dma instruction retired", result.retired, std::uint64_t{5});
    ok &= expect_eq("dma instruction pc", result.pc, std::uint32_t{20});
    const auto loaded = model.read_i32(16, source.size());
    ok &= expect_vector_eq("dma instruction result", loaded, source);
    return ok;
}

bool test_dma_store_instruction_program() {
    machine model(128, 16);
    model.resize_system_memory(0x1100);
    const std::array<std::int32_t, 3> source{17, -21, 4096};
    constexpr std::uint32_t store_addr = 0x1030;
    const std::array<std::int32_t, 3> dma_args{
        static_cast<std::int32_t>(store_addr),
        0,
        24,
    };
    program_builder program;
    program.scalar_load(1, 0, 0)
        .scalar_load(2, 0, 4)
        .scalar_load(3, 0, 8)
        .dma_store(1, 2, 3, static_cast<std::uint16_t>(source.size()))
        .exit();

    bool ok = true;
    ok &= expect_true("dma store args write", model.write_i32(0, dma_args));
    ok &= expect_true("dma store local write", model.write_i32(24, source));
    model.load_program(program.span());
    const auto result = model.run(8);
    ok &= expect_eq("dma store instruction state", result.state, lifecycle_state::done);
    ok &= expect_eq("dma store instruction fault", result.fault, model_error::none);
    ok &= expect_eq("dma store instruction retired", result.retired, std::uint64_t{5});
    ok &= expect_eq("dma store instruction pc", result.pc, std::uint32_t{20});
    const auto stored = model.read_system_i32(store_addr, source.size());
    ok &= expect_vector_eq("dma store instruction result", stored, source);
    return ok;
}

bool test_sync_order_instruction_program() {
    machine model(128, 16);
    model.resize_system_memory(0x1100);
    const std::array<std::int32_t, 2> source{0x1111, -0x2222};
    constexpr auto source_addr = std::uint32_t{0x1020};
    constexpr auto store_addr = std::uint32_t{0x1060};
    const std::array<std::int32_t, 4> dma_args{
        static_cast<std::int32_t>(source_addr),
        0,
        static_cast<std::int32_t>(store_addr),
        0,
    };

    program_builder program;
    program.scalar_load(1, 0, 0)
        .scalar_load(2, 0, 4)
        .movi(3, 32)
        .dma_load(1, 2, 3, static_cast<std::uint16_t>(source.size()))
        .wait_dma()
        .fence_local()
        .scalar_load(1, 0, 8)
        .scalar_load(2, 0, 12)
        .dma_store(1, 2, 3, static_cast<std::uint16_t>(source.size()))
        .fence_dma()
        .exit();

    bool ok = true;
    ok &= expect_true("sync program args write", model.write_i32(0, dma_args));
    ok &= expect_true("sync program system write", model.write_system_i32(source_addr, source));
    model.load_program(program.span());
    const auto result = model.run(16);
    ok &= expect_eq("sync program state", result.state, lifecycle_state::done);
    ok &= expect_eq("sync program fault", result.fault, model_error::none);
    ok &= expect_eq("sync program retired", result.retired, std::uint64_t{11});
    ok &= expect_eq("sync program pc", result.pc, std::uint32_t{44});
    const auto stored = model.read_system_i32(store_addr, source.size());
    ok &= expect_vector_eq("sync program store result", stored, source);
    ok &= expect_eq("sync program dma events", model.dma_events().size(), std::size_t{2});
    ok &= expect_eq("sync program dma event 0 sequence", model.dma_events().at(0).sequence, std::uint64_t{0});
    ok &= expect_eq("sync program dma event 1 sequence", model.dma_events().at(1).sequence, std::uint64_t{1});
    return ok;
}

bool test_vector_i32_alu_ops() {
    const std::array<std::int32_t, 12> args{
        1,
        -5,
        std::numeric_limits<std::int32_t>::min(),
        7,
        2,
        4,
        -1,
        -10,
        100,
        200,
        300,
        400,
    };
    const auto arg_bytes = std::as_bytes(std::span(args));

    auto run_op = [&](std::uint32_t op, std::span<const std::int32_t> expected) {
        machine model(128, 16);
        program_builder program;
        program.configure(4, vector_element_width::bits_32, true)
            .load(1, 0)
            .load(2, 16)
            .raw(op)
            .store(3, 48)
            .exit();
        const auto desc = valid_program_desc(program.span(), arg_bytes);
        const auto load_result = model.load_program_descriptor(desc, program.span(), arg_bytes);
        bool ok = true;
        ok &= expect_eq("alu load state", load_result.state, lifecycle_state::idle);
        ok &= expect_eq("alu load fault", load_result.fault, model_error::none);
        const auto result = model.run(16);
        ok &= expect_eq("alu run state", result.state, lifecycle_state::done);
        const auto actual = model.read_i32(48, expected.size());
        ok &= expect_vector_eq("alu result", actual, expected);
        return ok;
    };

    const std::array<std::int32_t, 4> add_expected{
        3,
        -1,
        std::numeric_limits<std::int32_t>::max(),
        -3,
    };
    const std::array<std::int32_t, 4> sub_expected{
        -1,
        -9,
        std::numeric_limits<std::int32_t>::min() + 1,
        17,
    };
    const std::array<std::int32_t, 4> min_expected{1, -5, std::numeric_limits<std::int32_t>::min(), -10};
    const std::array<std::int32_t, 4> max_expected{2, 4, -1, 7};

    bool ok = true;
    ok &= run_op(encode_vector_add(3, 1, 2), add_expected);
    ok &= run_op(encode_vector_sub(3, 1, 2), sub_expected);
    ok &= run_op(encode_vector_min(3, 1, 2), min_expected);
    ok &= run_op(encode_vector_max(3, 1, 2), max_expected);
    return ok;
}

bool test_vector_i32_compare_shift_ops() {
    const std::array<std::int32_t, 8> args{
        5,
        -5,
        1'073'741'824,
        -16,
        5,
        3,
        2,
        35,
    };
    const auto arg_bytes = std::as_bytes(std::span(args));

    auto run_op = [&](std::uint32_t op, std::span<const std::int32_t> expected) {
        machine model(128, 16);
        program_builder program;
        program.configure(4, vector_element_width::bits_32, true)
            .load(1, 0)
            .load(2, 16)
            .raw(op)
            .store(3, 32)
            .exit();
        const auto desc = valid_program_desc(program.span(), arg_bytes);
        bool ok = true;
        ok &= expect_eq("compare shift load fault", model.load_program_descriptor(desc, program.span(), arg_bytes).fault,
                        model_error::none);
        ok &= expect_eq("compare shift run", model.run(16).state, lifecycle_state::done);
        const auto actual = model.read_i32(32, expected.size());
        ok &= expect_vector_eq("compare shift result", actual, expected);
        return ok;
    };

    const std::array<std::int32_t, 4> eq_expected{1, 0, 0, 0};
    const std::array<std::int32_t, 4> lt_expected{0, 1, 0, 1};
    const std::array<std::int32_t, 4> shl_expected{160, -40, 0, -128};
    const std::array<std::int32_t, 4> srl_expected{0, 536'870'911, 268'435'456, 536'870'910};
    const std::array<std::int32_t, 4> sra_expected{0, -1, 268'435'456, -2};

    bool ok = true;
    ok &= run_op(encode_vector_eq(3, 1, 2), eq_expected);
    ok &= run_op(encode_vector_lt(3, 1, 2), lt_expected);
    ok &= run_op(encode_vector_shl(3, 1, 2), shl_expected);
    ok &= run_op(encode_vector_srl(3, 1, 2), srl_expected);
    ok &= run_op(encode_vector_sra(3, 1, 2), sra_expected);
    return ok;
}

bool test_vector_narrow_element_semantics() {
    bool ok = true;

    {
        machine model(128, 16);
        const std::array<std::int8_t, 4> lhs{127, -128, -1, 16};
        const std::array<std::int8_t, 4> rhs{1, -1, 2, -16};
        const std::array<std::int8_t, 4> add_expected{-128, 127, 1, 0};
        program_builder program;
        program.configure(4, vector_element_width::bits_8, true)
            .load(1, 0)
            .load(2, 4)
            .add(3, 1, 2)
            .store(3, 8)
            .exit();
        ok &= expect_true("narrow i8 lhs", model.write_i8(0, lhs));
        ok &= expect_true("narrow i8 rhs", model.write_i8(4, rhs));
        model.load_program(program.span());
        ok &= expect_eq("narrow i8 state", model.run(16).state, lifecycle_state::done);
        ok &= expect_vector_eq("narrow i8 add", model.read_i8(8, 4), add_expected);
    }

    {
        machine model(128, 16);
        const std::array<std::int8_t, 4> lhs{127, -128, -1, 16};
        const std::array<std::int8_t, 4> rhs{1, -1, 2, -16};
        const std::array<std::int8_t, 4> srl_expected{63, 1, 63, 16};
        program_builder program;
        program.configure(4, vector_element_width::bits_8, true)
            .load(1, 0)
            .load(2, 4)
            .srl(3, 1, 2)
            .store(3, 8)
            .exit();
        ok &= expect_true("narrow srl lhs", model.write_i8(0, lhs));
        ok &= expect_true("narrow srl rhs", model.write_i8(4, rhs));
        model.load_program(program.span());
        ok &= expect_eq("narrow srl state", model.run(16).state, lifecycle_state::done);
        ok &= expect_vector_eq("narrow logical shift", model.read_i8(8, 4), srl_expected);
    }

    {
        machine model(128, 16);
        const std::array<std::int8_t, 4> lhs{127, -128, -1, 16};
        const std::array<std::int8_t, 4> rhs{1, -1, 2, -16};
        const std::array<std::int8_t, 4> lt_expected{0, 1, 0, 1};
        program_builder program;
        program.configure(4, vector_element_width::bits_8, false)
            .load(1, 0)
            .load(2, 4)
            .lt(3, 1, 2)
            .store(3, 8)
            .exit();
        ok &= expect_true("narrow u8 lhs", model.write_i8(0, lhs));
        ok &= expect_true("narrow u8 rhs", model.write_i8(4, rhs));
        model.load_program(program.span());
        ok &= expect_eq("narrow u8 state", model.run(16).state, lifecycle_state::done);
        ok &= expect_vector_eq("narrow unsigned compare", model.read_i8(8, 4), lt_expected);
    }

    {
        machine model(128, 16);
        const std::array<std::int16_t, 2> lhs{32767, -32768};
        const std::array<std::int16_t, 2> rhs{1, -1};
        const std::array<std::int16_t, 2> expected{-32768, 32767};
        program_builder program;
        program.configure(2, vector_element_width::bits_16, true)
            .load(1, 0)
            .load(2, 4)
            .add(3, 1, 2)
            .store(3, 8)
            .exit();
        ok &= expect_true("narrow i16 lhs", model.write_i16(0, lhs));
        ok &= expect_true("narrow i16 rhs", model.write_i16(4, rhs));
        model.load_program(program.span());
        ok &= expect_eq("narrow i16 state", model.run(16).state, lifecycle_state::done);
        ok &= expect_vector_eq("narrow i16 add", model.read_i16(8, 2), expected);
    }

    {
        machine model(128, 16);
        const std::array<std::int16_t, 2> lhs{-32768, -1};
        const std::array<std::int16_t, 2> rhs{1, 1};
        const std::array<std::int16_t, 2> expected{0, 0};
        program_builder program;
        program.configure(2, vector_element_width::bits_16, false)
            .load(1, 0)
            .load(2, 4)
            .lt(3, 1, 2)
            .store(3, 8)
            .exit();
        ok &= expect_true("narrow u16 lhs", model.write_i16(0, lhs));
        ok &= expect_true("narrow u16 rhs", model.write_i16(4, rhs));
        model.load_program(program.span());
        ok &= expect_eq("narrow u16 state", model.run(16).state, lifecycle_state::done);
        ok &= expect_vector_eq("narrow unsigned i16 compare", model.read_i16(8, 2), expected);
    }

    {
        machine model(128, 16);
        const std::array<std::int32_t, 1> lhs{-1};
        const std::array<std::int32_t, 1> rhs{1};
        const std::array<std::int32_t, 1> expected{0};
        program_builder program;
        program.configure(1, vector_element_width::bits_32, false)
            .load(1, 0)
            .load(2, 4)
            .lt(3, 1, 2)
            .store(3, 8)
            .exit();
        ok &= expect_true("unsigned i32 lhs", model.write_i32(0, lhs));
        ok &= expect_true("unsigned i32 rhs", model.write_i32(4, rhs));
        model.load_program(program.span());
        ok &= expect_eq("unsigned i32 state", model.run(16).state, lifecycle_state::done);
        ok &= expect_vector_eq("unsigned i32 compare", model.read_i32(8, 1), expected);
    }

    return ok;
}

bool test_random_vector_i32_programs() {
    constexpr auto seed = std::uint64_t{0x48504C4F4E5632ULL};
    constexpr auto lanes_max = std::size_t{16};
    constexpr auto lhs_offset = std::uint16_t{0};
    constexpr auto rhs_offset = std::uint16_t{64};
    constexpr auto dst_offset = std::uint16_t{128};
    constexpr auto predicate_offset = std::uint16_t{192};
    constexpr auto local_mem_bytes = std::uint32_t{256};
    constexpr std::array ops{
        random_vector_op::add,
        random_vector_op::sub,
        random_vector_op::min,
        random_vector_op::max,
        random_vector_op::eq,
        random_vector_op::lt,
        random_vector_op::shl,
        random_vector_op::srl,
        random_vector_op::sra,
    };

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> vl_dist(1, static_cast<int>(lanes_max));
    std::uniform_int_distribution<int> op_dist(0, static_cast<int>(ops.size() - 1));
    std::uniform_int_distribution<int> value_dist(-4096, 4096);
    std::uniform_int_distribution<int> predicate_dist(0, 1);

    bool ok = true;
    for (std::size_t case_index = 0; case_index < 64; ++case_index) {
        const auto vl = static_cast<std::uint16_t>(vl_dist(rng));
        const auto op = ops.at(static_cast<std::size_t>(op_dist(rng)));

        std::array<std::int32_t, lanes_max * 4> args{};
        std::uint32_t predicate_bits = 0;
        std::vector<std::int32_t> expected(vl, 0);

        for (std::size_t lane = 0; lane < lanes_max; ++lane) {
            const auto lhs = static_cast<std::int32_t>(value_dist(rng));
            const auto rhs = static_cast<std::int32_t>(value_dist(rng));
            const auto initial = static_cast<std::int32_t>(value_dist(rng));
            args.at(lane) = lhs;
            args.at(lanes_max + lane) = rhs;
            args.at((lanes_max * 2) + lane) = initial;
            if (lane < vl) {
                const auto active = predicate_dist(rng) != 0;
                if (active) {
                    predicate_bits |= std::uint32_t{1} << lane;
                }
                expected.at(lane) = active
                    ? reference_random_vector_op(op, lhs, rhs)
                    : initial;
            }
        }
        args.at(lanes_max * 3) = std::bit_cast<std::int32_t>(predicate_bits);

        program_builder program;
        program.configure(vl, vector_element_width::bits_32, true)
            .load(1, lhs_offset)
            .load(2, rhs_offset)
            .load(3, dst_offset)
            .predicate_load(0, predicate_offset);
        append_random_vector_op(program, op, 3, 1, 2).store(3, dst_offset).exit();
        const auto arg_bytes = std::as_bytes(std::span(args));
        auto desc = valid_program_desc(program.span(), arg_bytes, local_mem_bytes);
        desc.required_op_classes |= HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE;

        machine model(local_mem_bytes, lanes_max);
        const auto load = model.load_program_descriptor(desc, program.span(), arg_bytes);
        const auto case_name = std::string{"random vector case "} + std::to_string(case_index);
        ok &= expect_eq(case_name + " load fault", load.fault, model_error::none);
        ok &= expect_eq(case_name + " run", model.run(32).state, lifecycle_state::done);
        const auto actual = model.read_i32(dst_offset, expected.size());
        ok &= expect_vector_eq(case_name, actual, expected);
    }
    return ok;
}

bool test_predicate_inactive_lanes_preserve_destination() {
    machine model(128, 16);
    const std::array<std::int32_t, 16> args{
        1, 2, 3, 4,
        10, 20, 30, 40,
        100, 200, 300, 400,
        0x5, 0, 0, 0,
    };
    const std::array<std::int32_t, 4> expected{11, 200, 33, 400};
    program_builder program;
    program.configure(4, vector_element_width::bits_32, true)
        .load(1, 0)
        .load(2, 16)
        .load(3, 32)
        .predicate_load(0, 48)
        .add(3, 1, 2, 0)
        .store(3, 32, 0)
        .predicate_ptrue(0)
        .add(4, 1, 2, 0)
        .store(4, 80, 0)
        .exit();
    const auto arg_bytes = std::as_bytes(std::span(args));
    auto desc = valid_program_desc(program.span(), arg_bytes);
    desc.required_op_classes |= HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE;

    bool ok = true;
    ok &= expect_eq("predicate load", model.load_program_descriptor(desc, program.span(), arg_bytes).fault,
                    model_error::none);
    ok &= expect_eq("predicate run", model.run(16).state, lifecycle_state::done);
    const auto actual = model.read_i32(32, expected.size());
    ok &= expect_vector_eq("predicate inactive lanes", actual, expected);
    const std::array<std::int32_t, 4> ptrue_expected{11, 22, 33, 44};
    ok &= expect_vector_eq("predicate ptrue lanes", model.read_i32(80, 4), ptrue_expected);
    return ok;
}

bool test_vector_select_permute_and_reduction() {
    machine model(256, 16);
    const std::array<std::int32_t, 4> lhs{10, 20, 30, 40};
    const std::array<std::int32_t, 4> rhs{-1, -2, -3, -4};
    const std::array<std::int32_t, 1> predicate{0x5};
    const std::array<std::int32_t, 4> indices{3, 0, 2, 1};
    const std::array<std::int32_t, 4> selected{10, -2, 30, -4};
    const std::array<std::int32_t, 4> gathered{40, 10, 30, 20};
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
        .store(3, 64, 0)
        .store(5, 80, 0)
        .configure(1, vector_element_width::bits_32, true)
        .store(6, 96, 0)
        .store(7, 100, 0)
        .store(8, 104, 0)
        .exit();

    bool ok = true;
    ok &= expect_true("select lhs write", model.write_i32(0, lhs));
    ok &= expect_true("select rhs write", model.write_i32(16, rhs));
    ok &= expect_true("select predicate write", model.write_i32(32, predicate));
    ok &= expect_true("gather indices write", model.write_i32(48, indices));
    model.load_program(program.span());
    ok &= expect_eq("helper program state", model.run(32).state, lifecycle_state::done);
    ok &= expect_vector_eq("select result", model.read_i32(64, 4), selected);
    ok &= expect_vector_eq("gather result", model.read_i32(80, 4), gathered);
    ok &= expect_eq("reduction sum", model.read_i32(96, 1).at(0), std::int32_t{100});
    ok &= expect_eq("reduction min", model.read_i32(100, 1).at(0), std::int32_t{10});
    ok &= expect_eq("reduction max", model.read_i32(104, 1).at(0), std::int32_t{40});
    return ok;
}

bool test_vector_saturating_arithmetic() {
    bool ok = true;
    {
        machine model(128, 16);
        const std::array<std::int8_t, 4> lhs{120, 127, -120, -128};
        const std::array<std::int8_t, 4> rhs{20, 1, -20, 1};
        const std::array<std::int8_t, 4> add_expected{127, 127, -128, -127};
        const std::array<std::int8_t, 4> sub_expected{100, 126, -100, -128};
        program_builder program;
        program.configure(4, vector_element_width::bits_8, true,
                          vector_rounding::nearest_even, true)
            .predicate_ptrue(0)
            .load(1, 0)
            .load(2, 4)
            .add(3, 1, 2)
            .sub(4, 1, 2)
            .store(3, 8)
            .store(4, 12)
            .exit();
        ok &= expect_true("signed saturation lhs", model.write_i8(0, lhs));
        ok &= expect_true("signed saturation rhs", model.write_i8(4, rhs));
        model.load_program(program.span());
        ok &= expect_eq("signed saturation state", model.run(16).state, lifecycle_state::done);
        ok &= expect_vector_eq("signed saturation add", model.read_i8(8, 4), add_expected);
        ok &= expect_vector_eq("signed saturation sub", model.read_i8(12, 4), sub_expected);
    }
    {
        machine model(128, 16);
        const std::array<std::int8_t, 4> lhs{-6, -1, 5, 0};
        const std::array<std::int8_t, 4> rhs{10, 1, 10, 1};
        const std::array<std::int8_t, 4> add_expected{-1, -1, 15, 1};
        const std::array<std::int8_t, 4> sub_expected{-16, -2, 0, 0};
        program_builder program;
        program.configure(4, vector_element_width::bits_8, false,
                          vector_rounding::nearest_even, true)
            .predicate_ptrue(0)
            .load(1, 0)
            .load(2, 4)
            .add(3, 1, 2)
            .sub(4, 1, 2)
            .store(3, 8)
            .store(4, 12)
            .exit();
        ok &= expect_true("unsigned saturation lhs", model.write_i8(0, lhs));
        ok &= expect_true("unsigned saturation rhs", model.write_i8(4, rhs));
        model.load_program(program.span());
        ok &= expect_eq("unsigned saturation state", model.run(16).state, lifecycle_state::done);
        ok &= expect_vector_eq("unsigned saturation add", model.read_i8(8, 4), add_expected);
        ok &= expect_vector_eq("unsigned saturation sub", model.read_i8(12, 4), sub_expected);
    }
    return ok;
}

bool test_vector_pack_unpack_and_transpose() {
    machine model(512, 16);
    std::array<std::int32_t, 16> first{};
    std::array<std::int32_t, 16> second{};
    std::array<std::int32_t, 16> zip_lo_expected{};
    std::array<std::int32_t, 16> zip_hi_expected{};
    std::array<std::int32_t, 16> transpose_expected{};
    for (std::size_t lane = 0; lane < first.size(); ++lane) {
        first[lane] = static_cast<std::int32_t>(lane);
        second[lane] = static_cast<std::int32_t>(100U + lane);
    }
    for (std::size_t lane = 0; lane < first.size(); ++lane) {
        const auto source_lane = lane / 2U;
        zip_lo_expected[lane] = lane % 2U == 0U ? first[source_lane] : second[source_lane];
        zip_hi_expected[lane] = lane % 2U == 0U
            ? first[8U + source_lane] : second[8U + source_lane];
        transpose_expected[lane] = first[(lane % 4U) * 4U + lane / 4U];
    }

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
        .exit();
    bool ok = true;
    ok &= expect_true("pack first write", model.write_i32(0, first));
    ok &= expect_true("pack second write", model.write_i32(64, second));
    model.load_program(program.span());
    ok &= expect_eq("pack program state", model.run(32).state, lifecycle_state::done);
    ok &= expect_vector_eq("zip lo result", model.read_i32(128, 16), zip_lo_expected);
    ok &= expect_vector_eq("zip hi result", model.read_i32(192, 16), zip_hi_expected);
    ok &= expect_vector_eq("unzip even result", model.read_i32(256, 16), first);
    ok &= expect_vector_eq("unzip odd result", model.read_i32(320, 16), second);
    ok &= expect_vector_eq("transpose4 result", model.read_i32(384, 16), transpose_expected);

    machine invalid(128, 16);
    program_builder invalid_program;
    invalid_program.configure(3, vector_element_width::bits_32, true)
        .predicate_ptrue(0)
        .zip_lo(3, 1, 2)
        .exit();
    invalid.load_program(invalid_program.span());
    const auto invalid_result = invalid.run(8);
    ok &= expect_eq("odd zip state", invalid_result.state, lifecycle_state::fault);
    ok &= expect_eq("odd zip fault", invalid_result.fault, model_error::vector_config);
    return ok;
}

bool test_vector_reduction_empty_identities() {
    machine model(128, 16);
    const std::array<std::int32_t, 4> source{10, -2, 30, -4};
    const std::array<std::int32_t, 1> empty_predicate{0};
    program_builder program;
    program.configure(4, vector_element_width::bits_32, true)
        .predicate_ptrue(0)
        .load(1, 0)
        .predicate_load(0, 16)
        .reduce_sum(2, 1, 0)
        .reduce_min(3, 1, 0)
        .reduce_max(4, 1, 0)
        .predicate_ptrue(0)
        .configure(1, vector_element_width::bits_32, true)
        .store(2, 32, 0)
        .store(3, 36, 0)
        .store(4, 40, 0)
        .exit();

    bool ok = true;
    ok &= expect_true("identity source write", model.write_i32(0, source));
    ok &= expect_true("empty predicate write", model.write_i32(16, empty_predicate));
    model.load_program(program.span());
    ok &= expect_eq("identity program state", model.run(24).state, lifecycle_state::done);
    ok &= expect_eq("sum identity", model.read_i32(32, 1).at(0), std::int32_t{0});
    ok &= expect_eq("min identity", model.read_i32(36, 1).at(0), std::numeric_limits<std::int32_t>::max());
    ok &= expect_eq("max identity", model.read_i32(40, 1).at(0), std::numeric_limits<std::int32_t>::min());
    return ok;
}

bool test_vector_requantization() {
    constexpr auto command_offset = std::uint16_t{64};
    machine model(160, 16);
    const std::array<std::int32_t, 4> source{3, 5, -3, 100};
    const std::array<std::int32_t, 6> command{1, 1, 0, -2, 3, 0};
    const std::array<std::int32_t, 4> expected{2, 2, -2, 3};
    program_builder program;
    program.configure(4, vector_element_width::bits_32, true)
        .predicate_ptrue(0)
        .load(1, 0)
        .requantize(2, 1, 0, command_offset)
        .store(2, 32, 0)
        .exit();

    bool ok = true;
    ok &= expect_true("requant source write", model.write_i32(0, source));
    ok &= expect_true("requant command write", model.write_i32(command_offset, command));
    model.load_program(program.span());
    ok &= expect_eq("requant state", model.run(16).state, lifecycle_state::done);
    ok &= expect_vector_eq("requant result", model.read_i32(32, 4), expected);

    machine malformed(160, 16);
    auto bad_command = command;
    bad_command[1] = 32;
    ok &= expect_true("malformed requant source write", malformed.write_i32(0, source));
    ok &= expect_true("malformed requant command write", malformed.write_i32(command_offset, bad_command));
    malformed.load_program(program.span());
    const auto bad_result = malformed.run(16);
    ok &= expect_eq("malformed requant state", bad_result.state, lifecycle_state::fault);
    ok &= expect_eq("malformed requant fault", bad_result.fault, model_error::vector_config);
    ok &= expect_eq("malformed requant pc", bad_result.pc, std::uint32_t{12});
    return ok;
}

bool test_vector_gather_fault() {
    machine model(128, 16);
    const std::array<std::int32_t, 4> source{10, 20, 30, 40};
    const std::array<std::int32_t, 4> invalid_indices{3, 4, 2, 1};
    program_builder program;
    program.configure(4, vector_element_width::bits_32, true)
        .predicate_ptrue(0)
        .load(1, 0)
        .load(2, 16)
        .gather(3, 1, 2, 0)
        .exit();

    bool ok = true;
    ok &= expect_true("gather fault source write", model.write_i32(0, source));
    ok &= expect_true("gather fault indices write", model.write_i32(16, invalid_indices));
    model.load_program(program.span());
    const auto result = model.run(16);
    ok &= expect_eq("gather fault state", result.state, lifecycle_state::fault);
    ok &= expect_eq("gather fault code", result.fault, model_error::vector_config);
    ok &= expect_eq("gather fault pc", result.pc, std::uint32_t{16});
    return ok;
}

bool test_matrix_gemm_i8_i32_micro_op() {
    machine model(256, 16);
    const std::array<std::int8_t, 6> a{
        1, -2, 3,
        4, 5, -6,
    };
    const std::array<std::int8_t, 12> b{
        7, -8, 9, 10,
        -1, 2, -3, 4,
        5, 6, -7, -8,
    };
    const std::array<std::int32_t, 8> initial_c{
        100, 200, 300, 400,
        500, 600, 700, 800,
    };
    const std::array<std::int32_t, 8> expected_accumulate{
        48, 12, -12, -44,
        -14, -116, 126, 216,
    };
    const auto op = matrix_gemm_i8_i32_op{
        .a_offset = 0,
        .b_offset = 32,
        .c_offset = 96,
        .a_row_stride_bytes = 3,
        .b_row_stride_bytes = 4,
        .c_row_stride_bytes = 16,
        .m = 2,
        .n = 4,
        .k = 3,
        .clear_accumulator = true,
        .accumulate = false,
        .store_result = false,
    };

    bool ok = true;
    ok &= expect_true("matrix write a", model.write_i8(op.a_offset, a));
    ok &= expect_true("matrix write b", model.write_i8(op.b_offset, b));
    ok &= expect_true("matrix write c", model.write_i32(op.c_offset, initial_c));
    ok &= expect_true("matrix issue clear", model.issue_matrix_gemm_i8_i32(op));
    ok &= expect_vector_eq(
        "matrix no-store preserves C",
        model.read_i32(op.c_offset, initial_c.size()),
        initial_c
    );

    auto accumulate_op = op;
    accumulate_op.clear_accumulator = false;
    accumulate_op.accumulate = true;
    accumulate_op.store_result = true;
    ok &= expect_true("matrix issue accumulate", model.issue_matrix_gemm_i8_i32(accumulate_op));
    const auto accumulate_actual = model.read_i32(op.c_offset, expected_accumulate.size());
    ok &= expect_vector_eq(
        "matrix accumulate result",
        accumulate_actual,
        expected_accumulate
    );

    const auto& events = model.matrix_events();
    ok &= expect_eq("matrix event count", events.size(), std::size_t{2});
    ok &= expect_eq("matrix event 0 sequence", events.at(0).sequence, std::uint64_t{0});
    ok &= expect_eq("matrix event 0 m", events.at(0).m, std::uint16_t{2});
    ok &= expect_eq("matrix event 0 n", events.at(0).n, std::uint16_t{4});
    ok &= expect_eq("matrix event 0 k", events.at(0).k, std::uint16_t{3});
    ok &= expect_eq("matrix event 0 stored", events.at(0).stored, false);
    ok &= expect_eq("matrix event 1 sequence", events.at(1).sequence, std::uint64_t{1});
    ok &= expect_eq("matrix event 1 stored", events.at(1).stored, true);
    model.clear_matrix_events();
    ok &= expect_eq("matrix event clear", model.matrix_events().size(), std::size_t{0});
    return ok;
}

bool test_matrix_gemm_program_instruction() {
    constexpr auto command_offset = std::uint16_t{160};
    machine model(256, 16);
    const std::array<std::int8_t, 4> a{1, 2, 3, 4};
    const std::array<std::int8_t, 4> b{5, 6, 7, 8};
    const std::array<std::int32_t, 4> expected{19, 22, 43, 50};
    const auto shape = std::uint32_t{2} |
        (std::uint32_t{2} << HOLON_NPU_ISA_MATRIX_SHAPE_N_SHIFT) |
        (std::uint32_t{2} << HOLON_NPU_ISA_MATRIX_SHAPE_K_SHIFT) |
        ((HOLON_NPU_ISA_MATRIX_FLAG_CLEAR | HOLON_NPU_ISA_MATRIX_FLAG_STORE)
         << HOLON_NPU_ISA_MATRIX_SHAPE_FLAGS_SHIFT);
    const std::array<std::int32_t, 8> command{
        0,
        32,
        64,
        2,
        2,
        8,
        std::bit_cast<std::int32_t>(shape),
        0,
    };
    program_builder program;
    program.matrix_gemm(0, command_offset).exit();

    bool ok = true;
    ok &= expect_true("matrix program write A", model.write_i8(0, a));
    ok &= expect_true("matrix program write B", model.write_i8(32, b));
    ok &= expect_true("matrix program write command", model.write_i32(command_offset, command));
    model.load_program(program.span());
    ok &= expect_eq("matrix program state", model.run(8).state, lifecycle_state::done);
    ok &= expect_vector_eq("matrix program result", model.read_i32(64, expected.size()), expected);
    ok &= expect_eq("matrix program retired", model.retired(), std::uint64_t{2});
    return ok;
}

bool test_matrix_issue_faults() {
    machine model(128, 16);
    const auto base = matrix_gemm_i8_i32_op{
        .a_offset = 0,
        .b_offset = 32,
        .c_offset = 64,
        .a_row_stride_bytes = 2,
        .b_row_stride_bytes = 2,
        .c_row_stride_bytes = 8,
        .m = 2,
        .n = 2,
        .k = 2,
        .clear_accumulator = true,
    };

    bool ok = true;
    auto expect_matrix_fault = [&](std::string_view name, matrix_gemm_i8_i32_op op) {
        ok &= expect_true(name, !model.issue_matrix_gemm_i8_i32(op));
        ok &= expect_eq("matrix fault state", model.state(), lifecycle_state::fault);
        ok &= expect_eq("matrix fault code", model.fault(), model_error::matrix_issue);
        model.reset();
    };

    auto bad = base;
    bad.m = 0;
    expect_matrix_fault("matrix zero m", bad);

    bad = base;
    bad.a_row_stride_bytes = 1;
    expect_matrix_fault("matrix short a stride", bad);

    bad = base;
    bad.c_offset = 66;
    expect_matrix_fault("matrix unaligned c", bad);

    bad = base;
    bad.c_offset = 124;
    expect_matrix_fault("matrix c out of range", bad);

    return ok;
}

bool test_vector_config_fault() {
    machine model(128, 16);
    program_builder program;
    program.configure(17, vector_element_width::bits_32, true).exit();
    model.load_program(program.span());
    const auto result = model.run(4);

    bool ok = true;
    ok &= expect_eq("config fault state", result.state, lifecycle_state::fault);
    ok &= expect_eq("config fault code", result.fault, model_error::vector_config);
    ok &= expect_eq("config fault pc", result.pc, std::uint32_t{0});
    return ok;
}

bool test_local_memory_bounds_fault() {
    machine model(32, 16);
    program_builder program;
    program.configure(4, vector_element_width::bits_32, true).load(1, 24).exit();
    model.load_program(program.span());
    const auto result = model.run(8);

    bool ok = true;
    ok &= expect_eq("bounds fault state", result.state, lifecycle_state::fault);
    ok &= expect_eq("bounds fault code", result.fault, model_error::local_memory_bounds);
    ok &= expect_eq("bounds fault pc", result.pc, std::uint32_t{4});
    return ok;
}

bool test_explicit_program_fault() {
    machine model(32, 16);
    program_builder program;
    program.fault(model_error::explicit_program_fault);
    model.load_program(program.span());
    const auto result = model.run(2);

    bool ok = true;
    ok &= expect_eq("explicit fault state", result.state, lifecycle_state::fault);
    ok &= expect_eq("explicit fault code", result.fault, model_error::explicit_program_fault);
    ok &= expect_eq("explicit fault pc", result.pc, std::uint32_t{0});
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_decode_and_disassemble();
    ok &= test_scalar_control_program();
    ok &= test_scalar_control_faults();
    ok &= test_csr_read_program();
    ok &= test_minimal_vector_program();
    ok &= test_descriptor_validation_faults();
    ok &= test_dma_ordering_and_visibility();
    ok &= test_dma_faults();
    ok &= test_dma_load_instruction_program();
    ok &= test_dma_store_instruction_program();
    ok &= test_sync_order_instruction_program();
    ok &= test_vector_i32_alu_ops();
    ok &= test_vector_i32_compare_shift_ops();
    ok &= test_vector_narrow_element_semantics();
    ok &= test_random_vector_i32_programs();
    ok &= test_predicate_inactive_lanes_preserve_destination();
    ok &= test_vector_select_permute_and_reduction();
    ok &= test_vector_saturating_arithmetic();
    ok &= test_vector_pack_unpack_and_transpose();
    ok &= test_vector_reduction_empty_identities();
    ok &= test_vector_requantization();
    ok &= test_vector_gather_fault();
    ok &= test_matrix_gemm_i8_i32_micro_op();
    ok &= test_matrix_gemm_program_instruction();
    ok &= test_matrix_issue_faults();
    ok &= test_vector_config_fault();
    ok &= test_local_memory_bounds_fault();
    ok &= test_explicit_program_fault();
    return ok ? 0 : 1;
}
