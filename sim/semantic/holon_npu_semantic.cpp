#include "holon_npu_semantic.hpp"

#include <limits>
#include <stdexcept>

namespace holon_npu::semantic {
namespace {
std::uint32_t word(std::span<const std::byte> bytes) {
    std::uint32_t value{};
    for (unsigned i = 0; i < bytes.size(); ++i) value |= std::to_integer<std::uint32_t>(bytes[i]) << (8 * i);
    return value;
}
}

program_machine::program_machine(memory::physical_map map, machine_config config)
    : map_(std::move(map)), config_(config), program_(config.program_bytes), scratchpad_(config.scratchpad_bytes) {
    if (!config.vector_bytes || config.vector_bytes % 16 || !config.matrix_rows || !config.matrix_cols || !config.tile_bytes)
        throw std::invalid_argument("invalid Holon architectural capacity");
    for (auto& v : vectors_) v.resize(config.vector_bytes);
    for (auto& p : predicates_) p.resize(config.vector_bytes);
    reset();
}

void program_machine::reset() {
    hart_.reset();
    std::ranges::fill(program_, std::byte{});
    std::ranges::fill(scratchpad_, std::byte{});
    for (auto& v : vectors_) std::ranges::fill(v, std::byte{});
    for (auto& p : predicates_) std::ranges::fill(p, 0);
    views_ = {}; tiles_ = {};
    pending_.reset(); frame_.reset(); stop_.reset(); dma_local_.reset(); first_parcel_.reset();
    started_ = false;
}

std::expected<void, boot_error> program_machine::boot(const elf::image& image, system_memory_view system) {
    if (pending_) return std::unexpected(boot_error::operation_pending);
    std::vector<std::byte> program(program_.size()), scratchpad(scratchpad_.size());
    // ELF loading preflights ALL destinations, including environment-owned memory.
    if (!image.load(map_, {program, scratchpad, system})) return std::unexpected(boot_error::image);
    reset(); program_ = std::move(program); scratchpad_ = std::move(scratchpad);
    if (!hart_.start(image.entry())) std::unreachable();
    started_ = true;
    return {};
}

std::expected<void, boot_error> program_machine::boot(
    std::span<const std::byte> bytes, physical_address base, instruction_address entry) {
    if (pending_) return std::unexpected(boot_error::operation_pending);
    if (bytes.empty() || bytes.size() % 4 || entry.value() % 4 || entry.value() < base.value()
        || std::uint64_t{entry.value()} + 4 > std::uint64_t{base.value()} + bytes.size())
        return std::unexpected(boot_error::image);
    const auto route = map_.resolve(base, bytes.size(), memory::access::execute);
    const auto* local = route ? std::get_if<memory::program_slice>(&*route) : nullptr;
    if (!local || local->offset.value() > program_.size() || bytes.size() > program_.size() - local->offset.value())
        return std::unexpected(boot_error::mapping);
    const std::vector<std::byte> copy(bytes.begin(), bytes.end());
    const auto offset = local->offset.value();
    reset(); std::ranges::copy(copy, program_.begin() + offset);
    if (!hart_.start(entry)) std::unreachable();
    started_ = true;
    return {};
}

std::optional<pending_operation> program_machine::pending() const {
    return pending_ ? std::optional{pending_->event} : std::nullopt;
}

scalar::trap_taken program_machine::trap(execution_fault fault) {
    frame_.reset(); first_parcel_.reset(); dma_local_.reset();
    return hart_.enter_trap(fault.cause, fault.value);
}

std::expected<execution_event, api_error> program_machine::issue(operation request, purpose why) {
    if (next_token_ == std::numeric_limits<std::uint64_t>::max()) return std::unexpected(api_error::token_exhausted);
    pending_operation event{operation_token{next_token_++}, hart_.pc(), std::move(request)};
    pending_ = pending_context{event, why};
    return event;
}

std::expected<memory_request, execution_fault> program_machine::memory_operation(
    physical_address address, std::uint32_t size, memory::access access, std::span<const std::byte> payload) {
    const auto fault = execution_fault{access == memory::access::execute ? 1u : (access == memory::access::write ? 7u : 5u), address.value()};
    const auto route = map_.resolve(address, size, access);
    if (!route) return std::unexpected(fault);
    const auto storage = std::visit([&](const auto& r) -> std::optional<memory::storage> {
        using T = std::remove_cvref_t<decltype(r)>;
        if constexpr (std::same_as<T, memory::system_slice>) return memory::storage::system;
        else {
            const auto capacity = std::same_as<T, memory::program_slice> ? program_.size() : scratchpad_.size();
            if (r.offset.value() > capacity || r.size > capacity - r.offset.value()) return std::nullopt;
            return std::same_as<T, memory::program_slice> ? memory::storage::program : memory::storage::scratchpad;
        }
    }, *route);
    if (!storage) return std::unexpected(fault);
    return memory_request{address, size, access, *storage, {payload.begin(), payload.end()}};
}

std::expected<execution_event, api_error> program_machine::advance() {
    if (pending_) return std::unexpected(api_error::operation_pending);
    if (!started_) return std::unexpected(api_error::invalid_state);
    if (stop_) return stopped{*stop_};
    if (next_token_ == std::numeric_limits<std::uint64_t>::max()) return std::unexpected(api_error::token_exhausted);
    if (!frame_ && !first_parcel_) {
        const auto interrupt = hart_.poll_interrupt();
        if (!interrupt) return std::unexpected(api_error::invalid_state);
        if (*interrupt) return **interrupt;
        if (hart_.waiting()) return scalar::sleeping{};
    }
    if (frame_) {
        if (const auto* scalar = std::get_if<instruction::scalar_word>(&*frame_)) return issue_scalar(*scalar);
        return issue_npu(std::get<instruction::holon_word>(*frame_));
    }
    const auto second = first_parcel_.has_value();
    const auto address = std::uint64_t{hart_.pc().value()} + (second ? 4 : 0);
    if (address > 0xfffffffcull) return trap({1, static_cast<std::uint32_t>(address)});
    const auto request = memory_operation(physical_address{static_cast<std::uint32_t>(address)}, 4, memory::access::execute);
    if (!request) return trap(request.error());
    return issue(*request, second ? purpose::fetch_second : purpose::fetch_first);
}

std::expected<execution_event, api_error> program_machine::scalar_event(const scalar::hart_event& event) {
    return std::visit([&](const auto& e) -> std::expected<execution_event, api_error> {
        using T = std::remove_cvref_t<decltype(e)>;
        if constexpr (!std::same_as<T, scalar::pending_effect>) {
            frame_.reset(); return e;
        } else {
            return std::visit([&](const auto& r) -> std::expected<execution_event, api_error> {
                using R = std::remove_cvref_t<decltype(r)>;
                if constexpr (std::same_as<R, scalar::fence_request>) return issue(r, purpose::scalar);
                else {
                    const auto request = [&] {
                        if constexpr (std::same_as<R, scalar::load_request>)
                            return memory_operation(r.address, static_cast<unsigned>(r.width), memory::access::read);
                        else return memory_operation(r.address, static_cast<unsigned>(r.width), memory::access::write,
                            std::span{r.payload}.first(static_cast<unsigned>(r.width)));
                    }();
                    if (!request) return scalar_event(*hart_.complete(e.token, scalar::access_fault{}));
                    return issue(*request, purpose::scalar);
                }
            }, e.request);
        }
    }, event);
}

std::expected<execution_event, api_error> program_machine::issue_scalar(instruction::scalar_word word) {
    const auto result = hart_.issue(word);
    if (!result) return std::unexpected(api_error::invalid_state);
    return scalar_event(*result);
}

scalar::committed program_machine::retire_npu(std::optional<scalar::register_write> write) {
    hart_.write_register({write}); frame_.reset();
    return hart_.commit(instruction_address{hart_.pc().value() + 8u});
}

std::expected<local_address, execution_fault> program_machine::local_range(
    std::int64_t address, std::size_t size, bool write, unsigned alignment) const {
    const auto low = static_cast<std::uint32_t>(address);
    if (address % alignment) return std::unexpected(execution_fault{write ? 6u : 4u, low});
    if (address < 0 || std::uint64_t(address) > 0xffffffffull) return std::unexpected(execution_fault{write ? 7u : 5u, low});
    const auto route = map_.resolve(physical_address{low}, size, write ? memory::access::write : memory::access::read);
    const auto* local = route ? std::get_if<memory::scratchpad_slice>(&*route) : nullptr;
    if (!local || local->offset.value() > scratchpad_.size() || size > scratchpad_.size() - local->offset.value())
        return std::unexpected(execution_fault{write ? 7u : 5u, low});
    return local->offset;
}

std::expected<execution_event, api_error> program_machine::issue_npu(instruction::holon_word word) {
    const auto inst = instruction::decode_holon(word);
    if (!inst) return trap({2, static_cast<std::uint32_t>(word.bits)});
    using enum instruction::npu_opcode;
    const auto reg = [&](instruction::npu_role role) { return hart_.reg(std::get<instruction::scalar_register>(*inst->find(role))); };
    if (inst->pattern.opcode == DLOAD || inst->pattern.opcode == DSTORE) {
        const bool load = inst->pattern.opcode == DLOAD;
        const auto count = reg(instruction::npu_role::count);
        if (!count) return retire_npu();
        const auto local = local_range(reg(load ? instruction::npu_role::dst : instruction::npu_role::src), count, load);
        if (!local) return trap(local.error());
        const auto system = physical_address{reg(load ? instruction::npu_role::src : instruction::npu_role::dst)};
        const auto data = std::span{scratchpad_}.subspan(local->value(), count);
        const auto request = memory_operation(system, count, load ? memory::access::read : memory::access::write,
            load ? std::span<const std::byte>{} : std::span<const std::byte>{data});
        if (!request) return trap(request.error());
        if (request->storage != memory::storage::system) return trap({load ? 5u : 7u, system.value()});
        dma_local_ = *local;
        return issue(*request, load ? purpose::dma_load : purpose::dma_store);
    }
    return issue(npu_request{*inst, describe_npu(*inst)}, purpose::npu);
}

std::expected<execution_event, api_error> program_machine::complete(operation_token token, operation_result result) {
    if (!pending_) return std::unexpected(api_error::no_pending);
    if (token != pending_->event.token) return std::unexpected(api_error::wrong_token);
    const auto context = *pending_;
    const auto* request = std::get_if<memory_request>(&context.event.value);
    const bool external = request && request->storage == memory::storage::system;
    const bool read = external && request->access != memory::access::write;
    const auto* data = std::get_if<read_payload>(&result);
    const auto* failure = std::get_if<bus_fault>(&result);
    const bool failed = failure != nullptr;
    if (failed ? !external : (read ? (!data || data->bytes.size() != request->size) : !std::holds_alternative<operation_success>(result)))
        return std::unexpected(api_error::invalid_result);
    if (failed && failure->address && (failure->address->value() < request->address.value()
        || std::uint64_t{failure->address->value()} >= std::uint64_t{request->address.value()} + request->size))
        return std::unexpected(api_error::invalid_result);
    if (context.why == purpose::npu) {
        const auto& inst = std::get<npu_request>(context.event.value).instruction;
        const auto value = execute_npu(inst);
        pending_.reset();
        if (!value) return trap(value.error());
        const auto event = retire_npu(*value);
        if (stop_) return stopped{*stop_};
        return event;
    }
    std::vector<std::byte> payload;
    if (data) payload = data->bytes;
    if (request && !external) {
        const memory::bindings bindings{program_, scratchpad_, {}};
        if (request->access == memory::access::write) {
            if (!map_.write(request->address, request->payload, bindings)) std::unreachable();
        } else {
            payload.resize(request->size);
            if (!map_.read(request->address, payload, bindings, request->access)) std::unreachable();
        }
    }
    pending_.reset();
    if (context.why == purpose::scalar) {
        const auto live = hart_.pending_request();
        if (!live) std::unreachable();
        const scalar::external_result value = failed ? scalar::external_result{scalar::access_fault{failure->address}}
            : (request && request->access == memory::access::read ? scalar::external_result{scalar::load_data{payload}}
                : scalar::external_result{scalar::acknowledged{}});
        return scalar_event(*hart_.complete(live->token, value));
    }
    if (failed) return trap({request->access == memory::access::execute ? 1u : (request->access == memory::access::write ? 7u : 5u), failure->address.value_or(request->address).value()});
    if (context.why == purpose::dma_load || context.why == purpose::dma_store) {
        if (context.why == purpose::dma_load) std::ranges::copy(payload, scratchpad_.begin() + dma_local_->value());
        dma_local_.reset(); return retire_npu();
    }
    const auto parcel = word(payload);
    if (context.why == purpose::fetch_second) {
        frame_ = instruction::holon_word{*first_parcel_ | (std::uint64_t{parcel} << 32)};
        first_parcel_.reset();
    } else if ((parcel & 3) == 3) frame_ = instruction::scalar_word{parcel};
    else first_parcel_ = parcel;
    return progress_event{};
}

} // namespace holon_npu::semantic
