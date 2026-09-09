#include "holon_npu_execution.hpp"
#include "holon_npu_runtime.hpp"

#include <cmath>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using namespace holon_npu::semantic;
using namespace holon_npu::semantic::instruction;
using holon_npu::runtime::program_builder;
using enum npu_role;
using enum npu_opcode;
constexpr unsigned tokens=3, width=4, hidden=8, vocabulary=8, image_bytes=8192;
constexpr std::uint32_t code_base=0x1000, local_base=0x10000, system_base=0x80000000;
enum class tensor : unsigned {
    embedding, position, ids, wq, wk, wv, wo, w1, w2, logits_weight,
    gamma1, beta1, gamma2, beta2,
    embedded=16, q, k, value, scores, probabilities, context, projected,
    residual1, normalized1, feed_forward, activated, feed_projected,
    residual2, normalized2, logits
};
constexpr unsigned address(tensor t) { return std::to_underlying(t)*256; }
scalar_register x(unsigned i) {return scalar_register{static_cast<std::uint8_t>(i)};}
vector_register v(unsigned i) {return vector_register{static_cast<std::uint8_t>(i)};}
predicate_register p(unsigned i) {return predicate_register{static_cast<std::uint8_t>(i)};}
tile_register tile(unsigned i) {return tile_register{static_cast<std::uint8_t>(i)};}
tile_view view_id(unsigned i) {return tile_view{static_cast<std::uint8_t>(i)};}
std::uint32_t bits(float value) {return std::bit_cast<std::uint32_t>(value);}
void require(bool ok,std::string_view message) {if(!ok)throw std::runtime_error(std::string{message});}
void put(std::span<std::byte> memory,unsigned at,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)memory[at+i]=static_cast<std::byte>(value>>(8*i));
}
std::uint32_t get(std::span<const std::byte> memory,unsigned at) {
    std::uint32_t value{};for(unsigned i=0;i<4;++i)value|=std::to_integer<std::uint32_t>(memory[at+i])<<(8*i);return value;
}

