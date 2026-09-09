#include "holon_npu.hh"
#include "mem/packet.hh"
#include "sim/sim_exit.hh"
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace gem5 {
namespace {
namespace sem=holon_npu::semantic;
constexpr std::uint32_t codeBase=0x1000, localBase=0x10000;
std::vector<std::byte> readImage(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("cannot open image: "+path);
    const std::vector<char> chars{std::istreambuf_iterator<char>{file},{}};
    if(file.bad())throw std::runtime_error("cannot read image: "+path);
    return std::vector<std::byte>(std::from_range,std::as_bytes(std::span{chars}));
}
}
HolonNpu::counters::counters(statistics::Group* parent)
    : statistics::Group(parent,"holon"),
      cycles(this,"cycles",statistics::units::Cycle::get(),"Elapsed NPU cycles"),
      retired(this,"retired",statistics::units::Count::get(),"Retired guest instructions"),
      traps(this,"traps",statistics::units::Count::get(),"Architectural trap entries"),
      transactions(this,"transactions",statistics::units::Count::get(),"Accepted timing packets"),
      bytes(this,"bytes",statistics::units::Byte::get(),"Accepted external request bytes"),
      retries(this,"retries",statistics::units::Count::get(),"Memory request rejections"),
      memoryTicks(this,"memoryTicks",statistics::units::Tick::get(),"External operation memory service ticks"),
      activeLanes(this,"activeLanes",statistics::units::Count::get(),"Issued active lanes"),
      laneSlots(this,"laneSlots",statistics::units::Count::get(),"Issued vector lane slots"),
      matrixMacs(this,"matrixMacs",statistics::units::Count::get(),"Issued matrix MACs"),
      localBytes(this,"localBytes",statistics::units::Byte::get(),"Issued local data traffic"),
      resourceCycles(this,"resourceCycles",statistics::units::Cycle::get(),"Blocking scheduled resource cycles"),
      elapsedTicks(this,"elapsedTicks",statistics::units::Tick::get(),"Exclusive elapsed time by execution phase") {
    resourceCycles.init(6);
    elapsedTicks.init(7);
    const std::array names{"frontend","local","vector","matrix","memory_setup","sync"};
    for(unsigned i=0;i<names.size();++i) {
        resourceCycles.subname(i,names[i]);elapsedTicks.subname(i,names[i]);
    }
    elapsedTicks.subname(6,"memory_wait");
}
sem::memory::physical_map HolonNpu::addressMap(const HolonNpuParams& p) {
    if(p.memory_base>0xffffffffull||!p.memory_bytes||p.memory_bytes>0x100000000ull-p.memory_base)
        throw std::invalid_argument("external physical range must fit 32 bits");
    auto map=sem::memory::physical_map::create(std::array{
        sem::memory::region{sem::physical_address{codeBase},49152,sem::memory::storage::program,{true,false,true}},
        sem::memory::region{sem::physical_address{localBase},65536,sem::memory::storage::scratchpad,{true,true,false}},
        sem::memory::region{sem::physical_address{static_cast<std::uint32_t>(p.memory_base)},p.memory_bytes,sem::memory::storage::system,{true,true,true}}});
    if(!map)throw std::invalid_argument("overlapping/invalid Holon physical map");
    return *map;
}
HolonNpu::HolonNpu(const HolonNpuParams& p)
    : ClockedObject(p),port_(*this),stats_(this),system_(*p.system),requestor_(system_.getRequestorId(this)),
      machine_(addressMap(p),{.vector_bytes=p.vector_bytes}),
      timing_({.frontend_cycles=p.frontend_cycles,.vector_lanes=p.vector_lanes,.divide_cycles=p.divide_cycles,
          .sqrt_cycles=p.sqrt_cycles,.scratchpad_bytes_per_cycle=p.scratchpad_bytes_per_cycle}),
      advanceEvent_([this]{advance();},name()+".advance"),
      completionEvent_([this]{operationReady();},name()+".complete"),
      packetEvent_([this]{sendPacket();},name()+".packet"),
      responseEvent_([this]{consumeResponse();},name()+".response") {}
Port& HolonNpu::getPort(const std::string& name,PortID id) {
    if(name=="memory")return port_;
    return ClockedObject::getPort(name,id);
}
void HolonNpu::startup() {
    ClockedObject::startup();
    if(!port_.isConnected()||!system_.isTimingMode())throw std::runtime_error("Holon requires connected timing memory");
    const auto& p=static_cast<const HolonNpuParams&>(params());
    const auto code=readImage(p.program_file),data=readImage(p.data_file);
    if(data.size()>p.memory_bytes)throw std::runtime_error("initial data exceeds physical memory");
    system_.physProxy.writeBlob(p.memory_base,data.data(),data.size());
    if(!machine_.boot(code,sem::physical_address{codeBase},sem::instruction_address{codeBase}))throw std::runtime_error("invalid local program boot image");
    started_=accountedTick_=curTick();schedule(advanceEvent_,clockEdge(Cycles(1)));
}
void HolonNpu::account() {
    stats_.elapsedTicks[std::to_underlying(timeDomain_)]+=curTick()-accountedTick_;
    accountedTick_=curTick();
    const auto elapsed=(curTick()-started_)/clockPeriod();
    machine_.account_cycles(sem::scalar::elapsed_cycles{elapsed-accountedCycles_});
    accountedCycles_=elapsed;stats_.cycles=elapsed;stats_.retired=machine_.hart().retired();
}
void HolonNpu::advance() {
    account();
    const auto& p=static_cast<const HolonNpuParams&>(params());
    if(machine_.hart().retired()+traps_>=p.max_instructions){finish("budget",1);return;}
    auto event=machine_.advance();
    if(!event)throw std::runtime_error("semantic advance API failure");
    handle(*event);
}
void HolonNpu::handle(sem::execution_event event) {
    if(const auto* pending=std::get_if<sem::pending_operation>(&event)){issue(*pending);return;}
    if(const auto* stop=std::get_if<sem::stopped>(&event)){finish("stopped",stop->status);return;}
    if(std::holds_alternative<sem::scalar::sleeping>(event)){finish("waiting",1);return;}
    if(std::holds_alternative<sem::scalar::trap_taken>(event)){++traps_;++stats_.traps;}
    timeDomain_=holon_npu::gem5_model::resource::frontend;
    schedule(advanceEvent_,clockEdge(Cycles(1)));
}
void HolonNpu::issue(const sem::pending_operation& pending) {
    if(pending_||inflight_||retry_)throw std::logic_error("overlapping blocking request");
    pending_=pending;
    const auto cost=timing_.estimate(pending.value);
    timeDomain_=cost.unit;
    stats_.resourceCycles[std::to_underlying(cost.unit)]+=cost.cycles;
    stats_.activeLanes+=cost.active_lanes;stats_.laneSlots+=cost.lane_slots;
    stats_.matrixMacs+=cost.matrix_macs;stats_.localBytes+=cost.local_bytes;
    schedule(completionEvent_,clockEdge(Cycles(cost.cycles)));
}
void HolonNpu::operationReady() {
    account();
    const auto* memory=std::get_if<sem::memory_request>(&pending_->value);
    if(!memory||memory->storage!=sem::memory::storage::system){complete(sem::operation_success{});return;}
    buffer_=memory->access==sem::memory::access::write?memory->payload:std::vector<std::byte>(memory->size);
    transferred_=0;memoryStarted_=curTick();
    timeDomain_=holon_npu::gem5_model::resource::memory_wait;sendPacket();
}
void HolonNpu::sendPacket() {
    const auto& memory=std::get<sem::memory_request>(pending_->value);
    const Addr address=memory.address.value()+transferred_;
    const auto size=std::min<std::size_t>({buffer_.size()-transferred_,256,4096-(address%4096),system_.cacheLineSize()-(address%system_.cacheLineSize())});
    const auto request=std::make_shared<Request>(address,size,0,requestor_);
    retry_=std::make_unique<Packet>(request,memory.access==sem::memory::access::write?MemCmd::WriteReq:MemCmd::ReadReq);
    retry_->dataStatic(reinterpret_cast<std::uint8_t*>(buffer_.data()+transferred_));retryPacket();
}
void HolonNpu::retryPacket() {
    if(!retry_||inflight_)throw std::logic_error("unexpected timing retry");
    if(port_.sendTimingReq(retry_.get())) {
        inflight_=retry_.release();++stats_.transactions;stats_.bytes+=inflight_->getSize();
    }else ++stats_.retries;
}
bool HolonNpu::MemoryPort::recvTimingResp(PacketPtr packet){return owner_.response(packet);}
void HolonNpu::MemoryPort::recvReqRetry(){owner_.retryPacket();}
bool HolonNpu::response(PacketPtr packet) {
    if(packet!=inflight_||!pending_||response_)throw std::logic_error("unexpected memory response");
    response_.reset(packet);
    const auto delay=packet->headerDelay+packet->payloadDelay;
    packet->headerDelay=packet->payloadDelay=0;
    schedule(responseEvent_,clockEdge(Cycles(0))+delay);
    return true;
}
void HolonNpu::consumeResponse() {
    auto response=std::move(response_);
    const auto packet=response.get();inflight_=nullptr;account();
    if(packet->isError()) {
        stats_.memoryTicks+=curTick()-memoryStarted_;
        complete(sem::bus_fault{sem::physical_address{static_cast<std::uint32_t>(packet->getAddr())}});return;
    }
    if(packet->isRead())std::memmove(buffer_.data()+transferred_,packet->getConstPtr<std::uint8_t>(),packet->getSize());
    transferred_+=packet->getSize();
    if(transferred_<buffer_.size())schedule(packetEvent_,clockEdge(Cycles(1)));
    else {
        stats_.memoryTicks+=curTick()-memoryStarted_;
        const auto& memory=std::get<sem::memory_request>(pending_->value);
        if(memory.access==sem::memory::access::write)complete(sem::operation_success{});
        else complete(sem::read_payload{std::move(buffer_)});
    }
}
void HolonNpu::complete(sem::operation_result result) {
    auto event=machine_.complete(pending_->token,std::move(result));pending_.reset();
    if(!event)throw std::runtime_error("semantic completion API failure");
    handle(*event);
}
void HolonNpu::finish(std::string_view reason,unsigned status) {
    if(terminal_)throw std::logic_error("duplicate terminal outcome");
    terminal_=true;account();
    const auto& p=static_cast<const HolonNpuParams&>(params());
    std::vector<std::byte> memory(p.memory_bytes);system_.physProxy.readBlob(p.memory_base,memory.data(),memory.size());
    std::ofstream output(p.output_file,std::ios::binary);output.write(reinterpret_cast<const char*>(memory.data()),memory.size());
    std::ofstream report(p.report_file);
    report<<"{\"reason\":\""<<reason<<"\",\"status\":"<<status<<",\"pc\":"<<machine_.hart().pc().value()
        <<",\"retired\":"<<machine_.hart().retired()<<",\"traps\":"<<traps_<<",\"cycles\":"<<accountedCycles_
        <<",\"ticks\":"<<curTick()-started_<<"}\n";
    output.close();report.close();
    if(!output||!report)throw std::runtime_error("cannot write execution artifacts");
    exitSimLoop("Holon "+std::string(reason),status);
}
void HolonNpu::serialize(CheckpointOut&) const {throw std::runtime_error("autonomous checkpoint state is not implemented");}
void HolonNpu::unserialize(CheckpointIn&) {throw std::runtime_error("autonomous checkpoint restore is not implemented");}
}
