#include "holon_npu_memory.hpp"

#include <algorithm>
#include <concepts>
#include <cstring>
#include <limits>
#include <type_traits>

namespace holon_npu::semantic {

std::optional<std::span<std::byte>> system_memory_view::range(system_address address, std::size_t size) const {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if ((!bytes.empty() && bytes.size() - 1 > maximum - base.value()) || address < base
        || (size && size - 1 > maximum - address.value())) return std::nullopt;
    const auto offset = address.value() - base.value();
    if (offset > bytes.size() || size > bytes.size() - offset) return std::nullopt;
    return bytes.subspan(static_cast<std::size_t>(offset), size);
}

namespace memory {
namespace {
constexpr std::uint64_t address_space = std::uint64_t{1} << 32;
template<class T> std::optional<std::span<T>> local_range(std::span<T> bytes, std::uint32_t offset, std::size_t size) {
    if (offset > bytes.size() || size > bytes.size() - offset) return std::nullopt;
    return bytes.subspan(offset, size);
}
}

std::expected<physical_map, map_error> physical_map::create(std::span<const region> regions) {
    std::vector<region> sorted{regions.begin(), regions.end()};
    std::ranges::sort(sorted, {}, &region::base);
    bool program_seen = false, scratchpad_seen = false;
    std::uint64_t end = 0;
    for (const auto& r : sorted) {
        if (!r.size) return std::unexpected(map_error::empty_region);
        if (r.size > address_space - r.base.value()) return std::unexpected(map_error::address_overflow);
        if (r.base.value() < end) return std::unexpected(map_error::overlap);
        if (!r.allowed.read && !r.allowed.write && !r.allowed.execute)
            return std::unexpected(map_error::invalid_permissions);
        switch (r.target) {
        case storage::program:
            if (r.allowed.write) return std::unexpected(map_error::invalid_permissions);
            if (program_seen) return std::unexpected(map_error::duplicate_local);
            program_seen = true;
            break;
        case storage::scratchpad:
            if (r.allowed.execute) return std::unexpected(map_error::invalid_permissions);
            if (scratchpad_seen) return std::unexpected(map_error::duplicate_local);
            scratchpad_seen = true;
            break;
        case storage::system: break;
        default: return std::unexpected(map_error::invalid_permissions);
        }
        end = r.base.value() + r.size;
    }
    return physical_map{std::move(sorted)};
}

std::expected<routed_access, access_error> physical_map::resolve(
    physical_address address, std::size_t size, access kind) const {
    if (!size || (kind != access::read && kind != access::write && kind != access::execute))
        return std::unexpected(access_error::invalid_request);
    if (size > address_space - address.value()) return std::unexpected(access_error::address_overflow);
    for (const auto& r : regions_) {
        if (address < r.base || address.value() - r.base.value() >= r.size) continue;
        const auto offset = address.value() - r.base.value();
        if (size > r.size - offset) return std::unexpected(access_error::unmapped);
        const bool allowed = kind == access::read ? r.allowed.read
            : (kind == access::write ? r.allowed.write : r.allowed.execute);
        if (!allowed) return std::unexpected(access_error::permission);
        switch (r.target) {
        case storage::program: return program_slice{program_offset{offset}, size};
        case storage::scratchpad: return scratchpad_slice{local_address{offset}, size};
        case storage::system: return system_slice{system_address{address.value()}, size};
        }
    }
    return std::unexpected(access_error::unmapped);
}

std::expected<void, access_error> physical_map::read(
    physical_address address, std::span<std::byte> destination, bindings memory, access kind) const {
    if (kind == access::write) return std::unexpected(access_error::invalid_request);
    const auto route = resolve(address, destination.size(), kind);
    if (!route) return std::unexpected(route.error());
    const auto source = std::visit([&](const auto& r) -> std::optional<std::span<const std::byte>> {
        using T = std::remove_cvref_t<decltype(r)>;
        if constexpr (std::same_as<T, program_slice>) return local_range(memory.program, r.offset.value(), r.size);
        else if constexpr (std::same_as<T, scratchpad_slice>) return local_range(memory.scratchpad, r.offset.value(), r.size);
        else return memory.system.range(r.address, r.size);
    }, *route);
    if (!source) return std::unexpected(access_error::backing_bounds);
    std::memmove(destination.data(), source->data(), destination.size());
    return {};
}

std::expected<void, access_error> physical_map::write(
    physical_address address, std::span<const std::byte> source, bindings memory) const {
    const auto route = resolve(address, source.size(), access::write);
    if (!route) return std::unexpected(route.error());
    const auto destination = std::visit([&](const auto& r) -> std::optional<std::span<std::byte>> {
        using T = std::remove_cvref_t<decltype(r)>;
        if constexpr (std::same_as<T, program_slice>) return std::nullopt;
        else if constexpr (std::same_as<T, scratchpad_slice>) return local_range(memory.scratchpad, r.offset.value(), r.size);
        else return memory.system.range(r.address, r.size);
    }, *route);
    if (!destination) return std::unexpected(access_error::backing_bounds);
    std::memmove(destination->data(), source.data(), source.size());
    return {};
}

} // namespace memory
} // namespace holon_npu::semantic