// Shape-specialized assembly only. No tensor data is read while building code.
class transformer_program {
public:
    program_builder code;
    transformer_program() {
        dma(true);
        embedding();
        matrix(tensor::embedded,tensor::wq,tensor::q,tokens,width,width);
        matrix(tensor::embedded,tensor::wk,tensor::k,tokens,width,width);
        matrix(tensor::embedded,tensor::wv,tensor::value,tokens,width,width);
        matrix(tensor::q,tensor::k,tensor::scores,tokens,tokens,width,true);
        softmax();
        matrix(tensor::probabilities,tensor::value,tensor::context,tokens,width,tokens);
        matrix(tensor::context,tensor::wo,tensor::projected,tokens,width,width);
        residual(tensor::embedded,tensor::projected,tensor::residual1);
        normalize(tensor::residual1,tensor::gamma1,tensor::beta1,tensor::normalized1);
        matrix(tensor::normalized1,tensor::w1,tensor::feed_forward,tokens,hidden,width);
        length(width);constant(2,0);
        for(unsigned i=0;i<tokens*hidden;i+=width) {
            load(1,address(tensor::feed_forward)+4*i);binary(VMAX,1,1,2);store(1,address(tensor::activated)+4*i);
        }
        matrix(tensor::activated,tensor::w2,tensor::feed_projected,tokens,width,hidden);
        residual(tensor::normalized1,tensor::feed_projected,tensor::residual2);
        normalize(tensor::residual2,tensor::gamma2,tensor::beta2,tensor::normalized2);
        matrix(tensor::normalized2,tensor::logits_weight,tensor::logits,tokens,vocabulary,width);
        dma(false);code.npu(STOP,{{status,x(0)}});
    }
private:
    void dma(bool load) {
        code.li(x(1),load?local_base:system_base).li(x(2),load?system_base:local_base).li(x(3),image_bytes)
            .npu(load?DLOAD:DSTORE,{{dst,x(1)},{src,x(2)},{npu_role::count,x(3)}});
    }
    void length(unsigned n) {code.li(x(4),n).npu(PTRUE,{{pd,p(7)},{vl,x(4)}});}
    void load_pointer(unsigned dest) {
        code.npu(VLD,{{vd,v(dest)},{base,x(5)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{policy,mask_policy::zero},{offset,displacement{0}}});
    }
    void load(unsigned dest,unsigned at) {code.li(x(5),local_base+at);load_pointer(dest);}
    void store(unsigned source,unsigned at) {
        code.li(x(5),local_base+at).npu(VST,{{va,v(source)},{base,x(5)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{offset,displacement{0}}});
    }
    void broadcast(unsigned dest,unsigned scalar) {
        code.npu(VBROADCAST,{{vd,v(dest)},{npu_role::value,x(scalar)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{policy,mask_policy::zero}});
    }
    void constant(unsigned dest,float value) {code.li(x(6),bits(value));broadcast(dest,6);}
    void binary(npu_opcode op,unsigned dest,unsigned a,unsigned b) {
        code.npu(op,{{vd,v(dest)},{va,v(a)},{vb,v(b)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{policy,mask_policy::zero}});
    }
    void fma(unsigned dest,unsigned a,unsigned b,unsigned c) {
        code.npu(VFMA,{{vd,v(dest)},{va,v(a)},{vb,v(b)},{vc,v(c)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{policy,mask_policy::zero}});
    }
    void reduce(npu_opcode op,unsigned source,unsigned scalar,float seed_value) {
        code.li(x(6),bits(seed_value)).npu(op,{{rd,x(scalar)},{va,v(source)},{seed,x(6)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32}});
    }
    void embedding() {
        length(width);
        for(unsigned row=0;row<tokens;++row) {
            code.li(x(5),local_base+address(tensor::ids)+4*row)
                .emit(scalar_word{0x0002a383u}) // LW x7,0(x5)
                .emit(scalar_word{0x00439393u}) // SLLI x7,x7,4: embedding row stride
                .li(x(5),local_base+address(tensor::embedding))
                .emit(scalar_word{0x007282b3u}); // ADD x5,x5,x7
            load_pointer(1);load(2,address(tensor::position)+row*width*4);
            binary(VADD,1,1,2);store(1,address(tensor::embedded)+row*width*4);
        }
    }
    void matrix(tensor a,tensor b,tensor out,unsigned m,unsigned n,unsigned k,bool transpose_b=false) {
        const auto capture=[&](unsigned id,tensor input,unsigned rows_value,unsigned cols_value,unsigned rs,unsigned cs) {
            code.li(x(10),local_base+address(input)).li(x(11),rows_value).li(x(12),cols_value).li(x(13),rs).li(x(14),cs)
                .npu(MVIEW,{{npu_role::view,view_id(id)},{base,x(10)},{rows,x(11)},{cols,x(12)},
                    {row_stride,x(13)},{col_stride,x(14)},{type,npu_type::f32}});
        };
        capture(0,a,m,k,k*4,4);capture(1,b,k,n,transpose_b?4:n*4,transpose_b?k*4:4);capture(2,out,m,n,n*4,4);
        code.npu(MLOAD,{{td,tile(0)},{npu_role::view,view_id(0)}}).npu(MLOAD,{{td,tile(1)},{npu_role::view,view_id(1)}})
            .npu(MDOT,{{td,tile(2)},{ta,tile(0)},{tb,tile(1)},{type,npu_type::f32}})
            .npu(MSTORE,{{ts,tile(2)},{npu_role::view,view_id(2)}});
    }
    void softmax() {
        constexpr std::array coefficients{1.0f/3628800,1.0f/362880,1.0f/40320,1.0f/5040,
            1.0f/720,1.0f/120,1.0f/24,1.0f/6,0.5f,1.0f,1.0f};
        for(unsigned row=0;row<tokens;++row) {
            length(tokens);load(1,address(tensor::scores)+row*tokens*4);
            constant(2,0.5f);binary(VMUL,1,1,2);store(1,address(tensor::scores)+row*tokens*4);
            code.li(x(8),0).li(x(9),row+1).npu(PWHILELT,{{pd,p(7)},{base,x(8)},{end,x(9)},{vl,x(4)}});
            reduce(VREDMAX,1,10,-std::numeric_limits<float>::infinity());broadcast(2,10);binary(VSUB,1,1,2);
            constant(2,-16);binary(VMAX,1,1,2);constant(2,0);binary(VMIN,1,1,2);
            constant(2,1.0f/16);binary(VMUL,5,1,2);
            constant(6,coefficients.front());
            for(std::size_t i=1;i<coefficients.size();++i){constant(8,coefficients[i]);fma(6,6,5,8);}
            for(unsigned i=0;i<4;++i)binary(VMUL,6,6,6);
            reduce(VREDSUM,6,10,0);broadcast(2,10);binary(VDIV,6,6,2);
            // Inactive arithmetic lanes were zeroed; store the complete causal row.
            code.npu(PTRUE,{{pd,p(7)},{vl,x(4)}});store(6,address(tensor::probabilities)+row*tokens*4);
        }
    }
    void residual(tensor a,tensor b,tensor out) {
        length(width);
        for(unsigned row=0;row<tokens;++row){load(1,address(a)+row*width*4);load(2,address(b)+row*width*4);binary(VADD,1,1,2);store(1,address(out)+row*width*4);}
    }
    void normalize(tensor input,tensor gamma,tensor beta,tensor out) {
        length(width);
        for(unsigned row=0;row<tokens;++row) {
            load(1,address(input)+row*width*4);
            reduce(VREDSUM,1,10,0);broadcast(2,10);constant(3,1.0f/width);binary(VMUL,2,2,3);
            binary(VSUB,1,1,2);binary(VMUL,4,1,1);reduce(VREDSUM,4,10,0);broadcast(2,10);binary(VMUL,2,2,3);
            constant(3,1e-5f);binary(VADD,2,2,3);
            code.npu(VSQRT,{{vd,v(2)},{va,v(2)},{pg,p(7)},{vl,x(4)},{type,npu_type::f32},{policy,mask_policy::zero}});
            binary(VDIV,1,1,2);load(3,address(gamma));load(4,address(beta));fma(1,1,3,4);store(1,address(out)+row*width*4);
        }
    }
};

struct stage {tensor location;std::string_view name;unsigned rows,cols;};
constexpr std::array stages{
    stage{tensor::embedded,"embedding",tokens,width},stage{tensor::q,"Q",tokens,width},stage{tensor::k,"K",tokens,width},
    stage{tensor::value,"V",tokens,width},stage{tensor::scores,"scaled_scores",tokens,tokens},stage{tensor::probabilities,"softmax",tokens,tokens},
    stage{tensor::context,"context",tokens,width},stage{tensor::projected,"projection",tokens,width},stage{tensor::residual1,"residual1",tokens,width},
    stage{tensor::normalized1,"layernorm1",tokens,width},stage{tensor::feed_forward,"ffn1",tokens,hidden},stage{tensor::activated,"relu",tokens,hidden},
    stage{tensor::feed_projected,"ffn2",tokens,width},stage{tensor::residual2,"residual2",tokens,width},stage{tensor::normalized2,"layernorm2",tokens,width},
    stage{tensor::logits,"logits",tokens,vocabulary}};

// Independent mathematical reference: doubles and exact libm exp, no ISA execution helpers.
using reference_image=std::array<std::vector<double>,32>;
reference_image reference(std::span<const std::byte> initial) {
    reference_image r;
    for(unsigned slot=0;slot<14;++slot)for(unsigned i=0;i<64;++i)r[slot].push_back(std::bit_cast<float>(get(initial,slot*256+i*4)));
    const auto at=[&](tensor t)->std::vector<double>&{return r[std::to_underlying(t)];};
    const auto product=[&](tensor a,tensor b,tensor out,unsigned m,unsigned n,unsigned k,bool transposed=false) {
        auto& c=at(out);c.assign(m*n,0);
        for(unsigned i=0;i<m;++i)for(unsigned j=0;j<n;++j)for(unsigned kk=0;kk<k;++kk)
            c[i*n+j]+=at(a)[i*k+kk]*at(b)[transposed?j*k+kk:kk*n+j];
    };
    at(tensor::embedded).resize(tokens*width);
    for(unsigned row=0;row<tokens;++row)for(unsigned col=0;col<width;++col)
        at(tensor::embedded)[row*width+col]=at(tensor::embedding)[get(initial,address(tensor::ids)+row*4)*width+col]+at(tensor::position)[row*width+col];
    product(tensor::embedded,tensor::wq,tensor::q,tokens,width,width);
    product(tensor::embedded,tensor::wk,tensor::k,tokens,width,width);
    product(tensor::embedded,tensor::wv,tensor::value,tokens,width,width);
    product(tensor::q,tensor::k,tensor::scores,tokens,tokens,width,true);
    for(auto& score:at(tensor::scores))score/=std::sqrt(double(width));
    at(tensor::probabilities).assign(tokens*tokens,0);
    for(unsigned row=0;row<tokens;++row) {
        double maximum=-std::numeric_limits<double>::infinity(),sum=0;
        for(unsigned col=0;col<=row;++col)maximum=std::max(maximum,at(tensor::scores)[row*tokens+col]);
        for(unsigned col=0;col<=row;++col){auto& q=at(tensor::probabilities)[row*tokens+col];q=std::exp(at(tensor::scores)[row*tokens+col]-maximum);sum+=q;}
        for(unsigned col=0;col<=row;++col)at(tensor::probabilities)[row*tokens+col]/=sum;
    }
    product(tensor::probabilities,tensor::value,tensor::context,tokens,width,tokens);
    product(tensor::context,tensor::wo,tensor::projected,tokens,width,width);
    const auto residual=[&](tensor a,tensor b,tensor out) {
        at(out).resize(tokens*width);for(unsigned i=0;i<tokens*width;++i)at(out)[i]=at(a)[i]+at(b)[i];
    };
    const auto norm=[&](tensor input,tensor gamma,tensor beta,tensor out) {
        at(out).resize(tokens*width);
        for(unsigned row=0;row<tokens;++row) {
            double mean=0,variance=0;for(unsigned col=0;col<width;++col)mean+=at(input)[row*width+col];mean/=width;
            for(unsigned col=0;col<width;++col){auto d=at(input)[row*width+col]-mean;variance+=d*d;}variance/=width;
            for(unsigned col=0;col<width;++col)at(out)[row*width+col]=(at(input)[row*width+col]-mean)/std::sqrt(variance+1e-5)*at(gamma)[col]+at(beta)[col];
        }
    };
    residual(tensor::embedded,tensor::projected,tensor::residual1);norm(tensor::residual1,tensor::gamma1,tensor::beta1,tensor::normalized1);
    product(tensor::normalized1,tensor::w1,tensor::feed_forward,tokens,hidden,width);
    at(tensor::activated)=at(tensor::feed_forward);for(auto& value:at(tensor::activated))value=std::max(value,0.0);
    product(tensor::activated,tensor::w2,tensor::feed_projected,tokens,width,hidden);
    residual(tensor::normalized1,tensor::feed_projected,tensor::residual2);norm(tensor::residual2,tensor::gamma2,tensor::beta2,tensor::normalized2);
    product(tensor::normalized2,tensor::logits_weight,tensor::logits,tokens,vocabulary,width);
    return r;
}

std::vector<std::byte> input(unsigned seed) {
    std::vector<std::byte> ram(image_bytes);std::mt19937 random(seed);
    for(unsigned slot=0;slot<14;++slot)for(unsigned i=0;i<64;++i) {
        float value=seed==0?0.0f:(seed==1?0.125f:static_cast<float>(static_cast<int>(random()%2001)-1000)/2000);
        put(ram,slot*256+4*i,bits(value));
    }
    for(unsigned i=0;i<tokens;++i)put(ram,address(tensor::ids)+4*i,random()%vocabulary);
    for(unsigned i=0;i<width;++i)for(auto t:{tensor::gamma1,tensor::gamma2})put(ram,address(t)+4*i,bits(1.0f+float(i)/8));
    if(seed==3) {
        // Near-constant residuals exercise epsilon without relying on exact zeros.
        for(unsigned i=0;i<64;++i) {
            put(ram,address(tensor::embedding)+4*i,bits(0.125f+float(i%4)*1e-5f));
            put(ram,address(tensor::position)+4*i,bits(0.125f));
        }
        for(auto t:{tensor::wo,tensor::w2})for(unsigned i=0;i<64;++i)put(ram,address(t)+4*i,0);
    }
    if(seed==4) {
        // Large Q/K projection magnitudes force the exp clamp and stable maximum.
        for(auto t:{tensor::wq,tensor::wk})for(unsigned i=0;i<64;++i) {
            const auto value=std::bit_cast<float>(get(ram,address(t)+4*i));
            put(ram,address(t)+4*i,bits(value*64));
        }
    }
    return ram;
}

void run_case(const transformer_program& program,unsigned seed,unsigned capacity,const std::filesystem::path& export_dir={}) {
    auto ram=input(seed);const auto expected=reference(ram);
    const auto initial=ram;
    if(seed==4) {
        bool clamped=false;
        const auto& scores=expected[std::to_underlying(tensor::scores)];
        for(unsigned row=1;row<tokens;++row) {
            const auto prefix=std::span{scores}.subspan(row*tokens,row+1);
            clamped|=std::ranges::max(prefix)-std::ranges::min(prefix)>16;
        }
        require(clamped,"large-score fixture must exercise guest exp clamp");
    }
    auto map=memory::physical_map::create(std::array{
        memory::region{physical_address{code_base},49152,memory::storage::program,{true,false,true}},
        memory::region{physical_address{local_base},65536,memory::storage::scratchpad,{true,true,false}},
        memory::region{physical_address{system_base},image_bytes,memory::storage::system,{true,true,false}}});
    require(map.has_value(),"workload map");
    program_machine machine(*map,{.vector_bytes=capacity});
    require(machine.boot(program.code.bytes(),physical_address{code_base},instruction_address{code_base}).has_value(),"boot Transformer program");
    auto report=run_program(machine,{system_address{system_base},ram},100000);
    require(report&&report->reason==run_reason::stopped&&!report->traps&&!report->status,"complete Transformer guest program without traps");
    double max_error=0;
    for(const auto& stage:stages)for(unsigned i=0;i<stage.rows*stage.cols;++i) {
        const auto actual=std::bit_cast<float>(get(ram,address(stage.location)+4*i));
        const auto wanted=expected[std::to_underlying(stage.location)][i];const auto error=std::abs(actual-wanted);
        if(!std::isfinite(actual)||error>2e-4+2e-4*std::abs(wanted)) {
            std::cerr<<"seed="<<seed<<" capacity="<<capacity<<" stage="<<stage.name<<" element="<<i<<" expected="<<wanted<<" actual="<<actual<<'\n';
            throw std::runtime_error("Transformer stage mismatch");
        }
        max_error=std::max(max_error,error);
    }
    for(unsigned row=0;row<tokens;++row){double sum=0;for(unsigned col=0;col<tokens;++col){const auto q=std::bit_cast<float>(get(ram,address(tensor::probabilities)+4*(row*tokens+col)));require(q>=0&&(col<=row||q==0),"causal probability invariant");sum+=q;}require(std::abs(sum-1)<1e-5,"probability row sums to one");}
    std::cout<<"Transformer seed="<<seed<<" VBYTES="<<capacity<<" stages="<<stages.size()<<" retired="<<report->retired<<" max_abs_error="<<max_error<<'\n';
    if(!export_dir.empty()) {
        std::filesystem::create_directories(export_dir);
        const auto write=[&](std::string_view name,std::span<const std::byte> bytes) {
            std::ofstream file(export_dir/name,std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            require(bool(file),"write shared gem5 workload image");
        };
        write("program.bin",program.code.bytes());write("input.bin",initial);write("expected.bin",ram);
        std::ofstream manifest(export_dir/"reference.json");
        manifest<<"{\"seed\":"<<seed<<",\"vector_bytes\":"<<capacity<<",\"pc\":"<<machine.hart().pc().value()
            <<",\"retired\":"<<report->retired<<",\"memory_bytes\":"<<image_bytes<<",\"stages\":"<<stages.size()<<"}\n";
        require(bool(manifest),"write shared workload metadata");
    }
}
} // namespace

int main(int argc,char** argv) {
    try {
        transformer_program program;
        if(argc==2 && std::string_view(argv[1]).starts_with("--export=")) {
            const auto destination=std::string_view(argv[1]).substr(9);
            require(!destination.empty(),"empty export directory");
            run_case(program,17,64,std::filesystem::path{destination});return 0;
        }
        require(argc==1,"usage: holon_npu_transformer_test [--export=directory]");
        for(unsigned capacity:{16u,64u,128u})for(unsigned seed:{0u,1u,3u,4u,17u,42u,123u,2026u,0x484f4cu,0xffffffffu})run_case(program,seed,capacity);
        std::cout<<"PASS: one self-hosted Transformer program, "<<program.code.offset()<<" bytes, 30 executions, independent double-precision reference\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
