#include "holon_npu_v2_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

using holon_npu::v2::model::class_name;
using holon_npu::v2::model::decode;
using holon_npu::v2::model::dma_direction;
using holon_npu::v2::model::disassemble;
using holon_npu::v2::model::encode_system_exit;
using holon_npu::v2::model::encode_system_fault;
using holon_npu::v2::model::encode_vector_add_i32;
using holon_npu::v2::model::encode_vector_config_set_vl;
using holon_npu::v2::model::encode_vector_eq_i32;
using holon_npu::v2::model::encode_vector_load_i32;
using holon_npu::v2::model::encode_vector_lt_i32;
using holon_npu::v2::model::encode_vector_max_i32;
using holon_npu::v2::model::encode_vector_min_i32;
using holon_npu::v2::model::encode_vector_shl_i32;
using holon_npu::v2::model::encode_vector_store_i32;
using holon_npu::v2::model::encode_vector_sra_i32;
using holon_npu::v2::model::encode_vector_srl_i32;
using holon_npu::v2::model::encode_vector_sub_i32;
using holon_npu::v2::model::lifecycle_state;
using holon_npu::v2::model::machine;
using holon_npu::v2::model::matrix_gemm_i8_i32_op;
using holon_npu::v2::model::model_error;

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

bool expect_vector_eq(
    std::string_view name,
    std::span<const std::int32_t> actual,
    std::span<const std::int32_t> expected
) {
    if (actual.size() != expected.size()) {
        std::cerr << name << ": size mismatch\n";
        return false;
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (actual[index] != expected[index]) {
            std::cerr << name << "[" << index << "]: expected " << expected[index]
                      << ", got " << actual[index] << '\n';
            return false;
        }
    }
    return true;
}

