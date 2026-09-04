#pragma once

#include "holon_npu_semantic.hpp"
#include "holon_npu_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "base/statistics.hh"
#include "dev/dma_device.hh"
#include "dev/platform.hh"
#include "params/HolonNpuDevice.hh"

namespace gem5 {

class HolonNpuDevice final : public DmaDevice {
public:
    using Params = HolonNpuDeviceParams;

    explicit HolonNpuDevice(const Params& parameters);

    AddrRangeList getAddrRanges() const override;
    Tick read(PacketPtr packet) override;
    Tick write(PacketPtr packet) override;
    DrainState drain() override;
    void serialize(CheckpointOut& checkpoint) const override;
    void unserialize(CheckpointIn& checkpoint) override;

private:
    struct device_stats : statistics::Group {
        explicit device_stats(statistics::Group* parent);

        statistics::Scalar activeCycles;
        statistics::Scalar totalCycles;
        statistics::Scalar frontendCycles;
        statistics::Scalar dmaSetupCycles;
        statistics::Scalar dmaWaitCycles;
        statistics::Scalar vectorCycles;
        statistics::Scalar matrixCycles;
        statistics::Scalar syncCycles;
        statistics::Scalar retiredInstructions;
        statistics::Scalar dmaCommands;
        statistics::Scalar dmaTransactions;
        statistics::Scalar dmaBytes;
        statistics::Scalar vectorOperations;
        statistics::Scalar vectorActiveLanes;
        statistics::Scalar vectorAvailableLanes;
        statistics::Scalar matrixOperations;
        statistics::Scalar matrixMacs;
        statistics::Scalar scratchpadReads;
        statistics::Scalar scratchpadWrites;
        statistics::Scalar syncOperations;
        statistics::Scalar irqCount;
        statistics::Scalar faultCount;
        statistics::Scalar resetCount;
        statistics::Scalar completionCount;
    };

    struct active_request {
        holon_npu::semantic::pending_operation operation{};
        std::vector<std::byte> buffer{};
        std::size_t transferred = 0;
        bool memory_read = false;
        bool memory_write = false;
        Tick issued_at = 0;
        std::uint64_t modeled_cycles = 0;
    };

    void kick();
    void handleSemanticEvent(holon_npu::semantic::execution_event event);
    void issueOperation(const holon_npu::semantic::pending_operation& operation);
    void issueNextDmaChunk();
    void dmaComplete();
    void operationComplete();
    void completeSemantic(holon_npu::semantic::operation_result result);
    void finishSoftwareReset();
    void updateInterrupt();
    void notifyDrainIfQuiescent();
    bool isQuiescent() const;
    void reject(PacketPtr packet) const;
    std::optional<std::uint32_t> readRegister(std::uint32_t offset) const;
    bool writeRegister(std::uint32_t offset, std::uint32_t value);
    std::uint32_t statusRegister() const;
    Tick cyclesToTicks(std::uint64_t cycles) const;
    std::uint64_t ticksToCycles(Tick ticks) const;

    Addr pioAddr;
    Addr pioSize;
    Tick pioDelay;
    Platform* platform;
    int interruptId;
    Tick deviceClockPeriod;
    holon_npu::semantic::device semanticDevice;
    holon_npu::gem5_model::timing_model timing;
    std::optional<active_request> activeRequest;
    EventFunctionWrapper kickEvent;
    EventFunctionWrapper dmaEvent;
    EventFunctionWrapper operationEvent;
    std::uint64_t descriptorAddress = 0;
    std::uint32_t irqEnable = 0;
    std::uint32_t irqStatus = 0;
    std::uint64_t elapsedCycles = 0;
    bool interruptAsserted = false;
    device_stats stats;
};

}  // namespace gem5
