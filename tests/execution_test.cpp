#include "holon_npu_execution.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string_view>

namespace {

using namespace holon_npu::semantic;
namespace runtime = holon_npu::runtime;

void require(bool condition, std::string_view message,
             std::source_location location = std::source_location::current()) {
    if (!condition) {
        throw std::runtime_error(std::format("{}:{}: {}", location.file_name(), location.line(), message));
    }
}

void put_word(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) {
        bytes[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
    }
}

std::uint32_t get_word(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + byte]) << (byte * 8U);
    }
    return value;
}

pending_operation next_operation(program_machine& machine) {
    const auto event = machine.advance();
    require(event && std::holds_alternative<pending_operation>(*event), "expected pending operation");
    return std::get<pending_operation>(*event);
}

void test_boot_validation() {
    program_machine machine(256);
    program_builder program;
    program.movi(1, 7).exit();
    std::array data{std::byte{0x5a}};
    const boot_image valid{program.span(), {}, 256, data, local_address{255}};
    require(machine.boot(valid).has_value(), "valid image");
    require(machine.advance().has_value(), "retire initial scalar instruction");
    const auto before = machine.snapshot();
    auto reject = [&](boot_image image, boot_error expected) {
        const auto result = machine.boot(image);
        require(!result && result.error() == expected, "typed boot rejection");
        const auto after = machine.snapshot();
        require(after.state == before.state && after.pc == before.pc &&
                after.retired == before.retired && after.fault == before.fault &&
                machine.scalar_register(1) == 7 &&
                machine.read_i8(local_address{255}, 1).front() == 0x5a,
                "rejected boot preserves architectural state");
    };
    auto invalid = valid;
    invalid.instructions = {};
    reject(invalid, boot_error::invalid_program_size);
    std::vector<std::uint32_t> oversized(HOLON_NPU_PROGRAM_MEM_MAX_BYTES / 4 + 1);
    invalid = valid;
    invalid.instructions = oversized;
    reject(invalid, boot_error::invalid_program_size);
    for (const auto entry : {2U, 8U, 0xffff'fffcU}) {
        invalid = valid;
        invalid.entry = instruction_address{entry};
        reject(invalid, boot_error::invalid_entry);
    }
    for (const auto size : {0U, 255U, 260U}) {
        invalid = valid;
        invalid.local_memory_bytes = size;
        reject(invalid, boot_error::invalid_local_memory_size);
    }
    for (const auto address : {256U, 0xffff'ffffU}) {
        invalid = valid;
        invalid.data_address = local_address{address};
        reject(invalid, boot_error::invalid_data_range);
    }

    const std::array entry_program{encode_system_fault(), encode_system_exit()};
    require(machine.boot({entry_program, instruction_address{4}, 256}).has_value(), "nonzero entry");
    const auto result = run_program(machine, {}, 1);
    require(result && result->state == lifecycle_state::done && result->retired == 1,
            "boot entry controls first instruction");
    require(machine.scalar_register(1) == 0 && machine.read_i8(local_address{255}, 1).front() == 0,
            "cold boot clears scalar and local state");
}

void test_cold_vector_state() {
    program_machine machine(256);
    std::array<std::byte, 64> local{};
    std::ranges::fill(std::span(local).first(16), std::byte{0x7f});
    program_builder dirty;
    dirty.configure(4, vector_element_width::bits_32, true).load(0, 0)
        .predicate_load(0, 32).configure(4, vector_element_width::bits_8, false).exit();
    require(machine.boot({dirty.span(), {}, 256, local}).has_value(), "seed vector/predicate state");
    require(run_program(machine, {}, 16).has_value(), "dirty program done");

    program_builder clean;
    clean.configure(4, vector_element_width::bits_32, true).store(0, 64).exit();
    std::ranges::fill(local, std::byte{0x55});
    require(machine.boot({clean.span(), {}, 256, local, local_address{64}}).has_value(), "cold boot");
    require(machine.vl() == 0 && machine.elements_signed() &&
            machine.element_width() == vector_element_width::bits_32, "reset vector configuration");
    const auto result = run_program(machine, {}, 3);
    require(result && result->state == lifecycle_state::done, "clean vector program done");
    require(machine.read_i32(local_address{64}, 4) == std::vector<std::int32_t>(4, 0),
            "boot clears vector registers and restores all-active predicate");
}

void test_tokens_across_boot() {
    program_machine machine(256);
    program_builder program;
    program.configure(1, vector_element_width::bits_32, true).exit();
    const boot_image image{program.span(), {}, 256};
    require(machine.boot(image).has_value(), "first boot");
    const auto first = next_operation(machine);
    const auto rejected = machine.boot(image);
    require(!rejected && rejected.error() == boot_error::operation_pending,
            "cannot boot while an operation is pending");
    require(machine.pc() == 0 && machine.retired() == 0, "pending operation remains precise");
    require(machine.complete(first.token, operation_success{}).has_value(), "original token valid");
    require(machine.boot(image).has_value(), "reboot after completion");
    const auto second = next_operation(machine);
    require(first.token != second.token, "token not reused across boot");
    const auto stale = machine.complete(first.token, operation_success{});
    require(!stale && stale.error() == api_error::token_mismatch, "stale token rejected");
    require(machine.retired() == 0 && machine.pc() == 0, "stale token has no side effects");
    machine.reset();
    require(machine.boot(image).has_value(), "boot after explicit reset");
    const auto third = next_operation(machine);
    require(second.token != third.token, "reset does not recycle pending token");
    require(!machine.complete(second.token, operation_success{}), "pre-reset token rejected");
    require(machine.complete(third.token, operation_success{}).has_value(), "current token accepted");
}

void test_execution_limits() {
    program_machine machine(256);
    require(!run_program(machine, {}, 1), "unbooted machine cannot run");
    program_builder loop;
    loop.scalar_addi(1, 1, 1).beq(0, 0, -1);
    require(machine.boot({loop.span(), {}, 256}).has_value(), "loop boot");
    for (const auto budget : {0U, 5U, 3U}) {
        const auto retired = machine.retired();
        const auto result = run_program(machine, {}, budget);
        require(!result && std::holds_alternative<instruction_budget_exhausted>(result.error()),
                "instruction limit is an environment error");
        require(machine.retired() == retired + budget && machine.fault() == architectural_fault::none &&
                machine.state() == lifecycle_state::running, "budget preserves live machine without fault");
    }
    require(machine.scalar_register(1) == 4 && machine.pc() == 0, "budget can resume a scalar loop");

    program_builder delayed;
    delayed.configure(1, vector_element_width::bits_32, true).exit();
    require(machine.boot({delayed.span(), {}, 256}).has_value(), "delayed operation boot");
    const auto pending = next_operation(machine);
    const auto interrupted = run_program(machine, {}, 4);
    require(!interrupted && std::get<api_error>(interrupted.error()) == api_error::operation_pending,
            "runner does not steal externally issued operation");
    require(machine.complete(pending.token, operation_success{}).has_value(), "external completion");
    const auto resumed = run_program(machine, {}, 1);
    require(resumed && resumed->state == lifecycle_state::done, "resume after completion");
    const auto terminal = run_program(machine, {}, 0);
    require(terminal && terminal->retired == 2, "terminal query needs no instruction budget");
}

void test_random_vector_programs() {
    constexpr auto seed = 0x48504e55U;
    std::mt19937 random(seed);
    constexpr system_address base{0x1'8000'0000ULL};
    program_machine machine(512);
    for (std::uint32_t trial = 0; trial < 64; ++trial) {
        const auto vl = static_cast<std::uint16_t>(trial % 16 + 1);
        const auto iterations = static_cast<std::uint16_t>(random() % 7 + 1);
        std::array<std::byte, 512> memory{};
        std::array<std::byte, 16> local{};
        put_word(local, 0, static_cast<std::uint32_t>(base.value()));
        put_word(local, 4, static_cast<std::uint32_t>(base.value() >> 32));
        put_word(local, 8, static_cast<std::uint32_t>(base.value() + 256));
        std::vector<std::uint32_t> expected(vl);
        for (std::size_t lane = 0; lane < vl; ++lane) {
            const auto a = static_cast<std::uint32_t>(random());
            const auto b = static_cast<std::uint32_t>(random());
            put_word(memory, lane * 4, a);
            put_word(memory, 64 + lane * 4, b);
            expected[lane] = a + b * iterations;
        }
        program_builder program;
        program.scalar_load(1, 0, 0).scalar_load(2, 0, 4).movi(3, 64)
            .dma_load(1, 2, 3, 32).wait_dma().fence_local()
            .configure(vl, vector_element_width::bits_32, true).load(0, 64).load(1, 128)
            .movi(6, iterations).add(0, 0, 1).scalar_addi(6, 6, -1).bne(6, 0, -2)
            .store(0, 192).fence_dma().scalar_load(1, 0, 8).movi(3, 192)
            .dma_store(1, 2, 3, vl).wait_dma().exit();
        require(machine.boot({program.span(), {}, 512, local}).has_value(), "vector boot");
        const auto result = run_program(machine, {base, memory}, 128);
        const auto context = std::format("seed={} case={} vl={} iterations={}", seed, trial, vl, iterations);
        require(result && result->state == lifecycle_state::done &&
                result->retired == program.size() + 3U * (iterations - 1U), context);
        for (std::size_t lane = 0; lane < vl; ++lane) {
            require(get_word(memory, 256 + lane * 4) == expected[lane], context);
        }
        require(std::ranges::all_of(std::span(memory).subspan(256 + vl * 4),
                                   [](std::byte b) { return b == std::byte{0}; }),
                "DMA store respects vector tail");
        require(machine.dma_events().size() == 2, "only program-issued DMA events");
    }
    std::cout << "  vector seed=" << seed << " cases=64 vl=1..16\n";
}

void test_dma_fault_precision() {
    for (const auto write : {false, true}) {
        program_machine machine(256);
        program_builder program;
        program.movi(1, 64).movi(2, 0).movi(3, 0);
        if (write) {
            program.dma_store(1, 2, 3, 4);
        } else {
            program.dma_load(1, 2, 3, 4);
        }
        program.exit();
        require(machine.boot({program.span(), {}, 256}).has_value(), "DMA fault boot");
        std::array<std::byte, 8> memory{};
        const auto result = run_program(machine, {{}, memory}, 16);
        require(result && result->state == lifecycle_state::fault && result->pc == 12 &&
                result->retired == 3 && result->fault ==
                    (write ? architectural_fault::axi_write : architectural_fault::axi_read),
                "DMA fault is precise and distinct from a simulation error");
        require(machine.dma_events().empty(), "failed DMA never retires");
    }
    std::array<std::byte, 16> memory{};
    const auto top = std::numeric_limits<std::uint64_t>::max();
    const program_dma_operation request{
        .system = system_address{top - 3}, .byte_count = 8,
    };
    require(std::holds_alternative<operation_failure>(
                service_operation(request, {system_address{top - 7}, memory})),
            "64-bit address wrap cannot alias mapped memory");
    const auto below = program_dma_operation{.system = system_address{15}, .byte_count = 4};
    require(std::holds_alternative<operation_failure>(service_operation(below, {system_address{16}, memory})),
            "address below memory base rejected");
}

void test_matrix_programs() {
    for (const auto shape : {std::array{1U, 1U, 1U}, std::array{16U, 16U, 16U},
                             std::array{17U, 19U, 23U}, std::array{64U, 64U, 64U}}) {
        const auto [m, n, k] = shape;
        constexpr std::uint32_t a_offset = 4096, b_offset = 8192, c_offset = 12288;
        const auto plan = runtime::examples::tiled_int8_gemm({
            .m = m, .n = n, .k = k, .a_offset = a_offset, .b_offset = b_offset,
            .c_offset = c_offset, .a_row_stride_bytes = k, .b_row_stride_bytes = n,
            .c_row_stride_bytes = n * 4, .local_mem_bytes = 32768, .command_offset = 0,
        });
        require(plan.has_value(), "matrix program construction");
        std::vector<std::byte> local(32768);
        require(plan->write_commands(local), "matrix command image");
        for (std::uint32_t i = 0; i < m * k; ++i) {
            local[a_offset + i] = std::byte{static_cast<std::uint8_t>(static_cast<int>(i % 13) - 6)};
        }
        for (std::uint32_t i = 0; i < k * n; ++i) {
            local[b_offset + i] = std::byte{static_cast<std::uint8_t>(static_cast<int>(i % 11) - 5)};
        }
        put_word(local, 2048, c_offset);
        program_builder program;
        for (const auto word : plan->image.span().first(plan->image.words.size() - 1)) {
            program.raw(word);
        }
        program.movi(1, 0).movi(2, 0).movi(4, 1024).scalar_load(3, 4, 1024)
            .fence_dma().dma_store(1, 2, 3, static_cast<std::uint16_t>(m * n)).wait_dma().exit();
        program_machine machine(32768);
        require(machine.boot({program.span(), {}, 32768, local}).has_value(), "matrix boot");
        std::vector<std::byte> memory(m * n * 4);
        const auto result = run_program(machine, {{}, memory}, program.size());
        require(result && result->state == lifecycle_state::done, "matrix program completion");
        for (std::uint32_t row = 0; row < m; ++row) {
            for (std::uint32_t col = 0; col < n; ++col) {
                std::int32_t expected = 0;
                for (std::uint32_t inner = 0; inner < k; ++inner) {
                    expected += std::bit_cast<std::int8_t>(local[a_offset + row * k + inner]) *
                                std::bit_cast<std::int8_t>(local[b_offset + inner * n + col]);
                }
                require(get_word(memory, (row * n + col) * 4) == static_cast<std::uint32_t>(expected),
                        "program DMA writeback matches independent GEMM reference");
            }
        }
        require(machine.matrix_events().size() == plan->commands.size(), "all matrix tiles executed");
        std::cout << std::format("  matrix m={} n={} k={} retired={} tiles={}\n",
                                 m, n, k, result->retired, plan->commands.size());
    }
}

}  // namespace

int main() {
    try {
        test_boot_validation();
        test_cold_vector_state();
        test_tokens_across_boot();
        test_execution_limits();
        test_random_vector_programs();
        test_dma_fault_precision();
        test_matrix_programs();
        std::cout << "autonomous execution: boot, reset, tokens, budgets, DMA, vector, matrix PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
