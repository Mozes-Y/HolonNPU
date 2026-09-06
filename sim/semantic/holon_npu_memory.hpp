#pragma once

#include "holon_npu_types.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace holon_npu::semantic {

struct system_memory_view {
    system_address base{};
    std::span<std::byte> bytes;
    [[nodiscard]] std::optional<std::span<std::byte>> range(system_address address, std::size_t size) const;
};

namespace memory {

enum class storage { program, scratchpad, system };
enum class access { read, write, execute };
struct permissions { bool read{}, write{}, execute{}; };
struct region {
    physical_address base{};
    std::uint64_t size{};
    storage target{};
    permissions allowed{};
};
enum class map_error { empty_region, address_overflow, overlap, duplicate_local, invalid_permissions };
enum class access_error { invalid_request, address_overflow, unmapped, permission, backing_bounds };
struct program_offset_tag;
using program_offset = strong_value<std::uint32_t, program_offset_tag>;
struct program_slice { program_offset offset{}; std::size_t size{}; };
struct scratchpad_slice { local_address offset{}; std::size_t size{}; };
struct system_slice { system_address address{}; std::size_t size{}; };
using routed_access = std::variant<program_slice, scratchpad_slice, system_slice>;

struct bindings {
    std::span<const std::byte> program;
    std::span<std::byte> scratchpad;
    system_memory_view system;
};

// Validated mapping only. Backing storage remains with the core/environment.
class physical_map {
public:
    [[nodiscard]] static std::expected<physical_map, map_error> create(std::span<const region> regions);
    [[nodiscard]] std::expected<routed_access, access_error> resolve(
        physical_address address, std::size_t size, access kind) const;
    [[nodiscard]] std::expected<void, access_error> read(
        physical_address address, std::span<std::byte> destination, bindings memory,
        access kind = access::read) const;
    [[nodiscard]] std::expected<void, access_error> write(
        physical_address address, std::span<const std::byte> source, bindings memory) const;
private:
    explicit physical_map(std::vector<region> regions) : regions_(std::move(regions)) {}
    std::vector<region> regions_;
};

} // namespace memory
} // namespace holon_npu::semantic
