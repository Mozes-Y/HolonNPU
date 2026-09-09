#pragma once
#include "holon_npu_semantic.hpp"
#include "holon_npu_timing.hpp"
#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/HolonNpu.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"

namespace gem5 {
class HolonNpu final : public ClockedObject {
public:
    explicit HolonNpu(const HolonNpuParams& parameters);
    Port& getPort(const std::string& name, PortID id=InvalidPortID) override;
    void startup() override;
    void serialize(CheckpointOut&) const override;
    void unserialize(CheckpointIn&) override;
private:
    class MemoryPort final : public RequestPort {
    public:
        explicit MemoryPort(HolonNpu& owner) : RequestPort(owner.name()+".memory"), owner_(owner) {}
        bool recvTimingResp(PacketPtr packet) override;
        void recvReqRetry() override;
    private:
        HolonNpu& owner_;
    } port_;
    struct counters : statistics::Group {
        explicit counters(statistics::Group* parent);
        statistics::Scalar cycles, retired, traps, transactions, bytes, retries;
        statistics::Scalar memoryTicks, activeLanes, laneSlots, matrixMacs, localBytes;
        statistics::Vector resourceCycles;
        statistics::Vector elapsedTicks;
    } stats_;
    System& system_;
    RequestorID requestor_;
    holon_npu::semantic::program_machine machine_;
    holon_npu::gem5_model::timing_model timing_;
    std::optional<holon_npu::semantic::pending_operation> pending_;
    std::vector<std::byte> buffer_;
    std::size_t transferred_{};
    std::unique_ptr<Packet> retry_;
    std::unique_ptr<Packet> response_;
    PacketPtr inflight_{};
    EventFunctionWrapper advanceEvent_, completionEvent_, packetEvent_, responseEvent_;
    Tick started_{}, memoryStarted_{};
    Tick accountedTick_{};
    holon_npu::gem5_model::resource timeDomain_{};
    std::uint64_t accountedCycles_{}, traps_{};
    bool terminal_{};
    static holon_npu::semantic::memory::physical_map addressMap(const HolonNpuParams&);
    void advance();
    void issue(const holon_npu::semantic::pending_operation&);
    void operationReady();
    void sendPacket();
    void retryPacket();
    bool response(PacketPtr);
    void consumeResponse();
    void complete(holon_npu::semantic::operation_result);
    void handle(holon_npu::semantic::execution_event);
    void account();
    void finish(std::string_view reason, unsigned status);
};
}
