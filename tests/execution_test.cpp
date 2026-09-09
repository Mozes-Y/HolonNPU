#include "holon_npu_execution.hpp"
#include "holon_npu_runtime.hpp"

#include <cfenv>
#include <iostream>
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
memory::physical_map map() {
    return *memory::physical_map::create(std::array{
        memory::region{physical_address{code_base}, 0x4000, memory::storage::program, {true, false, true}},
        memory::region{physical_address{spm_base}, 0x10000, memory::storage::scratchpad, {true, true, false}},
        memory::region{physical_address{ram_base}, 0x10000, memory::storage::system, {true, true, true}}});
}
std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }
void put(std::span<std::byte> bytes, unsigned address, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[address + i] = static_cast<std::byte>(value >> (8 * i));
}
std::uint32_t get(std::span<const std::byte> bytes, unsigned address) {
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i) value |= std::to_integer<std::uint32_t>(bytes[address + i]) << (8 * i);
    return value;
}
void boot(program_machine& machine, const program_builder& program) {
    require(machine.boot(program.bytes(), physical_address{code_base}, instruction_address{code_base}).has_value(), "boot raw mixed image");
}
void transfer(program_builder& b, bool load, std::uint32_t local, std::uint32_t external, unsigned count) {
    b.li(x(1), load ? local : external).li(x(2), load ? external : local).li(x(3), count)
        .npu(load ? DLOAD : DSTORE, {{dst,x(1)}, {src,x(2)}, {npu_role::count,x(3)}});
}
void load_vector(program_builder& b, unsigned vector, unsigned address, npu_type dtype) {
    b.li(x(5), address).npu(VLD, {{vd,v(vector)}, {base,x(5)}, {pg,p(7)}, {vl,x(4)},
        {type,dtype}, {offset,displacement{0}}, {policy,mask_policy::zero}});
}
void store_vector(program_builder& b, unsigned vector, unsigned address, npu_type dtype) {
    b.li(x(5), address).npu(VST, {{va,v(vector)}, {base,x(5)}, {pg,p(7)}, {vl,x(4)},
        {type,dtype}, {offset,displacement{0}}});
}
void finish(program_builder& b) { b.npu(STOP, {{status,x(0)}}); }

void capabilities() {
    for (const auto capacity : {16u, 64u, 128u}) {
        const machine_config config{.vector_bytes=capacity, .matrix_rows=8, .matrix_cols=16, .tile_bytes=512};
        program_machine machine(map(), config);
        program_builder code;
        for (unsigned i=0; i<4; ++i)
            code.npu(CAPS, {{rd,x(8+i)}, {selector,static_cast<resource_capacity>(i)}});
        finish(code); boot(machine,code);
        const auto report=run_program(machine,{},100);
        require(report && report->reason==run_reason::stopped && !report->traps && report->retired==5,
            "CAPS executes and retires without system memory access");
        const std::array expected{capacity,8u,16u,512u};
        for (unsigned i=0;i<4;++i) require(machine.hart().reg(x(8+i))==expected[i],"CAPS reports configured architectural capacity");
    }
}

void integer_conversion_edges() {
    constexpr std::array modes{rounding_mode::rne,rounding_mode::rtz,rounding_mode::rdn,rounding_mode::rup};
    constexpr std::array input{16777217u,0xffffffffu,16777216u,0u};
    constexpr std::array expected{
        std::array{0x4b800000u,0x4f800000u,0x4b800000u,0u},
        std::array{0x4b800000u,0x4f7fffffu,0x4b800000u,0u},
        std::array{0x4b800000u,0x4f7fffffu,0x4b800000u,0u},
        std::array{0x4b800001u,0x4f800000u,0x4b800000u,0u},
    };
    for (unsigned mode=0;mode<modes.size();++mode) {
        std::vector<std::byte> ram(0x10000);
        for (unsigned i=0;i<4;++i) put(ram,4*i,input[i]);
        program_builder code;
        transfer(code,true,spm_base,ram_base,16);
        code.li(x(4),4).npu(PTRUE,{{pd,p(7)},{vl,x(4)}});
        load_vector(code,1,spm_base,npu_type::u32);
        code.npu(VCONVERT,{{vd,v(2)},{va,v(1)},{pg,p(7)},{vl,x(4)},{type,npu_type::u32},
            {result_type,npu_type::f32},{rounding,modes[mode]},{policy,mask_policy::zero}});
        store_vector(code,2,spm_base+32,npu_type::f32);
        transfer(code,false,spm_base+32,ram_base+32,16); finish(code);
        program_machine machine(map()); boot(machine,code);
        const auto report=run_program(machine,{system_address{ram_base},ram},1000);
        require(report && report->reason==run_reason::stopped && !report->traps,"integer to FP32 program");
        for (unsigned i=0;i<4;++i) require(get(ram,32+4*i)==expected[mode][i],"integer to FP32 exact rounding boundary");
    }
}

