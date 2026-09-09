#include "holon_npu_semantic.hpp"

#include <cfenv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace holon_npu::semantic {
namespace {
using namespace instruction;
unsigned width(npu_type type) {
    switch (type) {
    case npu_type::i8: case npu_type::u8: return 1;
    case npu_type::i16: case npu_type::u16: return 2;
    default: return 4;
    }
}
bool is_signed(npu_type type) { return type == npu_type::i8 || type == npu_type::i16 || type == npu_type::i32; }
std::uint32_t mask(npu_type type) { return 0xffffffffu >> (32 - width(type) * 8); }
std::int64_t integer(std::uint32_t value, npu_type type) {
    value &= mask(type);
    const auto sign = 1u << (width(type) * 8 - 1);
    return is_signed(type) && (value & sign) ? std::int64_t{value} - (std::int64_t{1} << (width(type) * 8)) : value;
}
std::uint32_t read(std::span<const std::byte> bytes, std::size_t offset, unsigned size) {
    std::uint32_t result{};
    for (unsigned i = 0; i < size; ++i) result |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return result;
}
void write(std::span<std::byte> bytes, std::size_t offset, unsigned size, std::uint32_t value) {
    for (unsigned i = 0; i < size; ++i) bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}
template<class T> T arg(const npu_instruction& inst, npu_role role) { return std::get<T>(*inst.find(role)); }
std::uint32_t fp_bits(float value) { return std::isnan(value) ? 0x7fc00000u : std::bit_cast<std::uint32_t>(value); }
float fp(std::uint32_t value) { return std::bit_cast<float>(value); }

// No guest FP flags are exposed. Restore the caller's complete FP environment.
class numeric_scope {
public:
    numeric_scope() {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        if (std::fegetenv(&saved_) || std::fesetenv(FE_DFL_ENV)) throw std::runtime_error("binary32 environment unavailable");
    }
    ~numeric_scope() { std::fesetenv(&saved_); }
    numeric_scope(const numeric_scope&) = delete;
    numeric_scope& operator=(const numeric_scope&) = delete;
    void rounding(rounding_mode mode) {
        constexpr std::array modes{FE_TONEAREST, FE_TOWARDZERO, FE_DOWNWARD, FE_UPWARD};
        if (std::fesetround(modes[std::to_underlying(mode)])) throw std::runtime_error("binary32 rounding unavailable");
    }
private:
    std::fenv_t saved_{};
};
std::uint32_t minimum(std::uint32_t a, std::uint32_t b, npu_type type, bool maximum) {
    if (type != npu_type::f32) return (maximum ? integer(a, type) > integer(b, type) : integer(a, type) < integer(b, type)) ? a : b;
    if (std::isnan(fp(a))) return fp_bits(fp(b));
    if (std::isnan(fp(b))) return fp_bits(fp(a));
    if (fp(a) == 0 && fp(b) == 0) return maximum ? a & b : a | b;
    return (maximum ? fp(a) > fp(b) : fp(a) < fp(b)) ? a : b;
}
std::uint32_t arithmetic(npu_opcode op, std::uint32_t a, std::uint32_t b, std::uint32_t c, npu_type type) {
    using enum npu_opcode;
    if (op == VMIN || op == VMAX) return minimum(a, b, type, op == VMAX);
    if (type == npu_type::f32) {
        switch (op) {
        case VADD: return fp_bits(fp(a) + fp(b));
        case VSUB: return fp_bits(fp(a) - fp(b));
        case VMUL: return fp_bits(fp(a) * fp(b));
        case VDIV: return fp_bits(fp(a) / fp(b));
        case VSQRT: return fp_bits(std::sqrt(fp(a)));
        case VFMA: return fp_bits(std::fma(fp(a), fp(b), fp(c)));
        default: std::unreachable();
        }
    }
    const auto bits = width(type) * 8, shift = b & (bits - 1);
    switch (op) {
    case VADD: return a + b;
    case VSUB: return a - b;
    case VMUL: return a * b;
    case VMULH:
        if (is_signed(type)) return static_cast<std::uint32_t>((integer(a, type) * integer(b, type)) >> bits);
        return static_cast<std::uint32_t>((std::uint64_t{a} * b) >> bits);
    case VAND: return a & b;
    case VOR: return a | b;
    case VXOR: return a ^ b;
    case VSHL: return a << shift;
    case VSHR: return a >> shift;
    case VASHR: return static_cast<std::uint32_t>(integer(a, type) >> shift);
    default: std::unreachable();
    }
}
std::uint32_t convert(std::uint32_t bits, npu_type source, npu_type destination, rounding_mode mode, numeric_scope& scope) {
    scope.rounding(mode);
    const double value = source == npu_type::f32 ? static_cast<double>(fp(bits)) : static_cast<double>(integer(bits, source));
    if (destination == npu_type::f32) return fp_bits(static_cast<float>(value));
    const auto size = width(destination) * 8;
    const double low = is_signed(destination) ? -std::ldexp(1.0, static_cast<int>(size - 1)) : 0;
    const double high = is_signed(destination) ? std::ldexp(1.0, static_cast<int>(size - 1)) - 1 : std::ldexp(1.0, static_cast<int>(size)) - 1;
    const double rounded = std::isnan(value) ? 0 : std::clamp(std::nearbyint(value), low, high);
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(rounded));
}
}

