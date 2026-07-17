#include "holon_npu_runtime.hpp"
#include "holon_npu_model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

namespace runtime = holon_npu::runtime;
using holon_npu::model::lifecycle_state;
using holon_npu::model::machine;

bool expect(bool condition, std::string_view name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
    }
    return condition;
}

template <typename T, std::size_t N>
bool expect_values(
    std::span<const T> actual,
    const std::array<T, N>& expected,
    std::string_view name
) {
    return expect(actual.size() == expected.size() &&
                      std::equal(actual.begin(), actual.end(), expected.begin()),
                  name);
}

bool run_vector_add() {
    machine model(128, 16);
    const std::array<std::int32_t, 4> lhs{1, 2, 3, 4};
    const std::array<std::int32_t, 4> rhs{5, 6, 7, 8};
    const std::array<std::int32_t, 4> expected{6, 8, 10, 12};
    const auto image = runtime::examples::vector_add(4, 0, 16, 32);

    bool ok = true;
    ok &= expect(model.write_i32(0, lhs), "vector add lhs write");
    ok &= expect(model.write_i32(16, rhs), "vector add rhs write");
    model.load_program(image.span());
    ok &= expect(model.run(16).state == lifecycle_state::done, "vector add completion");
    ok &= expect_values<std::int32_t>(model.read_i32(32, 4), expected, "vector add result");
    ok &= expect((image.required_caps & HOLON_NPU_CAP_INTEGER_VECTOR_BASE) != 0,
                 "vector add capability metadata");
    return ok;
}

bool run_relu() {
    machine model(128, 16);
    const std::array<std::int32_t, 4> source{-7, 0, 4, -1};
    const std::array<std::int32_t, 4> zeros{};
    const std::array<std::int32_t, 4> expected{0, 0, 4, 0};
    const auto image = runtime::examples::relu(4, 0, 16, 32);

    bool ok = true;
    ok &= expect(model.write_i32(0, source), "relu source write");
    ok &= expect(model.write_i32(16, zeros), "relu zero write");
    model.load_program(image.span());
    ok &= expect(model.run(16).state == lifecycle_state::done, "relu completion");
    ok &= expect_values<std::int32_t>(model.read_i32(32, 4), expected, "relu result");
    return ok;
}

bool run_reduce_sum() {
    machine model(128, 16);
    const std::array<std::int32_t, 4> source{10, -3, 7, 2};
    const auto image = runtime::examples::reduce_sum(4, 0, 32);

    bool ok = true;
    ok &= expect(model.write_i32(0, source), "reduce source write");
    model.load_program(image.span());
    ok &= expect(model.run(16).state == lifecycle_state::done, "reduce completion");
    ok &= expect(model.read_i32(32, 1).at(0) == 16, "reduce sum result");
    return ok;
}

bool run_requant() {
    machine model(160, 16);
    const std::array<std::int32_t, 4> source{3, 5, -3, 100};
    const std::array<std::int32_t, 6> command{1, 1, 0, -2, 3, 0};
    const std::array<std::int32_t, 4> expected{2, 2, -2, 3};
    const auto image = runtime::examples::requant(4, 0, 32, 64);

    bool ok = true;
    ok &= expect(model.write_i32(0, source), "requant source write");
    ok &= expect(model.write_i32(64, command), "requant command write");
    model.load_program(image.span());
    ok &= expect(model.run(16).state == lifecycle_state::done, "requant completion");
    ok &= expect_values<std::int32_t>(model.read_i32(32, 4), expected, "requant result");
    ok &= expect((image.required_caps & HOLON_NPU_CAP_QUANT_VECTOR) != 0,
                 "requant capability metadata");
    return ok;
}

bool run_transpose4() {
    machine model(256, 16);
    const std::array<std::int32_t, 16> source{
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
        12, 13, 14, 15,
    };
    const std::array<std::int32_t, 16> expected{
        0, 4, 8, 12,
        1, 5, 9, 13,
        2, 6, 10, 14,
        3, 7, 11, 15,
    };
    const auto image = runtime::examples::transpose4(0, 128);

    bool ok = true;
    ok &= expect(model.write_i32(0, source), "transpose source write");
    model.load_program(image.span());
    ok &= expect(model.run(16).state == lifecycle_state::done, "transpose completion");
    ok &= expect_values<std::int32_t>(model.read_i32(128, 16), expected, "transpose result");
    return ok;
}

bool run_int8_gemm() {
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
        0, 32, 64, 2, 2, 8, std::bit_cast<std::int32_t>(shape), 0,
    };
    const auto image = runtime::examples::int8_gemm(0, 160);

    bool ok = true;
    ok &= expect(model.write_i8(0, a), "gemm A write");
    ok &= expect(model.write_i8(32, b), "gemm B write");
    ok &= expect(model.write_i32(160, command), "gemm command write");
    model.load_program(image.span());
    ok &= expect(model.run(8).state == lifecycle_state::done, "gemm completion");
    ok &= expect_values<std::int32_t>(model.read_i32(64, 4), expected, "gemm result");
    ok &= expect((image.required_caps & HOLON_NPU_CAP_MATRIX_MICRO_OP) != 0,
                 "gemm capability metadata");
    return ok;
}