void vector_program() {
    for (const auto capacity : {16u, 64u, 128u}) {
        program_machine machine(map(), {.vector_bytes = capacity});
        std::vector<std::byte> ram(0x10000);
        const std::array a{-1.0f, 2.0f, 3.5f, -4.0f}, b{2.0f, -2.0f, 0.5f, 1.0f};
        for (unsigned i = 0; i < 4; ++i) { put(ram, i * 4, bits(a[i])); put(ram, 16 + i * 4, bits(b[i])); }
        program_builder code;
        transfer(code, true, spm_base, ram_base, 32);
        code.li(x(4),4).npu(PTRUE, {{pd,p(7)}, {vl,x(4)}});
        load_vector(code, 1, spm_base, npu_type::f32);
        load_vector(code, 2, spm_base + 16, npu_type::f32);
        code.npu(VADD, {{vd,v(1)}, {va,v(1)}, {vb,v(2)}, {pg,p(7)}, {vl,x(4)}, {type,npu_type::f32}, {policy,mask_policy::merge}});
        store_vector(code,1,spm_base+64,npu_type::f32);
        transfer(code,false,spm_base+64,ram_base+256,16); finish(code);
        boot(machine,code);
        const auto report = run_program(machine,{system_address{ram_base},ram},1000);
        require(report && report->reason == run_reason::stopped && !report->status && !report->traps, "self-hosted vector/DMA/STOP");
        for (unsigned i = 0; i < 4; ++i) require(get(ram,256+i*4) == bits(a[i]+b[i]), "independent vector result in system memory");
        require(machine.hart().pc().value() == code_base + code.offset(), "mixed-width PC at STOP continuation");
        const auto raw = machine.vector_bytes(v(1));
        require(std::ranges::all_of(raw.subspan(16), [](auto b){return b == std::byte{};}), "tail bytes clear");
    }
}
void matrix_view(program_builder& code, unsigned id, unsigned base_value, unsigned rows_value, unsigned cols_value, unsigned stride_value) {
    code.li(x(10),base_value).li(x(11),rows_value).li(x(12),cols_value).li(x(13),stride_value).li(x(14),4)
        .npu(MVIEW, {{view,view_id(id)}, {base,x(10)}, {rows,x(11)}, {cols,x(12)},
            {row_stride,x(13)}, {col_stride,x(14)}, {type,npu_type::f32}});
}
void matrix_program() {
    std::vector<std::byte> ram(0x10000);
    for (unsigned i = 0; i < 6; ++i) { put(ram,i*4,bits(float(i+1))); put(ram,64+i*4,bits(float(i+1))); }
    program_builder code;
    transfer(code,true,spm_base,ram_base,96);
    matrix_view(code,0,spm_base,2,3,12); matrix_view(code,1,spm_base+64,3,2,8); matrix_view(code,2,spm_base+128,2,2,8);
    code.npu(MLOAD,{{td,t(0)},{view,view_id(0)}}).npu(MLOAD,{{td,t(1)},{view,view_id(1)}})
        .npu(MDOT,{{td,t(2)},{ta,t(0)},{tb,t(1)},{type,npu_type::f32}})
        .npu(MMACC,{{td,t(2)},{ta,t(0)},{tb,t(1)},{type,npu_type::f32}})
        .npu(MSTORE,{{ts,t(2)},{view,view_id(2)}});
    transfer(code,false,spm_base+128,ram_base+256,16); finish(code);
    program_machine machine(map()); boot(machine,code);
    const auto report = run_program(machine,{system_address{ram_base},ram},1000);
    require(report && report->reason == run_reason::stopped && !report->traps, "matrix load/dot/accumulate/store program");
    const std::array expected{44.0f,56.0f,98.0f,128.0f};
    for (unsigned i=0;i<4;++i) require(get(ram,256+i*4)==bits(expected[i]),"independent matrix result");
}
void completion_and_boot() {
    program_builder code; code.li(x(4),42).npu(STOP,{{status,x(4)}});
    program_machine machine(map()); boot(machine,code);
    const auto first = machine.advance();
    require(first && std::holds_alternative<pending_operation>(*first),"fetch is a pending operation");
    const auto request = *machine.pending();
    require(!machine.complete(operation_token{request.token.value()+1},operation_success{}),"wrong token rejected");
    require(!machine.complete(request.token,read_payload{{std::byte{1}}}),"local completion payload rejected");
    require(machine.hart().retired()==0 && machine.hart().pc().value()==code_base,"failed completion has no effects");
    require(!machine.boot(code.bytes(),physical_address{code_base},instruction_address{code_base}),"boot rejects pending work");
    require(machine.complete(request.token,operation_success{}).has_value(),"fetch acknowledged");
    require(!machine.complete(request.token,operation_success{}),"duplicate completion rejected");
    auto result = run_program(machine,{},1);
    require(result && result->reason==run_reason::budget && machine.hart().retired()==1,"budget stops at a retirement boundary");
    const auto pc = machine.hart().pc();
    require(!machine.boot({},physical_address{code_base},instruction_address{code_base}) && machine.hart().pc()==pc,"invalid boot is atomic");
    result = run_program(machine,{},10);
    require(result && result->reason==run_reason::stopped && result->status==42 && machine.hart().retired()==3,"STOP retires once and returns status");
    const auto retired = machine.hart().retired();
    require(run_program(machine,{},10)->reason==run_reason::stopped && machine.hart().retired()==retired,"STOP is idempotent");
    boot(machine,code); const auto next = machine.advance();
    require(next && machine.pending()->token != request.token,"tokens survive boot");
    require(!machine.complete(request.token,operation_success{}),"stale callback rejected after boot");
}
void trap_recovery() {
    program_builder code;
    code.li(x(1),code_base+256).emit(scalar_word{0x30509073}).emit(holon_word{0});
    code.li(x(9),123).npu(STOP,{{status,x(9)}});
    while(code.offset()<256) code.emit(scalar_word{0x13});
    code.emit(scalar_word{0x341022f3}).addi(x(5),x(5),8).emit(scalar_word{0x34129073}).emit(scalar_word{0x30200073});
    program_machine machine(map()); boot(machine,code);
    const auto result = run_program(machine,{},100);
    require(result && result->reason==run_reason::stopped && result->status==123 && result->traps==1,"guest handles illegal Holon encoding through MRET");
    require(*machine.hart().read_csr(scalar::csr_address{0x342})==2 && *machine.hart().read_csr(scalar::csr_address{0x343})==0,"standard illegal instruction trap and mtval");
}