holon_npu_program_desc_t valid_program_desc(
    std::span<const std::uint32_t> program,
    std::span<const std::byte> args
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
                         HOLON_NPU_V2_CAP_INTEGER_QUANT_VECTOR,
        .required_op_classes = HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
                               HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM,
        .code_addr = 0x1000,
        .code_size_bytes = static_cast<std::uint32_t>(program.size_bytes()),
        .entry_pc = 0,
        .arg_addr = 0x2000,
        .arg_size_bytes = static_cast<std::uint32_t>(args.size()),
        .local_mem_bytes = 128,
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

bool test_decode_and_disassemble() {
    const auto word = encode_vector_add_i32(3, 1, 2);
    const auto inst = decode(word);

    bool ok = true;
    ok &= expect_eq("decode class", inst.isa_class, HOLON_NPU_ISA_ENUM_VECTOR_ALU);
    ok &= expect_eq("decode rd", inst.rd, std::uint8_t{3});
    ok &= expect_eq("decode rs1", inst.rs1, std::uint8_t{1});
    ok &= expect_eq("decode rs2", inst.rs2, std::uint8_t{2});
    ok &= expect_true("class name", class_name(inst.isa_class) == "vector_alu");
    ok &= expect_true("disassemble", disassemble(inst) == "vector_alu.add_i32 v3, v1, v2");
    ok &= expect_true(
        "disassemble sub",
        disassemble(decode(encode_vector_sub_i32(4, 5, 6))) == "vector_alu.sub_i32 v4, v5, v6"
    );
    ok &= expect_true(
        "disassemble eq",
        disassemble(decode(encode_vector_eq_i32(7, 8, 9))) == "vector_alu.eq_i32 v7, v8, v9"
    );
    ok &= expect_true(
        "disassemble sra",
        disassemble(decode(encode_vector_sra_i32(10, 11, 12))) == "vector_alu.sra_i32 v10, v11, v12"
    );
    ok &= expect_eq("reserved decode", decode(HOLON_NPU_ISA_CLASS_RESERVED_D).isa_class,
                    HOLON_NPU_ISA_ENUM_RESERVED_D);
    return ok;
}

bool test_minimal_vector_program() {
    machine model(128, 16);
    const std::array<std::int32_t, 8> args{1, 2, 3, 4, 10, 20, 30, 40};
    const std::array<std::int32_t, 4> expected{11, 22, 33, 44};
    const std::array<std::uint32_t, 6> program{
        encode_vector_config_set_vl(4),
        encode_vector_load_i32(1, 0),
        encode_vector_load_i32(2, 16),
        encode_vector_add_i32(3, 1, 2),
        encode_vector_store_i32(3, 32),
        encode_system_exit(),
    };
    const auto arg_bytes = std::as_bytes(std::span(args));
    const auto desc = valid_program_desc(program, arg_bytes);

    bool ok = true;
    const auto load_result = model.load_program_descriptor(desc, program, arg_bytes);
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

bool test_descriptor_validation_faults() {
    machine model(128, 16);
    const std::array<std::int32_t, 4> args{1, 2, 3, 4};
    const std::array<std::uint32_t, 2> program{
        encode_vector_config_set_vl(4),
        encode_system_exit(),
    };
    const auto arg_bytes = std::as_bytes(std::span(args));
    const auto base = valid_program_desc(program, arg_bytes);

    bool ok = true;
    auto expect_fault = [&](std::string_view name, holon_npu_program_desc_t desc, model_error expected) {
        const auto result = model.load_program_descriptor(desc, program, arg_bytes);
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
        const std::array<std::uint32_t, 6> program{
            encode_vector_config_set_vl(4),
            encode_vector_load_i32(1, 0),
            encode_vector_load_i32(2, 16),
            op,
            encode_vector_store_i32(3, 48),
            encode_system_exit(),
        };
        const auto desc = valid_program_desc(program, arg_bytes);
        const auto load_result = model.load_program_descriptor(desc, program, arg_bytes);
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
    ok &= run_op(encode_vector_add_i32(3, 1, 2), add_expected);
    ok &= run_op(encode_vector_sub_i32(3, 1, 2), sub_expected);
    ok &= run_op(encode_vector_min_i32(3, 1, 2), min_expected);
    ok &= run_op(encode_vector_max_i32(3, 1, 2), max_expected);
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
        const std::array<std::uint32_t, 6> program{
            encode_vector_config_set_vl(4),
            encode_vector_load_i32(1, 0),
            encode_vector_load_i32(2, 16),
            op,
            encode_vector_store_i32(3, 32),
            encode_system_exit(),
        };
        const auto desc = valid_program_desc(program, arg_bytes);
        bool ok = true;
        ok &= expect_eq("compare shift load fault", model.load_program_descriptor(desc, program, arg_bytes).fault,
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
    ok &= run_op(encode_vector_eq_i32(3, 1, 2), eq_expected);
    ok &= run_op(encode_vector_lt_i32(3, 1, 2), lt_expected);
    ok &= run_op(encode_vector_shl_i32(3, 1, 2), shl_expected);
    ok &= run_op(encode_vector_srl_i32(3, 1, 2), srl_expected);
    ok &= run_op(encode_vector_sra_i32(3, 1, 2), sra_expected);
    return ok;
}

bool test_predicate_inactive_lanes_preserve_destination() {
    machine model(128, 16);
    const std::array<std::int32_t, 12> args{
        1, 2, 3, 4,
        10, 20, 30, 40,
        100, 200, 300, 400,
    };
    const std::array<std::uint8_t, 4> predicate{1, 0, 1, 0};
    const std::array<std::int32_t, 4> expected{11, 200, 33, 400};
    const std::array<std::uint32_t, 7> program{
        encode_vector_config_set_vl(4),
        encode_vector_load_i32(1, 0),
        encode_vector_load_i32(2, 16),
        encode_vector_load_i32(3, 32),
        encode_vector_add_i32(3, 1, 2),
        encode_vector_store_i32(3, 48),
        encode_system_exit(),
    };
    const auto arg_bytes = std::as_bytes(std::span(args));
    const auto desc = valid_program_desc(program, arg_bytes);

    bool ok = true;
    ok &= expect_eq("predicate load", model.load_program_descriptor(desc, program, arg_bytes).fault,
                    model_error::none);
    ok &= expect_true("set predicate", model.set_predicate(predicate));
    ok &= expect_eq("predicate run", model.run(16).state, lifecycle_state::done);
    const auto actual = model.read_i32(48, expected.size());
    ok &= expect_vector_eq("predicate inactive lanes", actual, expected);
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
    const std::array<std::int32_t, 8> expected_clear{
        24, 6, -6, -22,
        -7, -58, 63, 108,
    };
    const std::array<std::int32_t, 8> expected_accumulate{
        124, 206, 294, 378,
        493, 542, 763, 908,
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
    };

    bool ok = true;
    ok &= expect_true("matrix write a", model.write_i8(op.a_offset, a));
    ok &= expect_true("matrix write b", model.write_i8(op.b_offset, b));
    ok &= expect_true("matrix write c", model.write_i32(op.c_offset, initial_c));
    ok &= expect_true("matrix issue clear", model.issue_matrix_gemm_i8_i32(op));
    const auto clear_actual = model.read_i32(op.c_offset, expected_clear.size());
    ok &= expect_vector_eq("matrix clear result", clear_actual, expected_clear);

    ok &= expect_true("matrix rewrite c", model.write_i32(op.c_offset, initial_c));
    auto accumulate_op = op;
    accumulate_op.clear_accumulator = false;
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
    ok &= expect_eq("matrix event 1 sequence", events.at(1).sequence, std::uint64_t{1});
    model.clear_matrix_events();
    ok &= expect_eq("matrix event clear", model.matrix_events().size(), std::size_t{0});
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
    const std::array<std::uint32_t, 2> program{
        encode_vector_config_set_vl(17),
        encode_system_exit(),
    };
    model.load_program(program);
    const auto result = model.run(4);

    bool ok = true;
    ok &= expect_eq("config fault state", result.state, lifecycle_state::fault);
    ok &= expect_eq("config fault code", result.fault, model_error::vector_config);
    ok &= expect_eq("config fault pc", result.pc, std::uint32_t{0});
    return ok;
}

bool test_local_memory_bounds_fault() {
    machine model(32, 16);
    const std::array<std::uint32_t, 3> program{
        encode_vector_config_set_vl(4),
        encode_vector_load_i32(1, 24),
        encode_system_exit(),
    };
    model.load_program(program);
    const auto result = model.run(8);

    bool ok = true;
    ok &= expect_eq("bounds fault state", result.state, lifecycle_state::fault);
    ok &= expect_eq("bounds fault code", result.fault, model_error::local_memory_bounds);
    ok &= expect_eq("bounds fault pc", result.pc, std::uint32_t{4});
    return ok;
}

bool test_explicit_program_fault() {
    machine model(32, 16);
    const std::array<std::uint32_t, 1> program{
        encode_system_fault(model_error::explicit_program_fault),
    };
    model.load_program(program);
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
    ok &= test_minimal_vector_program();
    ok &= test_descriptor_validation_faults();
    ok &= test_dma_ordering_and_visibility();
    ok &= test_dma_faults();
    ok &= test_vector_i32_alu_ops();
    ok &= test_vector_i32_compare_shift_ops();
    ok &= test_predicate_inactive_lanes_preserve_destination();
    ok &= test_matrix_gemm_i8_i32_micro_op();
    ok &= test_matrix_issue_faults();
    ok &= test_vector_config_fault();
    ok &= test_local_memory_bounds_fault();
    ok &= test_explicit_program_fault();
    return ok ? 0 : 1;
}
