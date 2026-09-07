#include "holon_npu_elf.hpp"
#include "holon_npu_scalar_metadata.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <ranges>
#include <string_view>
#include <type_traits>

namespace holon_npu::semantic::elf {
namespace {
constexpr std::uint32_t pt_load = 1, pt_attributes = 0x70000003, sht_attributes = 0x70000003;
constexpr std::uint64_t address_space = std::uint64_t{1} << 32;
bool fits(std::size_t capacity, std::size_t offset, std::size_t size) {
    return offset <= capacity && size <= capacity - offset;
}
// Called only inside previously bounded ELF headers/attribute records.
std::uint32_t integer(std::span<const std::byte> bytes, std::size_t offset, unsigned width = 4) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return value;
}
struct cursor {
    std::span<const std::byte> bytes;
    std::size_t offset{};
    std::optional<std::span<const std::byte>> take(std::size_t size) {
        if (!fits(bytes.size(), offset, size)) return std::nullopt;
        const auto result = bytes.subspan(offset, size);
        offset += size;
        return result;
    }
    std::optional<std::uint32_t> word() {
        const auto value = take(4);
        return value ? std::optional{integer(*value, 0)} : std::nullopt;
    }
    std::optional<std::uint32_t> leb() {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 5; ++i) {
            const auto byte = take(1);
            if (!byte) return std::nullopt;
            const auto v = std::to_integer<std::uint32_t>((*byte)[0]);
            if (i == 4 && v > 15) return std::nullopt;
            value |= (v & 127) << (7 * i);
            if (!(v & 128)) return value;
        }
        return std::nullopt;
    }
    std::optional<std::string_view> string() {
        const auto rest = bytes.subspan(offset);
        const auto end = std::ranges::find(rest, std::byte{});
        if (end == rest.end()) return std::nullopt;
        const auto size = static_cast<std::size_t>(end - rest.begin());
        const std::string_view value{reinterpret_cast<const char*>(rest.data()), size};
        offset += size + 1;
        return value;
    }
    bool done() const { return offset == bytes.size(); }
};

bool supported_arch(std::string_view arch) {
    bool first = true;
    std::array<bool, instruction::elf_extensions.size()> seen{};
    for (auto field : arch | std::views::split('_')) {
        const std::string_view extension{field.begin(), field.end()};
        if (first) {
            if (extension != instruction::elf_base) return false;
            first = false;
        } else {
            const auto found = std::ranges::find(instruction::elf_extensions, extension);
            if (found == instruction::elf_extensions.end()) return false;
            const auto index = static_cast<std::size_t>(found - instruction::elf_extensions.begin());
            if (seen[index]) return false;
            seen[index] = true;
        }
    }
    return !first && !arch.ends_with('_');
}

std::expected<void, error> attributes(std::span<const std::byte> bytes) {
    if (bytes.empty() || bytes[0] != std::byte{'A'}) return std::unexpected(error::attributes);
    cursor file{bytes.subspan(1)};
    const auto vendor_size = file.word();
    if (!vendor_size || *vendor_size < 4) return std::unexpected(error::attributes);
    const auto vendor_bytes = file.take(*vendor_size - 4);
    if (!vendor_bytes || !file.done()) return std::unexpected(error::attributes);
    cursor vendor{*vendor_bytes};
    const auto name = vendor.string();
    if (!name || *name != "riscv") return std::unexpected(error::unsupported_profile);
    const auto start = vendor.offset;
    const auto tag = vendor.leb(), size = vendor.word();
    if (!tag || !size || *tag != 1 || *size < vendor.offset - start) return std::unexpected(error::attributes);
    const auto fields = vendor.take(*size - (vendor.offset - start));
    if (!fields || !vendor.done()) return std::unexpected(error::attributes);
    cursor values{*fields};
    std::array<bool, 17> seen{};
    while (!values.done()) {
        const auto id = values.leb();
        if (!id) return std::unexpected(error::attributes);
        if (*id < seen.size()) {
            if (seen[*id]) return std::unexpected(error::attributes);
            seen[*id] = true;
        }
        if (*id & 1) {
            const auto value = values.string();
            if (!value) return std::unexpected(error::attributes);
            if (*id == 5) {
                if (!supported_arch(*value)) return std::unexpected(error::unsupported_profile);
            } else if (*id % 128 < 64) return std::unexpected(error::unsupported_profile);
        } else {
            const auto value = values.leb();
            if (!value) return std::unexpected(error::attributes);
            switch (*id) {
            case 4: if (*value != instruction::elf_stack_alignment) return std::unexpected(error::unsupported_profile); break;
            case 6: case 14: if (*value != 0) return std::unexpected(error::unsupported_profile); break;
            case 16: if (*value > 1) return std::unexpected(error::unsupported_profile); break;
            case 8: case 10: case 12: break; // Deprecated, advisory privileged-version attributes.
            default: if (*id % 128 < 64) return std::unexpected(error::unsupported_profile);
            }
        }
    }
    if (!seen[5]) return std::unexpected(error::attributes);
    return {};
}
}