void vector_length_loop() {
    constexpr unsigned elements = 37;
    for (const auto capacity : {16u,64u,128u}) {
        program_machine machine(map(), {.vector_bytes=capacity});
        std::vector<std::byte> ram(0x10000);
        for (unsigned i=0;i<elements;++i) {
            put(ram,i*4,bits(float(i)-18.0f)); put(ram,256+i*4,bits(float(i)*0.25f));
        }
        program_builder code;
        transfer(code,true,spm_base,ram_base,512);
        code.li(x(9),elements).li(x(5),spm_base).li(x(6),spm_base+256).li(x(7),spm_base+512);
        const auto loop = code.offset();
        code.npu(VSETL,{{rd,x(4)},{avl,x(9)},{type,npu_type::f32},{peer_type,npu_type::f32}})
            .npu(PTRUE,{{pd,p(7)},{vl,x(4)}})
            .npu(VLD,{{vd,v(1)},{base,x(5)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{policy,mask_policy::zero},{offset,displacement{0}}})
            .npu(VLD,{{vd,v(2)},{base,x(6)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{policy,mask_policy::zero},{offset,displacement{0}}})
            .npu(VADD,{{vd,v(3)},{va,v(1)},{vb,v(2)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{policy,mask_policy::zero}})
            .npu(VST,{{va,v(3)},{base,x(7)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{offset,displacement{0}}});
        code.emit(scalar_word{0x404484b3}); // sub x9,x9,x4
        code.emit(scalar_word{0x00221413}); // slli x8,x4,2
        for (unsigned reg : {5u,6u,7u}) code.emit(scalar_word{0x33u | (reg<<7) | (reg<<15) | (8u<<20)});
        const auto relative = static_cast<std::uint32_t>(loop-code.offset()) & 8191;
        code.emit(scalar_word{0x1063u | (9u<<15) | ((relative>>12)<<31) | (((relative>>11)&1)<<7)
            | (((relative>>5)&63)<<25) | (((relative>>1)&15)<<8)});
        transfer(code,false,spm_base+512,ram_base+512,elements*4); finish(code);
        boot(machine,code);
        const auto report=run_program(machine,{system_address{ram_base},ram},10000);
        require(report && report->reason==run_reason::stopped && !report->traps,"guest VLA loop completes without host iteration");
        for(unsigned i=0;i<elements;++i) require(get(ram,512+i*4)==bits(float(i)*1.25f-18.0f),"VLA tail result independent of capacity");
    }
}

void conversion_rounding() {
    const std::array<std::uint32_t,8> input{bits(1.5f),bits(2.5f),bits(-1.5f),bits(-2.5f),bits(300.0f),bits(-300.0f),0x7fc00000u,0x7f800000u};
    const std::array<std::array<int,8>,4> expected{{
        {{2,2,-2,-2,127,-128,0,127}}, {{1,2,-1,-2,127,-128,0,127}},
        {{1,2,-2,-3,127,-128,0,127}}, {{2,3,-1,-2,127,-128,0,127}}}};
    const auto previous = std::fegetround();
    for(unsigned mode=0;mode<4;++mode) {
        std::vector<std::byte> ram(0x10000);
        for(unsigned i=0;i<8;++i) put(ram,i*4,input[i]);
        program_builder code;
        transfer(code,true,spm_base,ram_base,32);
        code.li(x(4),8).npu(PTRUE,{{pd,p(7)},{vl,x(4)}});
        load_vector(code,1,spm_base,npu_type::f32);
        code.npu(VCONVERT,{{vd,v(2)},{va,v(1)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},
            {result_type,npu_type::i8},{rounding,static_cast<rounding_mode>(mode)},{policy,mask_policy::zero}});
        store_vector(code,2,spm_base+64,npu_type::i8); transfer(code,false,spm_base+64,ram_base+64,8); finish(code);
        program_machine machine(map()); boot(machine,code);
        require(std::fesetround(FE_UPWARD)==0,"test caller floating environment");
        const auto report=run_program(machine,{system_address{ram_base},ram},1000);
        require(report && report->reason==run_reason::stopped && !report->traps,"conversion program");
        require(std::fegetround()==FE_UPWARD,"NPU restores caller rounding");
        for(unsigned i=0;i<8;++i) require(std::to_integer<unsigned>(ram[64+i])==static_cast<unsigned>(expected[mode][i]&255),"conversion tie/clamp/NaN scoreboard");
    }
    require(std::fesetround(previous)==0,"restore test rounding");
}

void floating_edges() {
    struct sample { npu_opcode op; std::array<std::uint32_t,4> a,b,c,expected; };
    const std::array samples{
        sample{VADD, {1,0x00800000,0x7f800000,0x80000000}, {1,0x807fffff,0xff800000,0x80000000}, {}, {2,1,0x7fc00000,0x80000000}},
        sample{VMUL, {0x80000000,0x00800000,0x7f7fffff,0x7f800001}, {bits(2),bits(0.5f),bits(2),bits(1)}, {}, {0x80000000,0x00400000,0x7f800000,0x7fc00000}},
        sample{VDIV, {bits(1),bits(-1),0,1}, {0,0,0,bits(2)}, {}, {0x7f800000,0xff800000,0x7fc00000,0}},
        sample{VSQRT, {bits(4),bits(-1),0x80000000,0x80000001}, {}, {}, {bits(2),0x7fc00000,0x80000000,0x7fc00000}},
        sample{VFMA, {0x3f800001,bits(2),0x7f800000,bits(2)}, {0x3f7ffffe,bits(3),0,bits(3)}, {bits(-1),bits(1),bits(1),bits(-6)}, {0xa8800000,bits(7),0x7fc00000,0}},
        sample{VMIN, {0x80000000,0,0x7fc00000,bits(4)}, {0,0x80000000,bits(3),0x7fc00000}, {}, {0x80000000,0x80000000,bits(3),bits(4)}},
        sample{VMAX, {0x80000000,0,0x7fc00000,bits(4)}, {0,0x80000000,bits(3),0x7fc00000}, {}, {0,0,bits(3),bits(4)}}};
    for (const auto& sample : samples) {
        std::vector<std::byte> ram(0x10000);
        for(unsigned i=0;i<4;++i) {put(ram,i*4,sample.a[i]);put(ram,16+i*4,sample.b[i]);put(ram,32+i*4,sample.c[i]);}
        program_builder code;
        transfer(code,true,spm_base,ram_base,48);
        code.li(x(4),4).npu(PTRUE,{{pd,p(7)},{vl,x(4)}});
        load_vector(code,1,spm_base,npu_type::f32);load_vector(code,2,spm_base+16,npu_type::f32);load_vector(code,3,spm_base+32,npu_type::f32);
        std::vector<named_operand> args{{vd,v(4)},{va,v(1)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{policy,mask_policy::zero}};
        if(sample.op!=VSQRT) args.push_back({vb,v(2)});
        if(sample.op==VFMA) args.push_back({vc,v(3)});
        code.emit(*encode_holon(sample.op,args));
        store_vector(code,4,spm_base+64,npu_type::f32);transfer(code,false,spm_base+64,ram_base+64,16);finish(code);
        program_machine machine(map());boot(machine,code);
        const auto result=run_program(machine,{system_address{ram_base},ram},1000);
        require(result && result->reason==run_reason::stopped && !result->traps,"binary32 edge program executes");
        for(unsigned i=0;i<4;++i) require(get(ram,64+i*4)==sample.expected[i],"binary32 exact edge bits including single-round FMA");
    }
}

void inactive_and_atomic_fault() {
    std::vector<std::byte> ram(0x10000);put(ram,0,0x12345678);
    program_builder code;
    transfer(code,true,spm_base+65532,ram_base,4);
    code.li(x(4),4).li(x(10),0).li(x(11),1).npu(PWHILELT,{{pd,p(7)},{base,x(10)},{end,x(11)},{vl,x(4)}});
    load_vector(code,1,spm_base+65532,npu_type::u32);
    code.npu(PTRUE,{{pd,p(7)},{vl,x(4)}});
    code.li(x(5),spm_base+65532);
    const auto fault_pc=code_base+code.offset();
    code.npu(VLD,{{vd,v(1)},{base,x(5)},{pg,p(7)},{vl,x(4)},{type,npu_type::u32},{policy,mask_policy::zero},{offset,displacement{0}}});finish(code);
    program_machine machine(map());boot(machine,code);
    bool trapped=false;
    for(unsigned i=0;i<1000 && !trapped;++i) {
        const auto prior=machine.hart().retired();
        auto event=machine.pending()?service_operation(machine,{system_address{ram_base},ram}):machine.advance();
        require(event.has_value(),"masked/fault issue");
        if(const auto* trap=std::get_if<scalar::trap_taken>(&*event)) {
            require(trap->pc.value()==fault_pc && trap->cause==5 && trap->value==spm_base+65536,"first active invalid lane precise load fault");
            require(machine.hart().retired()==prior,"fault does not retire");trapped=true;
        }
    }
    require(trapped,"active out-of-bounds access trapped");
    const auto bytes=machine.vector_bytes(v(1));
    require(get(bytes,0)==0x12345678 && std::ranges::all_of(bytes.subspan(4),[](auto b){return b==std::byte{};}),"inactive lanes never fault; later failed load preserves whole destination");
}

pending_operation next_external(program_machine& machine) {
    for (unsigned i=0;i<1000;++i) {
        if (const auto pending=machine.pending()) {
            const auto* request=std::get_if<memory_request>(&pending->value);
            if (request && request->storage==memory::storage::system) return *pending;
            require(service_operation(machine,{}).has_value(),"complete preceding local/fetch operation");
        } else {
            const auto event=machine.advance();
            require(event && !std::holds_alternative<scalar::trap_taken>(*event),"issue before external request");
        }
    }
    throw std::runtime_error("no external memory request issued within bound");
}

pending_operation next_engine(program_machine& machine) {
    for (unsigned i = 0; i < 1000; ++i) {
        if (const auto pending = machine.pending()) {
            if (std::holds_alternative<npu_request>(pending->value)) return *pending;
            require(service_operation(machine, {}).has_value(), "service instruction fetch");
        } else {
            const auto event = machine.advance();
            require(event && !std::holds_alternative<scalar::trap_taken>(*event), "issue before engine request");
        }
    }
    throw std::runtime_error("no engine request within bound");
}

void captured_footprints() {
    program_builder code;
    code.li(x(4), 3).npu(PTRUE, {{pd,p(7)}, {vl,x(4)}});
    code.li(x(4), 7);
    load_vector(code, 1, spm_base, npu_type::i16);
    program_machine machine(map()); boot(machine, code);
    auto pending = next_engine(machine);
    require(machine.complete(pending.token, operation_success{}).has_value(), "initialize predicate");
    pending = next_engine(machine);
    const auto footprint = std::get<npu_request>(pending.value).footprint;
    require(footprint.lanes == 7 && footprint.active_lanes == 3 && footprint.element_bytes == 2
        && footprint.local_read_bytes == 6 && !footprint.local_write_bytes, "captured VL, predicate and byte traffic");
    const auto retired = machine.hart().retired();
    require(!machine.advance() && machine.hart().retired() == retired, "captured work does not retire before completion");
    require(machine.complete(pending.token, operation_success{}).has_value(), "complete captured vector load");

    for (const bool matrix : {false, true}) {
        program_builder invalid;
        invalid.li(x(4), 0xffffffffu);
        if (matrix) invalid.npu(MCLEAR, {{td,t(0)}, {rows,x(4)}, {cols,x(4)}, {type,npu_type::f32}});
        else load_vector(invalid, 1, spm_base, npu_type::f32);
        boot(machine, invalid);
        pending = next_engine(machine);
        const auto f = std::get<npu_request>(pending.value).footprint;
        require(!f.lanes && !f.active_lanes && !f.matrix_m && !f.matrix_n && !f.local_read_bytes,
            "invalid dimensions cannot create unbounded scheduled work");
        const auto before = machine.hart().retired();
        const auto event = machine.complete(pending.token, operation_success{});
        require(event && std::holds_alternative<scalar::trap_taken>(*event), "invalid shape traps on completion");
        const auto trap = std::get<scalar::trap_taken>(*event);
        require(trap.pc == pending.pc && trap.cause == std::to_underlying(npu_trap::invalid_operand)
            && machine.hart().retired() == before, "shape fault remains precise without retirement");
    }
}

void asynchronous_memory_contract() {
    program_builder code;
    transfer(code,true,spm_base,ram_base,8);
    transfer(code,false,spm_base,ram_base+32,8); finish(code);
    program_machine machine(map());boot(machine,code);
    const auto load=next_external(machine);
    const auto retired=machine.hart().retired();
    const auto before=std::vector(machine.local_bytes().begin(),machine.local_bytes().end());
    require(!machine.advance() && machine.hart().retired()==retired,"pending DMA cannot advance or retire");
    require(!machine.complete(load.token,operation_success{}),"external load requires payload");
    require(!machine.complete(load.token,read_payload{std::vector<std::byte>(7)}),"short payload is an API error");
    require(!machine.complete(load.token,read_payload{std::vector<std::byte>(9)}),"oversized payload is an API error");
    require(std::ranges::equal(machine.local_bytes(),before) && machine.pending()->token==load.token,"invalid completion preserves pending state and local memory");
    std::vector<std::byte> payload(8);put(payload,0,0x12345678);put(payload,4,0xabcdef01);
    require(machine.complete(load.token,read_payload{payload}).has_value(),"DMA load completes exactly once");
    require(machine.hart().retired()==retired+1 && get(machine.local_bytes(),0)==0x12345678,"load visibility and retirement coincide");
    const auto store=next_external(machine);
    const auto request=std::get<memory_request>(store.value);
    require(request.payload==payload && request.address.value()==ram_base+32,"store owns stable issue-time bytes");
    require(!machine.complete(load.token,operation_success{}),"earlier load token cannot complete store");
    require(!machine.complete(store.token,read_payload{payload}),"store rejects read completion payload");
    require(std::get<memory_request>(machine.pending()->value).payload==payload,"rejected callbacks preserve captured store");
    // The environment accepts the first packet before the second packet fails.
    std::array<std::byte, 8> external_store{};
    std::ranges::copy(std::span{request.payload}.first(4), external_store.begin());
    const auto store_retired=machine.hart().retired();
    const auto fault=machine.complete(store.token,bus_fault{physical_address{ram_base+36}});
    require(fault && std::holds_alternative<scalar::trap_taken>(*fault),"write response fault traps");
    const auto trap=std::get<scalar::trap_taken>(*fault);
    require(trap.pc==store.pc && trap.cause==7 && trap.value==ram_base+36 && machine.hart().retired()==store_retired,"store bus fault retains precise PC and does not retire");
    require(get(external_store,0)==0x12345678 && get(external_store,4)==0,
        "failed store does not imply rollback of accepted external bytes");
    require(!machine.complete(store.token,operation_success{}),"late store acknowledgement cannot retire a faulted instruction");

    boot(machine,code);
    const auto failed_load=next_external(machine);
    const auto clean=std::vector(machine.local_bytes().begin(),machine.local_bytes().end());
    require(!machine.complete(failed_load.token,bus_fault{physical_address{ram_base+8}}),"fault outside pending request is an API error");
    require(machine.pending()->token==failed_load.token && std::ranges::equal(machine.local_bytes(),clean),"invalid fault address preserves pending operation");
    const auto failed=machine.complete(failed_load.token,bus_fault{physical_address{ram_base+4}});
    require(failed && std::holds_alternative<scalar::trap_taken>(*failed),"load response fault traps");
    const auto load_trap=std::get<scalar::trap_taken>(*failed);
    require(load_trap.pc==failed_load.pc && load_trap.cause==5 && load_trap.value==ram_base+4,"precise split load fault address");
    require(std::ranges::equal(machine.local_bytes(),clean),"failed external load has no partial SPM write");
    require(!machine.complete(failed_load.token,read_payload{payload}),"late payload cannot resurrect faulted load");

    boot(machine,code);
    const auto abandoned=next_external(machine);
    machine.reset();
    require(!machine.pending() && !machine.complete(abandoned.token,read_payload{payload}),"external reset invalidates pending callback");
    boot(machine,code);
    require(next_external(machine).token!=abandoned.token,"reset never recycles operation tokens");
}
}
int main() {
    try {
        completion_and_boot(); trap_recovery(); vector_program(); matrix_program(); vector_length_loop(); conversion_rounding();
        floating_edges(); inactive_and_atomic_fault(); asynchronous_memory_contract(); captured_footprints();
        capabilities(); integer_conversion_edges();
        std::cout << "canonical mixed RV32/Holon execution: precise completion, trap/MRET, vector capacities, matrix and DMA writeback PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
