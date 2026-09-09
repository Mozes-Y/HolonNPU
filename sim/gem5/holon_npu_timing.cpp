#include "holon_npu_timing.hpp"
#include <stdexcept>

namespace holon_npu::gem5_model {
namespace {
std::uint64_t groups(std::uint64_t elements, unsigned lanes) { return elements/lanes + (elements%lanes != 0); }
}
timing_model::timing_model(timing_parameters p) : parameters_(p) {
    if (!p.frontend_cycles || !p.vector_lanes || !p.vector_startup || !p.divide_cycles || !p.sqrt_cycles
        || !p.matrix_rows || !p.matrix_cols || !p.scratchpad_bytes_per_cycle || !p.dma_setup_cycles)
        throw std::invalid_argument("timing parameters must be positive");
}
operation_timing timing_model::estimate(const semantic::operation& operation) const {
    const auto& p=parameters_;
    if (const auto* m=std::get_if<semantic::memory_request>(&operation)) {
        if(m->storage==semantic::memory::storage::system) return {.cycles=p.dma_setup_cycles,.unit=resource::memory};
        if(m->access==semantic::memory::access::execute) return {.cycles=p.frontend_cycles,.unit=resource::frontend};
        return {.cycles=std::max<std::uint64_t>(1,groups(m->size,p.scratchpad_bytes_per_cycle)),.local_bytes=m->size,.unit=resource::local_memory};
    }
    if(std::holds_alternative<semantic::scalar::fence_request>(operation))return {.cycles=1,.unit=resource::sync};
    const auto& request=std::get<semantic::npu_request>(operation);
    const auto& f=request.footprint;
    const auto op=request.instruction.pattern.opcode;
    using enum semantic::instruction::npu_opcode;
    if(op==STOP||op==CAPS||op==VSETL)return {.cycles=p.frontend_cycles,.unit=resource::frontend};
    const bool matrix=op==MVIEW||op==MLOAD||op==MSTORE||op==MCLEAR||op==MDOT||op==MMACC;
    const auto local=f.local_read_bytes+f.local_write_bytes;
    auto cycles=std::uint64_t{p.vector_startup}+groups(local,p.scratchpad_bytes_per_cycle);
    std::uint64_t macs=0,slots=0;
    if(op==MDOT||op==MMACC) {
        macs=std::uint64_t{f.matrix_m}*f.matrix_n*f.matrix_k;
        // Blocking tiled wavefront estimate; no overlap or undocumented concurrency.
        for(std::uint32_t m=0;m<f.matrix_m;m+=p.matrix_rows)
            for(std::uint32_t n=0;n<f.matrix_n;n+=p.matrix_cols)
                if(f.matrix_k)cycles+=std::min(p.matrix_rows,f.matrix_m-m)+std::uint64_t{f.matrix_k}+std::min(p.matrix_cols,f.matrix_n-n)-1;
    } else if(matrix) {
        if(op==MCLEAR)cycles+=groups(std::uint64_t{f.matrix_m}*f.matrix_n,p.matrix_cols);
    } else {
        const auto g=groups(f.lanes,p.vector_lanes);
        slots=g*p.vector_lanes;
        const auto latency=op==VDIV?p.divide_cycles:(op==VSQRT?p.sqrt_cycles:1u);
        cycles+=g*latency;
        if(op==VREDSUM||op==VREDMIN||op==VREDMAX)cycles+=f.active_lanes;
    }
    return {cycles,f.active_lanes,slots,macs,local,matrix?resource::matrix:resource::vector};
}
}
