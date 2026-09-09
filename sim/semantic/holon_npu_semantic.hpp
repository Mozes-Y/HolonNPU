#pragma once

#include "holon_npu_elf.hpp"
#include "holon_npu_hart.hpp"

namespace holon_npu::semantic {

struct machine_config {
    std::size_t program_bytes = 65536, scratchpad_bytes = 65536;
    std::uint32_t vector_bytes = 64, matrix_rows = 16, matrix_cols = 16, tile_bytes = 1024;
};
enum class api_error { operation_pending, no_pending, wrong_token, invalid_result,
    token_exhausted, invalid_state, unimplemented_operation };
enum class boot_error { operation_pending, mapping, image };
struct memory_request {
    physical_address address{};
    std::uint32_t size{};
    memory::access access{};
    memory::storage storage{};
    std::vector<std::byte> payload;
};
struct npu_footprint {
    std::uint32_t lanes{}, active_lanes{}, element_bytes{};
    std::uint64_t local_read_bytes{}, local_write_bytes{};
    std::uint32_t matrix_m{}, matrix_n{}, matrix_k{};
};
struct npu_request { instruction::npu_instruction instruction; npu_footprint footprint; };
using operation = std::variant<memory_request, npu_request, scalar::fence_request>;
struct pending_operation { operation_token token{}; instruction_address pc{}; operation value; };
struct operation_success {};
struct read_payload { std::vector<std::byte> bytes; };
struct bus_fault { std::optional<physical_address> address; };
using operation_result = std::variant<operation_success, read_payload, bus_fault>;
struct progress_event {};
struct stopped { std::uint32_t status{}; };
using execution_event = std::variant<progress_event, scalar::committed, scalar::trap_taken,
    pending_operation, scalar::sleeping, stopped>;
struct execution_fault { std::uint32_t cause{}, value{}; };

// The only program executor. Local storage is owned here; system storage is not.
class program_machine {
public:
    program_machine(memory::physical_map map, machine_config config = {});
    void reset();
    [[nodiscard]] std::expected<void, boot_error> boot(const elf::image& image, system_memory_view system);
    [[nodiscard]] std::expected<void, boot_error> boot(
        std::span<const std::byte> program, physical_address base, instruction_address entry);
    [[nodiscard]] std::expected<execution_event, api_error> advance();
    [[nodiscard]] std::expected<execution_event, api_error> complete(operation_token token, operation_result result);
    [[nodiscard]] std::optional<pending_operation> pending() const;
    [[nodiscard]] const scalar::hart_state& hart() const { return hart_; }
    [[nodiscard]] bool done() const { return stop_.has_value(); }
    [[nodiscard]] std::optional<std::uint32_t> exit_status() const { return stop_; }
    [[nodiscard]] const machine_config& config() const { return config_; }
    void set_interrupts(scalar::interrupt_lines lines) { hart_.set_interrupts(lines); }
    void account_cycles(scalar::elapsed_cycles elapsed) { hart_.account_cycles(elapsed); }
    [[nodiscard]] std::span<const std::byte> local_bytes() const { return scratchpad_; }
    [[nodiscard]] std::span<const std::byte> vector_bytes(instruction::vector_register r) const { return vectors_.at(r.value()); }
    [[nodiscard]] std::span<const std::uint8_t> predicate_bits(instruction::predicate_register r) const { return predicates_.at(r.value()); }

private:
    enum class purpose { fetch_first, fetch_second, scalar, dma_load, dma_store, npu };
    struct pending_context { pending_operation event; purpose why; };
    struct tile_view {
        bool valid{}; std::uint32_t base{}, rows{}, cols{};
        std::int32_t row_stride{}, col_stride{};
        instruction::npu_type type{};
    };
    struct tile {
        bool valid{}; std::uint32_t rows{}, cols{};
        instruction::npu_type type{}; std::vector<std::uint32_t> elements;
    };
    memory::physical_map map_;
    machine_config config_;
    scalar::hart_state hart_;
    std::vector<std::byte> program_, scratchpad_;
    std::array<std::vector<std::byte>, instruction::npu_vector_count> vectors_;
    std::array<std::vector<std::uint8_t>, instruction::npu_predicate_count> predicates_;
    std::array<tile_view, instruction::npu_view_count> views_{};
    std::array<tile, instruction::npu_tile_count> tiles_{};
    std::optional<pending_context> pending_;
    std::optional<instruction::instruction_frame> frame_;
    std::optional<std::uint32_t> stop_;
    std::optional<local_address> dma_local_;
    std::optional<std::uint32_t> first_parcel_;
    std::uint64_t next_token_ = 1;
    bool started_{};

    [[nodiscard]] scalar::trap_taken trap(execution_fault fault);
    [[nodiscard]] std::expected<execution_event, api_error> issue(operation request, purpose why);
    [[nodiscard]] std::expected<memory_request, execution_fault> memory_operation(
        physical_address address, std::uint32_t size, memory::access access, std::span<const std::byte> payload = {});
    [[nodiscard]] std::expected<execution_event, api_error> issue_scalar(instruction::scalar_word word);
    [[nodiscard]] std::expected<execution_event, api_error> issue_npu(instruction::holon_word word);
    [[nodiscard]] npu_footprint describe_npu(const instruction::npu_instruction& instruction) const;
    [[nodiscard]] unsigned vector_capacity(const instruction::npu_instruction& instruction) const;
    [[nodiscard]] bool tile_shape_fits(std::uint32_t rows, std::uint32_t cols, instruction::npu_type type) const;
    [[nodiscard]] std::expected<execution_event, api_error> scalar_event(const scalar::hart_event& event);
    [[nodiscard]] scalar::committed retire_npu(std::optional<scalar::register_write> write = {});
    [[nodiscard]] std::expected<std::optional<scalar::register_write>, execution_fault> execute_npu(
        const instruction::npu_instruction& inst);
    [[nodiscard]] std::expected<local_address, execution_fault> local_range(
        std::int64_t address, std::size_t size, bool write, unsigned alignment = 1) const;
};

} // namespace holon_npu::semantic
