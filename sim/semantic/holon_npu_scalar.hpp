#pragma once

#include "holon_npu_instruction.hpp"

#include <array>
#include <optional>

namespace holon_npu::semantic::scalar {

using instruction::scalar_register;
using instruction::scalar_trap_cause;

struct operands { std::uint32_t rs1{}, rs2{}; };
struct register_write { scalar_register destination{}; std::uint32_t value{}; };
struct register_result { std::optional<register_write> write; };

enum class access_width : std::uint8_t { byte = 1, halfword = 2, word = 4 };
enum class extension : std::uint8_t { zero, sign };

struct load_request {
    physical_address address{};
    access_width width{};
    extension extend{};
    scalar_register destination{};
};

struct store_request {
    physical_address address{};
    access_width width{};
    std::array<std::byte, 4> payload{};
};

struct fence_request { std::uint8_t predecessor{}, successor{}; };
enum class csr_action : std::uint8_t { replace, set, clear };
struct csr_address_tag;
using csr_address = strong_value<std::uint16_t, csr_address_tag>;

struct csr_request {
    csr_address address{};
    scalar_register destination{};
    csr_action action{};
    std::uint32_t source{};
    bool read{}, write{};
};

enum class machine_request : std::uint8_t { return_from_trap, wait_for_interrupt };
using effect = std::variant<register_result, load_request, store_request,
                            fence_request, csr_request, machine_request>;

struct step {
    // Sequential/branch continuation; MRET resolves its target from machine state.
    instruction_address pc{}, next_pc{};
    effect value{};
};

struct trap {
    scalar_trap_cause cause{};
    instruction_address pc{};
    std::uint32_t value{};
};

enum class completion_error : std::uint8_t { invalid_request, payload_size };

// Source values are captured before any destination write, including rd == rs1/rs2.
[[nodiscard]] std::expected<step, trap> evaluate(
    instruction::scalar_word word, instruction_address pc, operands sources);

// Transport failures are handled by the caller; malformed payloads are API errors.
[[nodiscard]] std::expected<register_result, completion_error> complete_load(
    const load_request& request, std::span<const std::byte> payload);

} // namespace holon_npu::semantic::scalar