unsigned program_machine::vector_capacity(const instruction::npu_instruction& inst) const {
    using namespace instruction;
    using enum npu_role;
    auto bytes = inst.find(type) ? width(arg<npu_type>(inst, type)) : 1u;
    if (inst.find(result_type)) bytes = std::max(bytes, width(arg<npu_type>(inst, result_type)));
    if (inst.find(indices)) bytes = std::max(bytes, 4u);
    return config_.vector_bytes / bytes;
}

bool program_machine::tile_shape_fits(std::uint32_t rows, std::uint32_t cols, instruction::npu_type type) const {
    return rows <= config_.matrix_rows && cols <= config_.matrix_cols
        && std::uint64_t{rows} * cols * width(type) <= config_.tile_bytes;
}

npu_footprint program_machine::describe_npu(const instruction::npu_instruction& inst) const {
    using namespace instruction;
    using enum npu_opcode;
    using enum npu_role;
    npu_footprint out;
    const auto op = inst.pattern.opcode;
    out.element_bytes = inst.find(type) ? width(arg<npu_type>(inst, type)) : 1;
    if (inst.find(vl)) {
        out.lanes = hart_.reg(arg<scalar_register>(inst, vl));
        // Invalid shapes perform no lane work; completion still owns precise traps.
        if (out.lanes > vector_capacity(inst)) return {};
        for (unsigned i = 0; i < out.lanes; ++i)
            if (!inst.find(pg) || predicates_[arg<predicate_register>(inst, pg).value()][i]) ++out.active_lanes;
    }
    if (op == VLD || op == VLDS || op == VGATHER) out.local_read_bytes = std::uint64_t{out.active_lanes} * out.element_bytes;
    if (op == VST || op == VSTS || op == VSCATTER) out.local_write_bytes = std::uint64_t{out.active_lanes} * out.element_bytes;
    if (op == PLD) out.local_read_bytes = (std::uint64_t{out.lanes} + 7) / 8;
    if (op == PST) out.local_write_bytes = (std::uint64_t{out.lanes} + 7) / 8;
    if (op == MLOAD || op == MSTORE) {
        const auto& view = views_[arg<instruction::tile_view>(inst, npu_role::view).value()];
        if (view.valid) {
            out.matrix_m = view.rows; out.matrix_n = view.cols; out.element_bytes = width(view.type);
            const auto bytes = std::uint64_t{view.rows} * view.cols * out.element_bytes;
            if (op == MLOAD) out.local_read_bytes = bytes; else out.local_write_bytes = bytes;
        }
    }
    if (op == MDOT || op == MMACC) {
        const auto& a = tiles_[arg<tile_register>(inst, ta).value()];
        const auto& b = tiles_[arg<tile_register>(inst, tb).value()];
        if (a.valid && b.valid) { out.matrix_m = a.rows; out.matrix_n = b.cols; out.matrix_k = a.cols; }
    }
    if (op == MCLEAR) {
        out.matrix_m = hart_.reg(arg<scalar_register>(inst, rows));
        out.matrix_n = hart_.reg(arg<scalar_register>(inst, cols));
        if (!tile_shape_fits(out.matrix_m, out.matrix_n, arg<npu_type>(inst, type))) return {};
    }
    return out;
}