std::expected<image, error> image::parse(std::span<const std::byte> file) {
    if (file.size() < 52) return std::unexpected(error::truncated);
    if (integer(file, 0) != 0x464c457f || integer(file, 4, 1) != 1 || integer(file, 5, 1) != 1
        || integer(file, 6, 1) != 1 || integer(file, 7, 1) != 0 || integer(file, 8, 1) != 0
        || integer(file, 16, 2) != 2 || integer(file, 18, 2) != 243 || integer(file, 20) != 1
        || integer(file, 40, 2) != 52) return std::unexpected(error::header);
    if (integer(file, 36) != 0) return std::unexpected(error::unsupported_profile);
    const auto shoff = integer(file, 32), shnum = integer(file, 48, 2), shstr = integer(file, 50, 2);
    if ((bool(shoff) != bool(shnum)) || shnum >= 0xff00 || shstr == 0xffff || (shstr && shstr >= shnum))
        return std::unexpected(error::header);
    if (shnum && (shoff < 52 || integer(file, 46, 2) != 40
        || !fits(file.size(), shoff, std::size_t{shnum} * 40))) return std::unexpected(error::header);
    const auto entry = integer(file, 24), phoff = integer(file, 28), phnum = integer(file, 44, 2);
    if (!phnum || phnum == 0xffff || integer(file, 42, 2) != 32 || phoff < 52)
        return std::unexpected(error::header);
    if (!fits(file.size(), phoff, std::size_t{phnum} * 32)) return std::unexpected(error::truncated);
    std::vector<segment> segments;
    std::optional<std::span<const std::byte>> profile;
    for (unsigned index = 0; index < phnum; ++index) {
        const auto ph = file.subspan(std::size_t{phoff} + index * 32, 32);
        const auto type = integer(ph, 0), offset = integer(ph, 4), address = integer(ph, 8);
        const auto file_size = integer(ph, 16), memory_size = integer(ph, 20), flags = integer(ph, 24), align = integer(ph, 28);
        if (type == 0) continue;
        if (!fits(file.size(), offset, file_size)) return std::unexpected(error::truncated);
        if (type == pt_attributes) {
            if (profile) return std::unexpected(error::attributes);
            profile = file.subspan(offset, file_size);
        } else if (type == pt_load) {
            if (file_size > memory_size || memory_size > address_space - address
                || integer(ph, 12) != address || !flags || (flags & ~7u)) return std::unexpected(error::segment);
            if (align > 1 && (!std::has_single_bit(align) || address % align != offset % align))
                return std::unexpected(error::alignment);
            if (memory_size) segments.push_back({physical_address{address}, offset, file_size, memory_size,
                {bool(flags & 4), bool(flags & 2), bool(flags & 1)}});
        } else if (type == 0x6474e551) {
            if (flags & 1) return std::unexpected(error::unsupported_profile); // Non-executable guest stack.
        } else if (type != 4 && type != 6) return std::unexpected(error::unsupported_segment);
    }
    if (!profile) {
        if (!shnum) return std::unexpected(error::attributes);
        for (unsigned index = 0; index < shnum; ++index) {
            const auto sh = file.subspan(std::size_t{shoff} + index * 40, 40);
            if (integer(sh, 4) != sht_attributes) continue;
            const auto start = integer(sh, 16), size = integer(sh, 20);
            if (profile || !fits(file.size(), start, size)) return std::unexpected(error::attributes);
            profile = file.subspan(start, size);
        }
    }
    if (!profile) return std::unexpected(error::attributes);
    if (const auto checked = attributes(*profile); !checked) return std::unexpected(checked.error());
    std::ranges::sort(segments, {}, &segment::address);
    std::uint64_t end = 0;
    bool entry_found = false;
    for (const auto& s : segments) {
        if (s.address.value() < end) return std::unexpected(error::overlap);
        end = std::uint64_t{s.address.value()} + s.memory_size;
        if (s.permissions.execute && entry >= s.address.value() && entry - s.address.value() <= s.file_size
            && instruction::scalar_bytes <= s.file_size - (entry - s.address.value())) entry_found = true;
    }
    if (!entry_found || entry % instruction::alignment_bytes) return std::unexpected(error::entry);
    return image{instruction_address{entry}, std::move(segments), file};
}

std::expected<void, error> image::load(const memory::physical_map& map, load_bindings memory) const {
    struct action { std::span<std::byte> destination; std::span<const std::byte> source; };
    std::vector<action> actions;
    for (const auto& s : segments_) {
        std::optional<memory::routed_access> route;
        for (const auto [required, kind] : std::array{
            std::pair{s.permissions.read, memory::access::read}, std::pair{s.permissions.write, memory::access::write},
            std::pair{s.permissions.execute, memory::access::execute}}) {
            if (!required) continue;
            const auto resolved = map.resolve(s.address, s.memory_size, kind);
            if (!resolved) return std::unexpected(error::mapping);
            route = *resolved;
        }
        const auto destination = std::visit([&](const auto& r) -> std::optional<std::span<std::byte>> {
            using T = std::remove_cvref_t<decltype(r)>;
            if constexpr (std::same_as<T, memory::system_slice>) return memory.system.range(r.address, r.size);
            else {
                const auto bytes = std::same_as<T, memory::program_slice> ? memory.program : memory.scratchpad;
                if (!fits(bytes.size(), r.offset.value(), r.size)) return std::nullopt;
                return bytes.subspan(r.offset.value(), r.size);
            }
        }, *route);
        if (!destination) return std::unexpected(error::backing);
        actions.push_back({*destination, std::span{bytes_}.subspan(s.file_offset, s.file_size)});
    }
    // All validation and allocation precedes initialization, including read-only program storage.
    for (auto& a : actions) {
        std::ranges::copy(a.source, a.destination.begin());
        std::ranges::fill(a.destination.subspan(a.source.size()), std::byte{});
    }
    return {};
}

} // namespace holon_npu::semantic::elf
