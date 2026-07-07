#include "holon_npu_v2_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using holon_npu::v2::model::class_name;
using holon_npu::v2::model::decode;
using holon_npu::v2::model::disassemble;
using holon_npu::v2::model::encode_system_exit;
using holon_npu::v2::model::encode_system_fault;
using holon_npu::v2::model::encode_vector_add_i32;
using holon_npu::v2::model::encode_vector_config_set_vl;
using holon_npu::v2::model::encode_vector_load_i32;
using holon_npu::v2::model::encode_vector_store_i32;
using holon_npu::v2::model::lifecycle_state;
using holon_npu::v2::model::machine;
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
    ok &= expect_eq("reserved decode", decode(HOLON_NPU_ISA_CLASS_RESERVED_D).isa_class,
                    HOLON_NPU_ISA_ENUM_RESERVED_D);
    return ok;
}

bool test_minimal_vector_program() {
    machine model(128, 16);
    const std::array<std::int32_t, 4> lhs{1, 2, 3, 4};
    const std::array<std::int32_t, 4> rhs{10, 20, 30, 40};
    const std::array<std::int32_t, 4> expected{11, 22, 33, 44};
    const std::array<std::uint32_t, 6> program{
        encode_vector_config_set_vl(4),
        encode_vector_load_i32(1, 0),
        encode_vector_load_i32(2, 16),
        encode_vector_add_i32(3, 1, 2),
        encode_vector_store_i32(3, 32),
        encode_system_exit(),
    };

    bool ok = true;
    ok &= expect_true("write lhs", model.write_i32(0, lhs));
    ok &= expect_true("write rhs", model.write_i32(16, rhs));
    model.load_program(program);
    const auto result = model.run(16);
    ok &= expect_eq("minimal state", result.state, lifecycle_state::done);
    ok &= expect_eq("minimal fault", result.fault, model_error::none);
    ok &= expect_eq("minimal retired", result.retired, std::uint64_t{6});
    ok &= expect_eq("minimal pc", result.pc, std::uint32_t{24});
    const auto actual = model.read_i32(32, expected.size());
    ok &= expect_vector_eq("minimal vector result", actual, expected);
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
    ok &= test_vector_config_fault();
    ok &= test_local_memory_bounds_fault();
    ok &= test_explicit_program_fault();
    return ok ? 0 : 1;
}