std::expected<std::optional<scalar::register_write>, execution_fault> program_machine::execute_npu(const instruction::npu_instruction& inst) {
    using namespace instruction;
    using enum npu_opcode;
    using enum npu_role;
    numeric_scope numeric;
    const auto opcode = inst.pattern.opcode;
    const auto raw = std::get<holon_word>(*frame_).bits;
    const auto invalid = std::unexpected(execution_fault{std::to_underlying(npu_trap::invalid_operand), static_cast<std::uint32_t>(raw)});
    const auto reg = [&](npu_role role) { return hart_.reg(arg<scalar_register>(inst, role)); };
    const auto result = [&](std::uint32_t value) -> std::optional<scalar::register_write> { return scalar::register_write{arg<scalar_register>(inst, rd), value}; };
    const auto ty = inst.find(type) ? arg<npu_type>(inst, type) : npu_type::u8;
    const auto bytes = width(ty);
    if (opcode == STOP) { stop_ = reg(status); return std::nullopt; }
    if (opcode == CAPS) {
        const std::array values{config_.vector_bytes, config_.matrix_rows, config_.matrix_cols, config_.tile_bytes};
        return result(values[std::to_underlying(arg<resource_capacity>(inst, selector))]);
    }
    if (opcode == VSETL) return result(std::min(reg(avl), config_.vector_bytes / std::max(bytes, width(arg<npu_type>(inst, peer_type)))));

    if (opcode == MVIEW) {
        if (!tile_shape_fits(reg(rows), reg(cols), ty)) return invalid;
        views_[arg<instruction::tile_view>(inst, view).value()] = {true, reg(base), reg(rows), reg(cols),
            std::bit_cast<std::int32_t>(reg(row_stride)), std::bit_cast<std::int32_t>(reg(col_stride)), ty};
        return std::nullopt;
    }
    if (opcode == MCLEAR) {
        if (!tile_shape_fits(reg(rows), reg(cols), ty)) return invalid;
        tiles_[arg<tile_register>(inst, td).value()] = {true, reg(rows), reg(cols), ty, std::vector<std::uint32_t>(std::size_t{reg(rows)} * reg(cols))};
        return std::nullopt;
    }
    if (opcode == MLOAD || opcode == MSTORE) {
        const bool store = opcode == MSTORE;
        const auto& v = views_[arg<instruction::tile_view>(inst, view).value()];
        const auto index = arg<tile_register>(inst, store ? ts : td).value();
        const auto& old = tiles_[index];
        if (!v.valid || (store && (!old.valid || old.rows != v.rows || old.cols != v.cols || old.type != v.type))) return invalid;
        const auto size = width(v.type);
        std::vector<local_address> addresses;
        for (std::uint32_t r = 0; r < v.rows; ++r) for (std::uint32_t c = 0; c < v.cols; ++c) {
            const auto address = local_range(std::int64_t{v.base} + std::int64_t{r} * v.row_stride + std::int64_t{c} * v.col_stride, size, store, size);
            if (!address) return std::unexpected(address.error());
            addresses.push_back(*address);
        }
        tile loaded{true, v.rows, v.cols, v.type, {}};
        for (std::size_t i = 0; i < addresses.size(); ++i) {
            if (store) write(scratchpad_, addresses[i].value(), size, old.elements[i]);
            else loaded.elements.push_back(read(scratchpad_, addresses[i].value(), size));
        }
        if (!store) tiles_[index] = std::move(loaded);
        return std::nullopt;
    }
    if (opcode == MDOT || opcode == MMACC) {
        const auto& a = tiles_[arg<tile_register>(inst, ta).value()];
        const auto& b = tiles_[arg<tile_register>(inst, tb).value()];
        const auto index = arg<tile_register>(inst, td).value();
        const auto& old = tiles_[index];
        const bool floating = ty == npu_type::f32, accumulate = opcode == MMACC;
        if (!a.valid || !b.valid || a.cols != b.rows || !tile_shape_fits(a.rows, b.cols, ty)
            || (a.type == npu_type::f32) != floating || (b.type == npu_type::f32) != floating
            || (accumulate && (!old.valid || old.rows != a.rows || old.cols != b.cols || old.type != ty))) return invalid;
        tile out{true, a.rows, b.cols, ty, std::vector<std::uint32_t>(std::size_t{a.rows} * b.cols)};
        for (std::uint32_t r = 0; r < a.rows; ++r) for (std::uint32_t c = 0; c < b.cols; ++c) {
            const auto i = std::size_t{r} * b.cols + c;
            auto sum = accumulate ? old.elements[i] : 0u;
            for (std::uint32_t k = 0; k < a.cols; ++k) {
                const auto av = a.elements[std::size_t{r} * a.cols + k], bv = b.elements[std::size_t{k} * b.cols + c];
                if (floating) sum = fp_bits(std::fma(fp(av), fp(bv), fp(sum)));
                else sum += static_cast<std::uint32_t>(integer(av, a.type)) * static_cast<std::uint32_t>(integer(bv, b.type));
            }
            out.elements[i] = sum;
        }
        tiles_[index] = std::move(out);
        return std::nullopt;
    }

    const auto length = reg(vl);
    const auto output_type = opcode == VCONVERT ? arg<npu_type>(inst, result_type) : ty;
    if (length > vector_capacity(inst)) return invalid;
    const auto pred = [&](npu_role role, unsigned lane) { return predicates_[arg<predicate_register>(inst, role).value()][lane] != 0; };
    if (opcode == PTRUE || opcode == PWHILELT || opcode == PAND || opcode == POR || opcode == PXOR || opcode == PNOT || opcode == PLD) {
        std::vector<std::uint8_t> output(config_.vector_bytes);
        std::optional<local_address> address;
        if (opcode == PLD && length) {
            const auto checked = local_range(std::int64_t{reg(base)} + arg<displacement>(inst, offset).value(), (length + 7) / 8, false);
            if (!checked) return std::unexpected(checked.error());
            address = *checked;
        }
        for (unsigned i = 0; i < length; ++i) {
            switch (opcode) {
            case PTRUE: output[i] = 1; break;
            case PWHILELT: output[i] = std::uint64_t{reg(base)} + i < reg(end); break;
            case PAND: output[i] = pred(pa, i) && pred(pb, i); break;
            case POR: output[i] = pred(pa, i) || pred(pb, i); break;
            case PXOR: output[i] = pred(pa, i) != pred(pb, i); break;
            case PNOT: output[i] = !pred(pa, i); break;
            case PLD: output[i] = (std::to_integer<unsigned>(scratchpad_[address->value() + i / 8]) >> (i % 8)) & 1u; break;
            default: std::unreachable();
            }
        }
        predicates_[arg<predicate_register>(inst, pd).value()] = std::move(output);
        return std::nullopt;
    }
    if (opcode == PCOUNT || opcode == PFIRST) {
        std::uint32_t value = opcode == PCOUNT ? 0 : 0xffffffffu;
        for (unsigned i = 0; i < length; ++i) if (pred(pa, i)) {
            if (opcode == PCOUNT) ++value;
            else { value = i; break; }
        }
        return result(value);
    }
    if (opcode == PST) {
        if (!length) return std::nullopt;
        const auto address = local_range(std::int64_t{reg(base)} + arg<displacement>(inst, offset).value(), (length + 7) / 8, true);
        if (!address) return std::unexpected(address.error());
        std::vector<std::byte> payload((length + 7) / 8);
        for (unsigned i = 0; i < length; ++i) if (pred(pa, i)) payload[i / 8] |= static_cast<std::byte>(1u << (i % 8));
        std::ranges::copy(payload, scratchpad_.begin() + address->value());
        return std::nullopt;
    }
    const auto lane = [&](npu_role role, unsigned i, unsigned size) { return read(vectors_[arg<vector_register>(inst, role).value()], std::size_t{i} * size, size); };
    if (opcode == VEXTRACT) {
        if (reg(index) >= length) return invalid;
        return result(static_cast<std::uint32_t>(integer(lane(va, reg(index), bytes), ty)));
    }
    if (opcode == VREDSUM || opcode == VREDMIN || opcode == VREDMAX) {
        auto value = reg(seed);
        for (unsigned i = 0; i < length; ++i) if (pred(pg, i)) {
            const auto v = static_cast<std::uint32_t>(integer(lane(va, i, bytes), ty));
            const auto result_type = ty == npu_type::f32 ? ty : (is_signed(ty) ? npu_type::i32 : npu_type::u32);
            value = arithmetic(opcode == VREDSUM ? VADD : (opcode == VREDMIN ? VMIN : VMAX), value, v, 0, result_type);
        }
        return result(value);
    }
    const bool store = opcode == VST || opcode == VSTS || opcode == VSCATTER;
    const bool load = opcode == VLD || opcode == VLDS || opcode == VGATHER;
    std::vector<local_address> addresses(length);
    if (store || load) {
        for (unsigned i = 0; i < length; ++i) if (pred(pg, i)) {
            const auto displacement = arg<instruction::displacement>(inst, offset).value();
            std::int64_t index = std::int64_t{i} * bytes;
            if (inst.find(stride)) index = std::int64_t{i} * std::bit_cast<std::int32_t>(reg(stride));
            if (inst.find(indices)) index = std::int64_t{lane(indices, i, 4)} << arg<index_scale>(inst, scale).value();
            const auto address = local_range(std::int64_t{reg(base)} + displacement + index, bytes, store, bytes);
            if (!address) return std::unexpected(address.error());
            addresses[i] = *address;
        }
    }
    if (store) {
        for (unsigned i = 0; i < length; ++i) if (pred(pg, i)) write(scratchpad_, addresses[i].value(), bytes, lane(va, i, bytes));
        return std::nullopt;
    }
    const bool comparison = opcode == VCMPEQ || opcode == VCMPNE || opcode == VCMPLT || opcode == VCMPLE;
    std::vector<std::uint8_t> predicate_output(config_.vector_bytes);
    std::vector<std::byte> output;
    if (!comparison) output = vectors_[arg<vector_register>(inst, vd).value()];
    const auto out_bytes = width(output_type);
    for (unsigned i = 0; i < length; ++i) {
        if (!pred(pg, i)) {
            if (!comparison && arg<mask_policy>(inst, policy) == mask_policy::zero) write(output, std::size_t{i} * out_bytes, out_bytes, 0);
            continue;
        }
        const auto a = inst.find(va) ? lane(va, i, bytes) : 0;
        const auto b = inst.find(vb) ? lane(vb, i, bytes) : 0;
        std::uint32_t value{};
        if (comparison) {
            const double av = ty == npu_type::f32 ? fp(a) : static_cast<double>(integer(a, ty));
            const double bv = ty == npu_type::f32 ? fp(b) : static_cast<double>(integer(b, ty));
            predicate_output[i] = opcode == VCMPEQ ? av == bv : (opcode == VCMPNE ? av != bv : (opcode == VCMPLT ? av < bv : av <= bv));
            continue;
        }
        if (load) value = read(scratchpad_, addresses[i].value(), bytes);
        else if (opcode == VCONVERT) value = convert(a, ty, output_type, arg<rounding_mode>(inst, rounding), numeric);
        else if (opcode == VSELECT) value = pred(select, i) ? a : b;
        else if (opcode == VBROADCAST) value = reg(npu_role::value);
        else if (opcode == VPERMUTE) {
            const auto index = lane(indices, i, 4);
            value = index < length ? lane(va, index, bytes) : 0;
        } else value = arithmetic(opcode, a, b, inst.find(vc) ? lane(vc, i, bytes) : 0, ty);
        write(output, std::size_t{i} * out_bytes, out_bytes, value);
    }
    if (comparison) predicates_[arg<predicate_register>(inst, pd).value()] = std::move(predicate_output);
    else {
        std::fill(output.begin() + std::size_t{length} * out_bytes, output.end(), std::byte{});
        vectors_[arg<vector_register>(inst, vd).value()] = std::move(output);
    }
    return std::nullopt;
}

} // namespace holon_npu::semantic
