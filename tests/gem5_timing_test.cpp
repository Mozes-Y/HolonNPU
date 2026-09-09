#include "holon_npu_timing.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace holon_npu;
    using namespace semantic::instruction;
    const auto request=[](npu_opcode opcode,semantic::npu_footprint footprint) {
        const auto pattern=std::ranges::find(npu_patterns,opcode,&npu_pattern::opcode);
        if(pattern==npu_patterns.end())throw std::logic_error("test opcode");
        return semantic::npu_request{npu_instruction{*pattern,{}},footprint};
    };
    const auto check=[](bool ok){if(!ok)throw std::runtime_error("timing scoreboard mismatch");};
    try {
        gem5_model::timing_model timing;
        for (const auto opcode : {npu_opcode::STOP, npu_opcode::CAPS, npu_opcode::VSETL}) {
            const auto control = timing.estimate(request(opcode, {}));
            check(control.unit == gem5_model::resource::frontend && control.cycles == 1 && !control.lane_slots);
        }
        const auto vector=request(npu_opcode::VADD,{.lanes=17,.active_lanes=9,.element_bytes=4});
        auto result=timing.estimate(vector);check(result.cycles==3&&result.active_lanes==9&&result.lane_slots==32);
        check(timing.estimate(request(npu_opcode::VDIV,vector.footprint)).cycles==25);
        check(timing.estimate(request(npu_opcode::VSQRT,vector.footprint)).cycles==33);
        check(timing.estimate(request(npu_opcode::VREDSUM,vector.footprint)).cycles==12);
        check(timing.estimate(request(npu_opcode::VLD,{.lanes=4,.active_lanes=4,.element_bytes=4,.local_read_bytes=16})).cycles==6);
        result=timing.estimate(request(npu_opcode::MDOT,{.matrix_m=3,.matrix_n=4,.matrix_k=4}));
        check(result.cycles==11&&result.matrix_macs==48);
        check(timing.estimate(request(npu_opcode::MDOT,{.matrix_m=17,.matrix_n=19,.matrix_k=23})).cycles==161);
        auto narrow=gem5_model::timing_model({.vector_lanes=4});
        check(narrow.estimate(vector).cycles==6);
        bool rejected=false;try{gem5_model::timing_model bad({.vector_lanes=0});}catch(const std::invalid_argument&){rejected=true;}
        check(rejected);
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    std::cout<<"Autonomous timing: captured footprints, resource accounting and parameter sensitivity PASS\n";
}