bool run_tiled_int8_gemm_shape(std::uint32_t m, std::uint32_t n, std::uint32_t k) {
    constexpr std::uint32_t command_offset = 0;
    constexpr std::uint32_t a_offset = 4096;
    constexpr std::uint32_t b_offset = 8192;
    constexpr std::uint32_t c_offset = 12288;
    constexpr std::uint32_t local_mem_bytes = 32768;
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
        .command_offset = command_offset,
    };
    const auto planned = runtime::examples::tiled_int8_gemm(config);
    if (!expect(planned.has_value(), "tiled GEMM program construction")) {
        return false;
    }

    std::vector<std::byte> local(local_mem_bytes);
    bool ok = expect(planned->write_commands(local), "tiled GEMM command materialization");
    std::vector<std::int8_t> a(static_cast<std::size_t>(m) * k);
    std::vector<std::int8_t> b(static_cast<std::size_t>(k) * n);
    for (std::size_t index = 0; index < a.size(); ++index) {
        a[index] = static_cast<std::int8_t>(static_cast<int>(index % 13U) - 6);
        local[a_offset + index] = std::byte{static_cast<std::uint8_t>(a[index])};
    }
    for (std::size_t index = 0; index < b.size(); ++index) {
        b[index] = static_cast<std::int8_t>(static_cast<int>((index * 3U) % 11U) - 5);
        local[b_offset + index] = std::byte{static_cast<std::uint8_t>(b[index])};
    }

    std::vector<std::int32_t> expected(static_cast<std::size_t>(m) * n);
    for (std::uint32_t row = 0; row < m; ++row) {
        for (std::uint32_t col = 0; col < n; ++col) {
            std::uint32_t accumulator = 0;
            for (std::uint32_t inner = 0; inner < k; ++inner) {
                const auto product = static_cast<std::int32_t>(a[row * k + inner]) *
                                     static_cast<std::int32_t>(b[inner * n + col]);
                accumulator += static_cast<std::uint32_t>(product);
            }
            expected[row * n + col] = std::bit_cast<std::int32_t>(accumulator);
        }
    }

    machine model(local_mem_bytes, 16);
    model.load_program(planned->image.span());
    ok &= expect(model.load_arguments(local, 0), "tiled GEMM local image load");
    ok &= expect(
        model.run(planned->image.words.size() + 1U).state == lifecycle_state::done,
        "tiled GEMM completion"
    );
    const auto actual = model.read_i32(c_offset, expected.size());
    ok &= expect(
        actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin()),
        "tiled GEMM result"
    );

    const auto tile_count = [](std::uint32_t dimension) {
        return (dimension + HOLON_NPU_ISA_MATRIX_MAX_DIMENSION - 1U) /
               HOLON_NPU_ISA_MATRIX_MAX_DIMENSION;
    };
    ok &= expect(
        planned->commands.size() ==
            static_cast<std::size_t>(tile_count(m) * tile_count(n) * tile_count(k)),
        "tiled GEMM command count"
    );
    return ok;
}

bool test_tiled_int8_gemm_validation() {
    auto config = runtime::matrix_gemm_config{
        .m = 1,
        .n = 1,
        .k = 1,
        .a_offset = 4096,
        .b_offset = 8192,
        .c_offset = 12288,
        .a_row_stride_bytes = 1,
        .b_row_stride_bytes = 1,
        .c_row_stride_bytes = 4,
        .local_mem_bytes = 16384,
        .command_offset = 0,
    };
    bool ok = true;
    auto invalid = config;
    invalid.m = 0;
    auto result = runtime::examples::tiled_int8_gemm(invalid);
    ok &= expect(
        !result && result.error() == runtime::matrix_program_error::invalid_dimension,
        "tiled GEMM zero dimension"
    );
    invalid = config;
    invalid.command_offset = 4096;
    result = runtime::examples::tiled_int8_gemm(invalid);
    ok &= expect(
        !result && result.error() == runtime::matrix_program_error::command_encoding_space,
        "tiled GEMM command encoding space"
    );
    invalid = config;
    invalid.a_offset = 16;
    result = runtime::examples::tiled_int8_gemm(invalid);
    ok &= expect(
        !result && result.error() == runtime::matrix_program_error::overlapping_local_regions,
        "tiled GEMM command overlap"
    );
    return ok;
}

bool test_dma_encoding_contract() {
    const auto maximum = holon_npu::model::decode(
        runtime::encode_dma_load(1, 2, 3, HOLON_NPU_ISA_DMA_MAX_WORDS)
    );
    bool ok = expect(
        maximum.rd == 1 && maximum.rs1 == 2 && maximum.rs2 == 3 &&
            maximum.imm == HOLON_NPU_ISA_IMM_MASK,
        "DMA register and maximum count encoding"
    );
    try {
        static_cast<void>(runtime::encode_dma_store(1, 2, 3, 0));
        ok &= expect(false, "DMA zero count rejected");
    } catch (const std::invalid_argument&) {
        ok &= expect(true, "DMA zero count rejected");
    }
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= run_vector_add();
    ok &= run_relu();
    ok &= run_reduce_sum();
    ok &= run_requant();
    ok &= run_transpose4();
    ok &= run_int8_gemm();
    ok &= run_tiled_int8_gemm_shape(1, 1, 1);
    ok &= run_tiled_int8_gemm_shape(16, 16, 16);
    ok &= run_tiled_int8_gemm_shape(17, 19, 23);
    ok &= run_tiled_int8_gemm_shape(64, 64, 64);
    ok &= test_tiled_int8_gemm_validation();
    ok &= test_dma_encoding_contract();
    return ok ? 0 : 1;
}
