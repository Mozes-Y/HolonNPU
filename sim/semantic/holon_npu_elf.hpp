#pragma once

#include "holon_npu_memory.hpp"

namespace holon_npu::semantic::elf {

enum class error {
    truncated, header, unsupported_profile, unsupported_segment, attributes,
    segment, alignment, overlap, entry, mapping, backing
};
struct segment {
    physical_address address{};
    std::uint32_t file_offset{}, file_size{}, memory_size{};
    memory::permissions permissions{};
};
struct load_bindings {
    std::span<std::byte> program, scratchpad;
    system_memory_view system;
};

// Owning validated bytes/metadata; neither spans into the input nor execution state.
class image {
public:
    [[nodiscard]] static std::expected<image, error> parse(std::span<const std::byte> file);
    [[nodiscard]] instruction_address entry() const { return entry_; }
    [[nodiscard]] std::span<const segment> segments() const { return segments_; }
    // Initialization only, with execution quiescent. All destinations preflighted.
    [[nodiscard]] std::expected<void, error> load(const memory::physical_map& map, load_bindings memory) const;
private:
    image(instruction_address entry, std::vector<segment> segments, std::span<const std::byte> bytes)
        : entry_(entry), segments_(std::move(segments)), bytes_(bytes.begin(), bytes.end()) {}
    instruction_address entry_{};
    std::vector<segment> segments_;
    std::vector<std::byte> bytes_;
};

} // namespace holon_npu::semantic::elf
