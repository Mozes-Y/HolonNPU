#pragma once

#include <compare>
#include <cstdint>

namespace holon_npu::semantic {

template <typename Representation, typename Tag>
class strong_value {
public:
    constexpr strong_value() = default;
    explicit constexpr strong_value(Representation value) : value_(value) {}

    [[nodiscard]] constexpr Representation value() const { return value_; }
    constexpr auto operator<=>(const strong_value&) const = default;

private:
    Representation value_{};
};

struct system_address_tag;
struct local_address_tag;
struct instruction_address_tag;
struct operation_token_tag;

using system_address = strong_value<std::uint64_t, system_address_tag>;
using local_address = strong_value<std::uint32_t, local_address_tag>;
using instruction_address = strong_value<std::uint32_t, instruction_address_tag>;
using operation_token = strong_value<std::uint64_t, operation_token_tag>;

} // namespace holon_npu::semantic
