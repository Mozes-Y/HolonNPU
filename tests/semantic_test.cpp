#include "holon_npu_execution.hpp"
#include "holon_npu_runtime.hpp"

#include <iostream>
#include <random>
#include <source_location>
#include <stdexcept>

namespace {
using namespace holon_npu::semantic;
using namespace holon_npu::semantic::instruction;
using holon_npu::runtime::program_builder;
using enum npu_role;
using enum npu_opcode;
constexpr std::uint32_t code_base = 0x1000, spm_base = 0x10000, ram_base = 0x80000000;
scalar_register x(unsigned i) { return scalar_register{static_cast<std::uint8_t>(i)}; }
vector_register v(unsigned i) { return vector_register{static_cast<std::uint8_t>(i)}; }
predicate_register p(unsigned i) { return predicate_register{static_cast<std::uint8_t>(i)}; }
tile_register t(unsigned i) { return tile_register{static_cast<std::uint8_t>(i)}; }
tile_view view_id(unsigned i) { return tile_view{static_cast<std::uint8_t>(i)}; }
void require(bool ok, std::string_view message, std::source_location at = std::source_location::current()) {
    if (!ok) throw std::runtime_error(std::string{message} + " at line " + std::to_string(at.line()));
}

struct fixture {
    std::vector<std::byte> ram = std::vector<std::byte>(65536);
    program_builder code;
    unsigned capacity;
    explicit fixture(unsigned vector_capacity = 64) : capacity(vector_capacity) {}
    static memory::physical_map map() {
        return *memory::physical_map::create(std::array{
            memory::region{physical_address{code_base}, 32768, memory::storage::program, {true, false, true}},
            memory::region{physical_address{spm_base}, 65536, memory::storage::scratchpad, {true, true, false}},
            memory::region{physical_address{ram_base}, 65536, memory::storage::system, {true, true, false}}});
    }
    void put(unsigned at, std::uint32_t value, unsigned bytes = 4) {
        for (unsigned i = 0; i < bytes; ++i) ram[at + i] = static_cast<std::byte>(value >> (8 * i));
    }
    std::uint32_t get(unsigned at, unsigned bytes = 4) const {
        std::uint32_t value{};
        for (unsigned i = 0; i < bytes; ++i) value |= std::to_integer<std::uint32_t>(ram[at + i]) << (8 * i);
        return value;
    }
    void dma(bool load) {
        code.li(x(1), load ? spm_base : ram_base).li(x(2), load ? ram_base : spm_base).li(x(3), 4096)
            .npu(load ? DLOAD : DSTORE, {{dst,x(1)}, {src,x(2)}, {npu_role::count,x(3)}});
    }
    void start(unsigned length) {
        dma(true);
        code.li(x(4), length).npu(PTRUE, {{pd,p(7)}, {vl,x(4)}});
    }
    void load(unsigned reg, unsigned at, npu_type dtype, mask_policy masking = mask_policy::zero) {
        code.li(x(5), spm_base + at).npu(VLD, {{vd,v(reg)}, {base,x(5)}, {pg,p(7)}, {vl,x(4)},
            {type,dtype}, {offset,displacement{0}}, {policy,masking}});
    }
    void store(unsigned reg, unsigned at, npu_type dtype) {
        code.li(x(5), spm_base + at).npu(VST, {{va,v(reg)}, {base,x(5)}, {pg,p(7)}, {vl,x(4)},
            {type,dtype}, {offset,displacement{0}}});
    }
    void predicate_store(unsigned reg, unsigned at) {
        code.li(x(5), spm_base + at).npu(PST, {{pa,p(reg)}, {base,x(5)}, {vl,x(4)}, {offset,displacement{0}}});
    }
    void scalar_store(unsigned reg, unsigned at) {
        code.li(x(5), spm_base + at);
        code.emit(scalar_word{0x2023u | (5u << 15) | (reg << 20)});
    }
    void run() {
        dma(false); code.npu(STOP, {{status,x(0)}});
        program_machine machine(map(), {.vector_bytes = capacity});
        require(machine.boot(code.bytes(), physical_address{code_base}, instruction_address{code_base}).has_value(), "boot test program");
        const auto report = run_program(machine, {system_address{ram_base}, ram}, 10000);
        require(report && report->reason == run_reason::stopped && !report->traps && !report->status, "guest program runs without trap");
        require(machine.hart().pc().value() == code_base + code.offset(), "precise mixed-width PC");
    }
};

std::uint32_t lane_mask(unsigned bytes) { return 0xffffffffu >> (32 - bytes * 8); }
std::int64_t signed_value(std::uint32_t a, unsigned bytes) {
    const auto modulus = std::int64_t{1} << (bytes * 8);
    return a & (1u << (bytes * 8 - 1)) ? std::int64_t{a} - modulus : a;
}

void integer_arithmetic() {
    constexpr std::uint32_t seed_value = 0x53454d49;
    std::mt19937 random(seed_value);
    constexpr std::array ops{VADD, VSUB, VMUL, VMULH, VMIN, VMAX, VAND, VOR, VXOR, VSHL, VSHR, VASHR};
    std::size_t cases{};
    for (unsigned capacity : {16u, 64u, 128u}) for (unsigned dtype = 0; dtype < 6; ++dtype) {
        const unsigned bytes = 1u << (dtype / 2), bits = bytes * 8;
        const bool sign = dtype % 2 == 0;
        for (auto op : ops) {
            if (op == VASHR && !sign) continue;
            for (unsigned trial = 0; trial < 4; ++trial) {
                const unsigned length = trial == 0 ? 0 : (trial == 1 ? capacity / bytes : 1 + random() % (capacity / bytes));
                fixture f(capacity);
                std::vector<std::uint32_t> expected(length);
                for (unsigned i = 0; i < length; ++i) {
                    const auto a = (i == 0 ? 1u << (bits - 1) : static_cast<std::uint32_t>(random())) & lane_mask(bytes);
                    const auto b = (i == 0 ? lane_mask(bytes) : static_cast<std::uint32_t>(random())) & lane_mask(bytes);
                    f.put(i * bytes, a, bytes); f.put(256 + i * bytes, b, bytes);
                    const auto av = sign ? signed_value(a, bytes) : std::int64_t{a};
                    const auto bv = sign ? signed_value(b, bytes) : std::int64_t{b};
                    std::uint32_t result{};
                    switch (op) {
                    case VADD: result = a + b; break;
                    case VSUB: result = a - b; break;
                    case VMUL: result = a * b; break;
                    case VMULH:
                        result = sign ? static_cast<std::uint32_t>((av * bv) >> bits)
                                      : static_cast<std::uint32_t>((std::uint64_t{a} * b) >> bits); break;
                    case VMIN: result = av < bv ? a : b; break;
                    case VMAX: result = av > bv ? a : b; break;
                    case VAND: result = a & b; break;
                    case VOR: result = a | b; break;
                    case VXOR: result = a ^ b; break;
                    case VSHL: result = a << (b % bits); break;
                    case VSHR: result = a >> (b % bits); break;
                    case VASHR: result = static_cast<std::uint32_t>(av >> (b % bits)); break;
                    default: throw std::logic_error("unhandled scoreboard operation");
                    }
                    expected[i] = result & lane_mask(bytes);
                }
                f.start(length); const auto ty = static_cast<npu_type>(dtype);
                f.load(1, 0, ty); f.load(2, 256, ty);
                f.code.npu(op, {{vd,v(1)}, {va,v(1)}, {vb,v(2)}, {pg,p(7)}, {vl,x(4)}, {type,ty}, {policy,mask_policy::merge}});
                f.store(1, 512, ty); f.run();
                for (unsigned i = 0; i < length; ++i) require(f.get(512 + i * bytes, bytes) == expected[i], "integer random/edge/source-alias scoreboard");
                ++cases;
            }
        }
    }
    std::cout << "Integer: " << cases << " programs, six types, three capacities, seed=" << seed_value << '\n';
}

void predicates_and_selection() {
    fixture f;
    f.put(0, 0x155, 2);
    for (unsigned i = 0; i < 10; ++i) { f.put(64 + 4 * i, 100 + i); f.put(128 + 4 * i, 200 + i); }
    f.start(10); f.load(1, 64, npu_type::i32); f.load(2, 128, npu_type::i32);
    f.code.li(x(5), spm_base).npu(PLD, {{pd,p(1)}, {base,x(5)}, {vl,x(4)}, {offset,displacement{0}}})
        .li(x(6), 0xfffffffc).li(x(8), 0xffffffff)
        .npu(PWHILELT, {{pd,p(2)}, {base,x(6)}, {end,x(8)}, {vl,x(4)}});
    f.predicate_store(2, 512);
    f.code.npu(PAND, {{pd,p(3)}, {pa,p(1)}, {pb,p(2)}, {vl,x(4)}}); f.predicate_store(3, 514);
    f.code.npu(POR, {{pd,p(3)}, {pa,p(1)}, {pb,p(2)}, {vl,x(4)}}); f.predicate_store(3, 516);
    f.code.npu(PXOR, {{pd,p(3)}, {pa,p(1)}, {pb,p(2)}, {vl,x(4)}}); f.predicate_store(3, 518);
    f.code.npu(PNOT, {{pd,p(1)}, {pa,p(1)}, {vl,x(4)}}); f.predicate_store(1, 520);
    f.code.npu(PCOUNT, {{rd,x(10)}, {pa,p(1)}, {vl,x(4)}}); f.scalar_store(10, 524);
    f.code.npu(PFIRST, {{rd,x(10)}, {pa,p(1)}, {vl,x(4)}}); f.scalar_store(10, 528);
    f.code.npu(VSELECT, {{vd,v(1)}, {va,v(1)}, {vb,v(2)}, {select,p(1)}, {pg,p(7)}, {vl,x(4)}, {type,npu_type::i32}, {policy,mask_policy::zero}});
    f.store(1, 640, npu_type::i32);
    f.code.li(x(4), 0).npu(PFIRST, {{rd,x(10)}, {pa,p(1)}, {vl,x(4)}}); f.scalar_store(10, 532);
    f.code.npu(PCOUNT, {{rd,x(10)}, {pa,p(1)}, {vl,x(4)}}); f.scalar_store(10, 536);
    f.run();
    for (auto [at, expected] : std::array<std::pair<unsigned, unsigned>, 5>{{{512,7}, {514,5}, {516,0x157}, {518,0x152}, {520,0x2aa}}})
        require(f.get(at, 2) == expected, "predicate packed bits, unsigned range overflow and alias snapshot");
    require(f.get(524) == 5 && f.get(528) == 1 && f.get(532) == 0xffffffff && f.get(536) == 0, "predicate query and empty identities");
    for (unsigned i = 0; i < 10; ++i) require(f.get(640 + 4 * i) == (i % 2 ? 100 : 200) + i, "independent select and execution predicates");
    std::cout << "Predicate: packed tails, logical operations, aliasing, queries and selection\n";
}

void compare_reduce_broadcast() {
    for (const auto ty : {npu_type::i32, npu_type::u32, npu_type::f32}) {
        fixture f;
        const bool floating = ty == npu_type::f32;
        const std::array a = floating ? std::array{0xbf800000u, 0u, 0x40000000u, 0x7fc00000u}
                                     : std::array{0xffffffffu, 0u, 2u, 0x80000000u};
        const std::array b = floating ? std::array{0u, 0u, 0x3f800000u, 0x7fc00000u}
                                     : std::array{0u, 0u, 1u, 0x80000000u};
        for (unsigned i = 0; i < 4; ++i) { f.put(4*i,a[i]); f.put(64+4*i,b[i]); }
        f.start(4); f.load(1,0,ty); f.load(2,64,ty);
        unsigned output = 512;
        for (auto op : {VCMPEQ, VCMPNE, VCMPLT, VCMPLE}) {
            f.code.npu(op, {{pd,p(3)}, {va,v(1)}, {vb,v(2)}, {pg,p(7)}, {vl,x(4)}, {type,ty}});
            f.predicate_store(3,output); output += 4;
        }
        f.code.li(x(6),floating ? 0x3f800000u : 5u).li(x(4),3);
        for (auto op : {VREDSUM, VREDMIN, VREDMAX}) {
            f.code.npu(op, {{rd,x(10)}, {va,v(1)}, {seed,x(6)}, {pg,p(7)}, {vl,x(4)}, {type,ty}});
            f.scalar_store(10,output); output += 4;
        }
        f.code.npu(VBROADCAST, {{vd,v(1)}, {value,x(6)}, {pg,p(7)}, {vl,x(4)}, {type,ty}, {policy,mask_policy::zero}})
            .li(x(8),2).npu(VEXTRACT, {{rd,x(10)}, {va,v(1)}, {index,x(8)}, {vl,x(4)}, {type,ty}});
        f.scalar_store(10,output); f.run();
        require(f.get(512,1) == (floating ? 2 : 10) && f.get(516,1) == (floating ? 13 : 5), "comparison equal/unequal including NaN");
        require(f.get(520,1) == (ty == npu_type::u32 ? 0 : 1) && f.get(524,1) == (floating ? 3 : (ty == npu_type::u32 ? 10 : 11)), "signed/unsigned/float ordering");
        const auto sum = floating ? 0x40000000u : 6u;
        const auto min = floating ? 0xbf800000u : (ty == npu_type::u32 ? 0u : 0xffffffffu);
        const auto max = floating ? 0x40000000u : (ty == npu_type::u32 ? 0xffffffffu : 5u);
        require(f.get(528)==sum && f.get(532)==min && f.get(536)==max, "seeded reduction ordering and wraparound");
        require(f.get(540)==(floating ? 0x3f800000u : 5u), "broadcast/extract scalar bits");
    }
    std::cout << "Compare/reduce: signed, unsigned, binary32, NaN and explicit scalar seeds\n";
}

void indexed_and_strided_memory() {
    fixture f;
    const std::array index_values{3u,0u,3u,1u};
    for (unsigned i=0;i<4;++i) {f.put(4*i,10+i);f.put(64+4*i,index_values[i]);}
    f.start(4); f.load(1,0,npu_type::u32); f.load(2,64,npu_type::u32);
    f.code.li(x(5),spm_base+12).li(x(6),0xfffffffcu)
        .npu(VLDS,{{vd,v(3)},{base,x(5)},{stride,x(6)},{pg,p(7)},{vl,x(4)},{type,npu_type::u32},{policy,mask_policy::zero},{offset,displacement{0}}});
    f.store(3,512,npu_type::u32);
    f.code.li(x(5),spm_base+540).npu(VSTS,{{va,v(1)},{base,x(5)},{stride,x(6)},{pg,p(7)},{vl,x(4)},{type,npu_type::u32},{offset,displacement{0}}})
        .li(x(5),spm_base).npu(VGATHER,{{vd,v(3)},{base,x(5)},{indices,v(2)},{pg,p(7)},{vl,x(4)},{type,npu_type::u32},
            {policy,mask_policy::zero},{scale,index_scale{2}},{offset,displacement{0}}});
    f.store(3,544,npu_type::u32);
    f.code.li(x(5),spm_base+560).npu(VSCATTER,{{va,v(1)},{base,x(5)},{indices,v(2)},{pg,p(7)},{vl,x(4)},{type,npu_type::u32},{scale,index_scale{2}},{offset,displacement{0}}})
        .npu(VPERMUTE,{{vd,v(1)},{va,v(1)},{indices,v(2)},{pg,p(7)},{vl,x(4)},{type,npu_type::u32},{policy,mask_policy::zero}});
    f.store(1,576,npu_type::u32); f.run();
    for(unsigned i=0;i<4;++i) {
        require(f.get(512+4*i)==13-i && f.get(528+4*i)==13-i,"negative stride load/store");
        require(f.get(544+4*i)==10+index_values[i] && f.get(576+4*i)==10+index_values[i],"gather/permutation snapshot under destination alias");
    }
    require(f.get(560)==11 && f.get(564)==13 && f.get(568)==0 && f.get(572)==12,"scatter repeated-address last-active-lane ordering");
    std::cout << "Memory: negative strides, scaled gather/scatter and alias-safe permutation\n";
}

void masks_and_tails() {
    for (const auto masking : {mask_policy::merge, mask_policy::zero}) {
        fixture f;
        f.put(256, 0x5, 1);
        for (unsigned i = 0; i < 8; ++i) { f.put(4*i,10+i); f.put(64+4*i,100+i); }
        f.start(8); f.load(1,0,npu_type::u32); f.load(2,64,npu_type::u32);
        f.code.li(x(4),3).li(x(5),spm_base+256)
            .npu(PLD,{{pd,p(3)},{base,x(5)},{vl,x(4)},{offset,displacement{0}}})
            .npu(VADD,{{vd,v(2)},{va,v(1)},{vb,v(1)},{pg,p(3)},{vl,x(4)},{type,npu_type::u32},{policy,masking}})
            .npu(VCMPEQ,{{pd,p(3)},{va,v(1)},{vb,v(1)},{pg,p(3)},{vl,x(4)},{type,npu_type::u32}});
        // Observe all bytes/bits, including lanes beyond the earlier active VL.
        f.code.li(x(4),8); f.predicate_store(3,520); f.store(2,512+32,npu_type::u32);
        f.run();
        require(f.get(520,1)==5,"comparison captures aliased execution predicate and clears tails");
        for (unsigned i=0;i<8;++i) {
            const auto expected=i==0 ? 20u : (i==2 ? 24u : (i==1 && masking==mask_policy::merge ? 101u : 0u));
            require(f.get(544+4*i)==expected,"inactive merge/zero is independent from unconditional tail clearing");
        }
    }
    fixture f;
    f.start(0);
    f.code.li(x(6),0xdeadbeef);
    unsigned address=512;
    for (const auto op : {VREDSUM,VREDMIN,VREDMAX}) {
        f.code.npu(op,{{rd,x(10)},{va,v(1)},{seed,x(6)},{pg,p(7)},{vl,x(4)},{type,npu_type::i8}});
        f.scalar_store(10,address); address+=4;
    }
    f.run();
    for(unsigned at=512;at<524;at+=4) require(f.get(at)==0xdeadbeef,"empty reduction preserves full scalar seed");
    std::cout << "Masks/tails: merge versus zero, predicate destination alias and empty reductions\n";
}

void matrix_views() {
    fixture f;
    for(unsigned i=0;i<6;++i) {f.put(i,static_cast<std::uint8_t>(static_cast<int>(i)-3),1);f.put(64+i,250+i,1);}
    f.start(0);
    const auto view = [&](unsigned id,unsigned at,unsigned rows,unsigned cols,std::int32_t rs,std::int32_t cs,npu_type ty) {
        f.code.li(x(10),spm_base+at).li(x(11),rows).li(x(12),cols).li(x(13),static_cast<std::uint32_t>(rs)).li(x(14),static_cast<std::uint32_t>(cs))
            .npu(MVIEW,{{npu_role::view,view_id(id)},{base,x(10)},{npu_role::rows,x(11)},{npu_role::cols,x(12)},
                {row_stride,x(13)},{col_stride,x(14)},{type,ty}});
    };
    view(0,3,2,3,-3,1,npu_type::i8); view(1,64,3,2,1,3,npu_type::u8);
    f.code.npu(MLOAD,{{td,t(0)},{npu_role::view,view_id(0)}}).npu(MLOAD,{{td,t(1)},{npu_role::view,view_id(1)}});
    // Redefining a view and its source GPRs must not change a captured tile.
    view(0,0,1,1,1,1,npu_type::u8);
    f.code.li(x(11),2).li(x(12),2).npu(MCLEAR,{{td,t(2)},{rows,x(11)},{cols,x(12)},{type,npu_type::i32}})
        .npu(MMACC,{{td,t(2)},{ta,t(0)},{tb,t(1)},{type,npu_type::i32}})
        .npu(MDOT,{{td,t(0)},{ta,t(0)},{tb,t(1)},{type,npu_type::i32}});
    view(2,512,2,2,8,4,npu_type::i32);view(3,528,2,2,8,4,npu_type::i32);
    f.code.npu(MSTORE,{{ts,t(0)},{npu_role::view,view_id(2)}}).npu(MSTORE,{{ts,t(2)},{npu_role::view,view_id(3)}});
    f.run();
    for(unsigned r=0;r<2;++r) for(unsigned c=0;c<2;++c) {
        std::int32_t expected{};
        for(unsigned k=0;k<3;++k) expected+=(static_cast<int>((1-r)*3+k)-3)*static_cast<int>(250+k+3*c);
        require(f.get(512+4*(r*2+c))==static_cast<std::uint32_t>(expected) && f.get(528+4*(r*2+c))==static_cast<std::uint32_t>(expected),
            "mixed signed matrix, transpose/negative views, captured operands and destination alias");
    }
    std::cout << "Matrix: typed views, mixed signedness, clear/accumulate/dot and captured tile independence\n";
}
} // namespace

int main() {
    try {
        integer_arithmetic(); predicates_and_selection(); compare_reduce_broadcast(); indexed_and_strided_memory(); masks_and_tails(); matrix_views();
    } catch(const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
    std::cout << "PASS: canonical semantic scoreboards through guest DMA writeback\n";
}
