#pragma once

#include "holon_npu_scalar.hpp"

namespace holon_npu::semantic { class program_machine; }

namespace holon_npu::semantic::scalar {

struct hart_token_tag;
struct elapsed_cycles_tag;
using hart_token = strong_value<std::uint64_t, hart_token_tag>;
using elapsed_cycles = strong_value<std::uint64_t, elapsed_cycles_tag>;
using external_effect = std::variant<load_request, store_request, fence_request>;
struct pending_effect { hart_token token{}; instruction_address pc{}; external_effect request; };
struct committed { instruction_address pc{}, next_pc{}; std::uint64_t total{}; };
struct trap_taken { instruction_address pc{}, handler{}; std::uint32_t cause{}, value{}; };
struct sleeping {};
using hart_event = std::variant<committed, pending_effect, trap_taken, sleeping>;
struct load_data { std::span<const std::byte> bytes; };
struct acknowledged {};
struct access_fault { std::optional<physical_address> address; };
using external_result = std::variant<load_data, acknowledged, access_fault>;
struct interrupt_lines { bool software{}, timer{}, external{}; };
enum class hart_error {
    operation_pending, no_pending, wrong_token, invalid_result, token_exhausted,
    invalid_entry, unknown_csr
};

// Architectural state/commit only: no fetch loop, memory ownership or timing model.
class hart_state {
public:
    hart_state();
    void reset();
    [[nodiscard]] std::expected<void, hart_error> start(instruction_address entry);
    [[nodiscard]] std::expected<hart_event, hart_error> issue(instruction::scalar_word word);
    [[nodiscard]] std::expected<hart_event, hart_error> complete(hart_token token, external_result result);
    [[nodiscard]] std::expected<std::optional<trap_taken>, hart_error> poll_interrupt();
    [[nodiscard]] std::expected<trap_taken, hart_error> fetch_fault(
        std::optional<instruction_address> failing_address = {});
    void set_interrupts(interrupt_lines lines);
    void account_cycles(elapsed_cycles elapsed);
    [[nodiscard]] instruction_address pc() const { return instruction_address{pc_}; }
    [[nodiscard]] std::uint64_t retired() const { return retired_; }
    [[nodiscard]] std::uint32_t reg(scalar_register index) const;
    [[nodiscard]] std::expected<std::uint32_t, hart_error> read_csr(csr_address address) const;
    [[nodiscard]] bool waiting() const { return waiting_; }
    [[nodiscard]] bool pending() const { return pending_.has_value(); }
    [[nodiscard]] std::optional<pending_effect> pending_request() const {
        return pending_ ? std::optional{pending_->event} : std::nullopt;
    }

private:
    friend class ::holon_npu::semantic::program_machine;
    struct pending_context { pending_effect event; instruction_address next_pc; };
    std::array<std::int32_t, instruction::register_count> registers_{};
    std::uint32_t pc_{};
    std::uint64_t retired_{}, cycle_{}, instret_{}, next_token_{1};
    std::array<std::uint32_t, instruction::machine_csrs.size()> csrs_{};
    bool waiting_{};
    std::optional<pending_context> pending_;

    std::uint32_t& csr(instruction::machine_csr address);
    [[nodiscard]] std::uint32_t csr(instruction::machine_csr address) const;
    void write_csr(instruction::machine_csr address, std::uint32_t value);
    void write_register(const register_result& result);
    void retire(bool count = true);
    [[nodiscard]] committed commit(instruction_address next_pc, bool count = true);
    [[nodiscard]] trap_taken enter_trap(std::uint32_t cause, std::uint32_t value);
};

} // namespace holon_npu::semantic::scalar
