#include "holon_npu_device.hh"

#include "holon_npu_program.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

#include "base/logging.hh"
#include "mem/packet_access.hh"
#include "sim/serialize.hh"

namespace gem5 {
namespace {

constexpr std::size_t maximumDmaBytes = 256;
constexpr Addr pageBytes = 4096;

std::uint32_t lifecycleBit(holon_npu::semantic::lifecycle_state state) {
    using state_type = holon_npu::semantic::lifecycle_state;
    switch (state) {
        case state_type::idle: return HOLON_NPU_STATUS_IDLE;
        case state_type::loading: return HOLON_NPU_STATUS_LOADING;
        case state_type::running: return HOLON_NPU_STATUS_RUNNING;
        case state_type::halted: return HOLON_NPU_STATUS_HALTED;
        case state_type::resetting: return HOLON_NPU_STATUS_RESETTING;
        case state_type::done: return HOLON_NPU_STATUS_DONE;
        case state_type::fault: return HOLON_NPU_STATUS_FAULT;
    }
    return 0;
}

}  // namespace

HolonNpuDevice::device_stats::device_stats(statistics::Group* parent)
    : statistics::Group(parent, "holon"),
      ADD_STAT(activeCycles, statistics::units::Cycle::get(), "Active NPU cycles"),
      ADD_STAT(totalCycles, statistics::units::Cycle::get(), "Total NPU cycles including memory waits"),
      ADD_STAT(frontendCycles, statistics::units::Cycle::get(), "Frontend issue and scalar cycles"),
      ADD_STAT(dmaSetupCycles, statistics::units::Cycle::get(), "DMA setup cycles"),
      ADD_STAT(dmaWaitCycles, statistics::units::Cycle::get(), "Cycles waiting for gem5 memory responses"),
      ADD_STAT(vectorCycles, statistics::units::Cycle::get(), "Vector engine busy cycles"),
      ADD_STAT(matrixCycles, statistics::units::Cycle::get(), "Matrix engine busy cycles"),
      ADD_STAT(syncCycles, statistics::units::Cycle::get(), "Synchronization cycles"),
      ADD_STAT(retiredInstructions, statistics::units::Count::get(), "Retired Holon instructions"),
      ADD_STAT(dmaCommands, statistics::units::Count::get(), "Architectural DMA commands"),
      ADD_STAT(dmaTransactions, statistics::units::Count::get(), "gem5 DMA transactions"),
      ADD_STAT(dmaBytes, statistics::units::Byte::get(), "DMA payload bytes"),
      ADD_STAT(vectorOperations, statistics::units::Count::get(), "Vector operations"),
      ADD_STAT(vectorActiveLanes, statistics::units::Count::get(), "Active vector lanes"),
      ADD_STAT(vectorAvailableLanes, statistics::units::Count::get(), "Scheduled vector lanes"),
      ADD_STAT(matrixOperations, statistics::units::Count::get(), "Matrix operations"),
      ADD_STAT(matrixMacs, statistics::units::Count::get(), "Matrix MAC operations"),
      ADD_STAT(scratchpadReads, statistics::units::Count::get(), "Scratchpad read transactions"),
      ADD_STAT(scratchpadWrites, statistics::units::Count::get(), "Scratchpad write transactions"),
      ADD_STAT(syncOperations, statistics::units::Count::get(), "Synchronization operations"),
      ADD_STAT(irqCount, statistics::units::Count::get(), "Interrupt assertions"),
      ADD_STAT(faultCount, statistics::units::Count::get(), "Terminal faults"),
      ADD_STAT(resetCount, statistics::units::Count::get(), "Software resets"),
      ADD_STAT(completionCount, statistics::units::Count::get(), "Completion records") {}

HolonNpuDevice::HolonNpuDevice(const Params& parameters)
    : DmaDevice(parameters),
      pioAddr(parameters.pio_addr),
      pioSize(parameters.pio_size),
      pioDelay(parameters.pio_latency),
      platform(parameters.platform),
      interruptId(parameters.interrupt_id),
      deviceClockPeriod(parameters.device_clock),
      semanticDevice(HOLON_NPU_LOCAL_MEM_MAX_BYTES, parameters.vector_lanes),
      timing(holon_npu::gem5_model::timing_parameters{
          .frontend_cycles = static_cast<std::uint32_t>(parameters.frontend_cycles),
          .scalar_local_cycles = static_cast<std::uint32_t>(parameters.scalar_local_cycles),
          .vector_issue_cycles = static_cast<std::uint32_t>(parameters.vector_issue_cycles),
          .vector_lanes = static_cast<std::uint32_t>(parameters.vector_lanes),
          .quant_parameter_words = static_cast<std::uint32_t>(parameters.quant_parameter_words),
          .matrix_descriptor_words = static_cast<std::uint32_t>(parameters.matrix_descriptor_words),
          .matrix_tile_m = static_cast<std::uint32_t>(parameters.matrix_m),
          .matrix_array_k = static_cast<std::uint32_t>(parameters.matrix_k),
          .matrix_array_n = static_cast<std::uint32_t>(parameters.matrix_n),
          .matrix_validate_cycles = static_cast<std::uint32_t>(parameters.matrix_validate_cycles),
          .matrix_clear_cycles = static_cast<std::uint32_t>(parameters.matrix_clear_cycles),
          .matrix_drain_cycles = static_cast<std::uint32_t>(parameters.matrix_drain_cycles),
          .dma_setup_cycles = static_cast<std::uint32_t>(parameters.dma_setup_cycles),
          .sync_cycles = static_cast<std::uint32_t>(parameters.sync_cycles),
          .scratchpad_read_cycles = static_cast<std::uint32_t>(parameters.scratchpad_read_cycles),
          .scratchpad_write_cycles = static_cast<std::uint32_t>(parameters.scratchpad_write_cycles),
      }),
      kickEvent([this] { kick(); }, name() + ".kick"),
      dmaEvent([this] { dmaComplete(); }, name() + ".dma"),
      operationEvent([this] { operationComplete(); }, name() + ".operation"),
      stats(this) {
    fatal_if(pioSize != 0x1000, "HolonNPU requires a 4 KiB MMIO aperture");
    fatal_if(parameters.vector_lanes <= 0, "HolonNPU vector_lanes must be positive");
    fatal_if(parameters.matrix_m <= 0 || parameters.matrix_k <= 0 || parameters.matrix_n <= 0,
             "HolonNPU matrix dimensions must be positive");
}

AddrRangeList HolonNpuDevice::getAddrRanges() const {
    return {AddrRange(pioAddr, pioAddr + pioSize)};
}

Tick HolonNpuDevice::cyclesToTicks(std::uint64_t cycles) const {
    return std::max<Tick>(1, cycles * deviceClockPeriod);
}

std::uint64_t HolonNpuDevice::ticksToCycles(Tick ticks) const {
    return (ticks + deviceClockPeriod - 1) / deviceClockPeriod;
}

void HolonNpuDevice::reject(PacketPtr packet) const {
    packet->makeAtomicResponse();
    packet->setBadAddress();
}

Tick HolonNpuDevice::read(PacketPtr packet) {
    if (packet->getSize() != sizeof(std::uint32_t) ||
        packet->getAddr() < pioAddr || packet->getAddr() >= pioAddr + pioSize ||
        ((packet->getAddr() - pioAddr) & 3U) != 0U) {
        reject(packet);
        return pioDelay;
    }
    const auto value = readRegister(
        static_cast<std::uint32_t>(packet->getAddr() - pioAddr)
    );
    if (!value) {
        reject(packet);
        return pioDelay;
    }
    packet->makeAtomicResponse();
    packet->setLE(*value);
    return pioDelay;
}

Tick HolonNpuDevice::write(PacketPtr packet) {
    if (packet->getSize() != sizeof(std::uint32_t) ||
        packet->getAddr() < pioAddr || packet->getAddr() >= pioAddr + pioSize ||
        ((packet->getAddr() - pioAddr) & 3U) != 0U ||
        !writeRegister(
            static_cast<std::uint32_t>(packet->getAddr() - pioAddr),
            packet->getLE<std::uint32_t>()
        )) {
        reject(packet);
        return pioDelay;
    }
    packet->makeAtomicResponse();
    return pioDelay;
}

std::uint32_t HolonNpuDevice::statusRegister() const {
    return lifecycleBit(semanticDevice.state()) |
        (irqStatus != 0 ? HOLON_NPU_STATUS_IRQ_PENDING : 0U);
}

std::optional<std::uint32_t> HolonNpuDevice::readRegister(std::uint32_t offset) const {
    switch (offset) {
        case HOLON_NPU_REG_DEVICE_ID: return HOLON_NPU_RESET_DEVICE_ID;
        case HOLON_NPU_REG_ABI_VERSION: return HOLON_NPU_RESET_ABI_VERSION;
        case HOLON_NPU_REG_ISA_VERSION: return HOLON_NPU_RESET_ISA_VERSION;
        case HOLON_NPU_REG_CAP0_LO: return HOLON_NPU_RESET_CAP0_LO;
        case HOLON_NPU_REG_CAP0_HI: return HOLON_NPU_RESET_CAP0_HI;
        case HOLON_NPU_REG_OP_CLASS_LO: return HOLON_NPU_RESET_OP_CLASS_LO;
        case HOLON_NPU_REG_OP_CLASS_HI: return HOLON_NPU_RESET_OP_CLASS_HI;
        case HOLON_NPU_REG_PROGRAM_MEM_BYTES: return HOLON_NPU_RESET_PROGRAM_MEM_BYTES;
        case HOLON_NPU_REG_LOCAL_MEM_BYTES: return HOLON_NPU_RESET_LOCAL_MEM_BYTES;
        case HOLON_NPU_REG_VECTOR_CAP0: return HOLON_NPU_RESET_VECTOR_CAP0;
        case HOLON_NPU_REG_MATRIX_CAP0: return HOLON_NPU_RESET_MATRIX_CAP0;
        case HOLON_NPU_REG_STATUS: return statusRegister();
        case HOLON_NPU_REG_FAULT_CODE: return static_cast<std::uint32_t>(semanticDevice.fault());
        case HOLON_NPU_REG_DEBUG_PC: return semanticDevice.program().pc();
        case HOLON_NPU_REG_PROGRAM_DESC_ADDR_LO: return static_cast<std::uint32_t>(descriptorAddress);
        case HOLON_NPU_REG_PROGRAM_DESC_ADDR_HI: return static_cast<std::uint32_t>(descriptorAddress >> 32U);
        case HOLON_NPU_REG_IRQ_ENABLE: return irqEnable;
        case HOLON_NPU_REG_IRQ_STATUS: return irqStatus;
        case HOLON_NPU_REG_PERF_CYCLE_LO: return static_cast<std::uint32_t>(elapsedCycles);
        case HOLON_NPU_REG_PERF_CYCLE_HI: return static_cast<std::uint32_t>(elapsedCycles >> 32U);
        case HOLON_NPU_REG_PERF_INSTRET_LO: return static_cast<std::uint32_t>(semanticDevice.program().retired());
        case HOLON_NPU_REG_PERF_INSTRET_HI:
            return static_cast<std::uint32_t>(semanticDevice.program().retired() >> 32U);
        default: return std::nullopt;
    }
}

bool HolonNpuDevice::writeRegister(std::uint32_t offset, std::uint32_t value) {
    if (offset == HOLON_NPU_REG_PROGRAM_DESC_ADDR_LO &&
        semanticDevice.state() == holon_npu::semantic::lifecycle_state::idle) {
        descriptorAddress = (descriptorAddress & 0xFFFF'FFFF'0000'0000ULL) | value;
        return true;
    }
    if (offset == HOLON_NPU_REG_PROGRAM_DESC_ADDR_HI &&
        semanticDevice.state() == holon_npu::semantic::lifecycle_state::idle) {
        descriptorAddress = (descriptorAddress & 0xFFFF'FFFFULL) |
            (static_cast<std::uint64_t>(value) << 32U);
        return true;
    }
    if (offset == HOLON_NPU_REG_DOORBELL) {
        if (value != HOLON_NPU_DOORBELL_START ||
            !semanticDevice.submit(holon_npu::semantic::system_address{descriptorAddress})) {
            return false;
        }
        schedule(kickEvent, curTick() + cyclesToTicks(1));
        return true;
    }
    if (offset == HOLON_NPU_REG_CONTROL) {
        if (value == HOLON_NPU_CONTROL_SOFT_RESET) {
            if (!semanticDevice.soft_reset()) return false;
            stats.resetCount++;
            if (!kickEvent.scheduled()) schedule(kickEvent, curTick() + cyclesToTicks(1));
            return true;
        }
        if (value == HOLON_NPU_CONTROL_CLEAR_TERMINAL) {
            return semanticDevice.clear_terminal().has_value();
        }
        if (value == HOLON_NPU_CONTROL_HALT) return semanticDevice.halt().has_value();
        if (value == HOLON_NPU_CONTROL_RESUME) {
            const auto accepted = semanticDevice.resume().has_value();
            if (accepted && !kickEvent.scheduled()) schedule(kickEvent, curTick() + cyclesToTicks(1));
            return accepted;
        }
        if (value == HOLON_NPU_CONTROL_DEBUG_STEP) {
            const auto accepted = semanticDevice.debug_step().has_value();
            if (accepted && !kickEvent.scheduled()) schedule(kickEvent, curTick() + cyclesToTicks(1));
            return accepted;
        }
        return false;
    }
    if (offset == HOLON_NPU_REG_IRQ_ENABLE && (value & ~HOLON_NPU_IRQ_VALID_MASK) == 0) {
        irqEnable = value;
        updateInterrupt();
        return true;
    }
    if (offset == HOLON_NPU_REG_IRQ_CLEAR && (value & ~HOLON_NPU_IRQ_VALID_MASK) == 0) {
        irqStatus &= ~value;
        semanticDevice.clear_irq();
        updateInterrupt();
        return true;
    }
    return false;
}

void HolonNpuDevice::kick() {
    if (activeRequest || semanticDevice.state() == holon_npu::semantic::lifecycle_state::halted) return;
    const auto event = semanticDevice.advance();
    fatal_if(!event, "HolonNPU semantic advance protocol violation");
    handleSemanticEvent(*event);
}

void HolonNpuDevice::handleSemanticEvent(holon_npu::semantic::execution_event event) {
    if (const auto* pending = std::get_if<holon_npu::semantic::pending_operation>(&event)) {
        issueOperation(*pending);
        return;
    }
    if (std::holds_alternative<holon_npu::semantic::retired_event>(event)) {
        stats.retiredInstructions++;
        elapsedCycles++;
        stats.activeCycles++;
        stats.frontendCycles++;
        stats.totalCycles++;
        if (semanticDevice.state() != holon_npu::semantic::lifecycle_state::halted)
            schedule(kickEvent, curTick() + cyclesToTicks(1));
        return;
    }
    const auto terminal = std::get<holon_npu::semantic::terminal_event>(event);
    if (terminal.state == holon_npu::semantic::lifecycle_state::idle) {
        finishSoftwareReset();
        return;
    }
    if (terminal.state == holon_npu::semantic::lifecycle_state::done &&
        semanticDevice.irq_pending()) {
        irqStatus |= HOLON_NPU_IRQ_DONE;
    } else if (terminal.state == holon_npu::semantic::lifecycle_state::fault) {
        if (semanticDevice.irq_pending()) irqStatus |= HOLON_NPU_IRQ_FAULT;
        stats.faultCount++;
    }
    updateInterrupt();
    notifyDrainIfQuiescent();
}

void HolonNpuDevice::finishSoftwareReset() {
    descriptorAddress = 0;
    irqStatus = 0;
    elapsedCycles = 0;
    updateInterrupt();
    notifyDrainIfQuiescent();
}

void HolonNpuDevice::issueOperation(const holon_npu::semantic::pending_operation& request) {
    activeRequest = active_request{.operation = request, .issued_at = curTick()};
    auto& active = *activeRequest;
    std::visit(
        [this, &active](const auto& operation) {
            using operation_type = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<operation_type, holon_npu::semantic::descriptor_fetch> ||
                          std::same_as<operation_type, holon_npu::semantic::code_fetch> ||
                          std::same_as<operation_type, holon_npu::semantic::argument_fetch>) {
                active.memory_read = true;
                active.buffer.resize(operation.byte_count);
            } else if constexpr (std::same_as<operation_type, holon_npu::semantic::completion_record_write>) {
                active.memory_write = true;
                active.buffer.assign(operation.payload.begin(), operation.payload.end());
                holon_npu_completion_record_t record{};
                static_assert(sizeof(record) == HOLON_NPU_COMPLETION_RECORD_SIZE);
                std::memcpy(&record, active.buffer.data(), sizeof(record));
                record.cycle_count = elapsedCycles;
                std::memcpy(active.buffer.data(), &record, sizeof(record));
            } else if constexpr (std::same_as<operation_type, holon_npu::semantic::program_dma_operation>) {
                active.memory_read = operation.direction == holon_npu::semantic::dma_direction::system_to_local;
                active.memory_write = !active.memory_read;
                active.buffer = active.memory_read
                    ? std::vector<std::byte>(operation.byte_count)
                    : operation.store_payload;
            }
        },
        request.value
    );

    const auto estimate = timing.estimate(request.value);
    active.modeled_cycles = estimate.cycles;
    elapsedCycles += estimate.cycles;
    stats.activeCycles += estimate.cycles;
    stats.totalCycles += estimate.cycles;
    stats.vectorActiveLanes += estimate.active_lanes;
    stats.vectorAvailableLanes += estimate.available_lanes;
    stats.matrixMacs += estimate.matrix_macs;
    stats.scratchpadReads += estimate.scratchpad_reads;
    stats.scratchpadWrites += estimate.scratchpad_writes;
    if (std::holds_alternative<holon_npu::semantic::vector_operation>(request.value)) {
        stats.vectorOperations++;
        stats.vectorCycles += estimate.cycles;
    } else if (std::holds_alternative<holon_npu::semantic::matrix_operation>(request.value)) {
        stats.matrixOperations++;
        stats.matrixCycles += estimate.cycles;
    } else if (std::holds_alternative<holon_npu::semantic::sync_operation>(request.value)) {
        stats.syncOperations++;
        stats.syncCycles += estimate.cycles;
    } else if (std::holds_alternative<holon_npu::semantic::program_dma_operation>(request.value)) {
        stats.dmaCommands++;
        stats.dmaSetupCycles += estimate.cycles;
    } else if (active.memory_read || active.memory_write) {
        stats.dmaSetupCycles += estimate.cycles;
    } else {
        stats.frontendCycles += estimate.cycles;
    }
    if (std::holds_alternative<holon_npu::semantic::completion_record_write>(request.value)) stats.completionCount++;

    if (active.memory_read || active.memory_write) issueNextDmaChunk();
    else schedule(operationEvent, curTick() + cyclesToTicks(estimate.cycles));
}

void HolonNpuDevice::issueNextDmaChunk() {
    auto& active = *activeRequest;
    Addr address = 0;
    std::visit(
        [&address](const auto& operation) {
            using operation_type = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<operation_type, holon_npu::semantic::descriptor_fetch> ||
                          std::same_as<operation_type, holon_npu::semantic::code_fetch> ||
                          std::same_as<operation_type, holon_npu::semantic::argument_fetch>)
                address = operation.address.value();
            else if constexpr (std::same_as<operation_type, holon_npu::semantic::completion_record_write>)
                address = operation.address.value();
            else if constexpr (std::same_as<operation_type, holon_npu::semantic::program_dma_operation>)
                address = operation.system.value();
        },
        active.operation.value
    );
    address += active.transferred;
    const auto remaining = active.buffer.size() - active.transferred;
    const auto pageRemaining = static_cast<std::size_t>(pageBytes - (address & (pageBytes - 1U)));
    const auto bytes = std::min({remaining, pageRemaining, maximumDmaBytes});
    fatal_if(bytes == 0, "HolonNPU attempted a zero-byte DMA transaction");
    auto* data = reinterpret_cast<std::uint8_t*>(active.buffer.data() + active.transferred);
    stats.dmaTransactions++;
    stats.dmaBytes += bytes;
    const auto completion_delay = active.transferred == 0
        ? cyclesToTicks(active.modeled_cycles)
        : Tick{0};
    if (active.memory_read) dmaRead(address, bytes, &dmaEvent, data, completion_delay);
    else dmaWrite(address, bytes, &dmaEvent, data, completion_delay);
}

void HolonNpuDevice::dmaComplete() {
    auto& active = *activeRequest;
    Addr address = 0;
    std::visit([&address](const auto& operation) {
        using operation_type = std::remove_cvref_t<decltype(operation)>;
        if constexpr (requires { operation.address; }) address = operation.address.value();
        else if constexpr (std::same_as<operation_type, holon_npu::semantic::program_dma_operation>)
            address = operation.system.value();
    }, active.operation.value);
    address += active.transferred;
    const auto remaining = active.buffer.size() - active.transferred;
    const auto pageRemaining = static_cast<std::size_t>(pageBytes - (address & (pageBytes - 1U)));
    active.transferred += std::min({remaining, pageRemaining, maximumDmaBytes});
    if (active.transferred != active.buffer.size()) issueNextDmaChunk();
    else completeSemantic(active.memory_read
        ? holon_npu::semantic::operation_result{holon_npu::semantic::read_payload{active.buffer}}
        : holon_npu::semantic::operation_result{holon_npu::semantic::operation_success{}});
}

void HolonNpuDevice::operationComplete() {
    completeSemantic(holon_npu::semantic::operation_success{});
}

void HolonNpuDevice::completeSemantic(holon_npu::semantic::operation_result result) {
    if (activeRequest->memory_read || activeRequest->memory_write) {
        const auto serviceCycles = ticksToCycles(curTick() - activeRequest->issued_at);
        const auto waitCycles = serviceCycles > activeRequest->modeled_cycles
            ? serviceCycles - activeRequest->modeled_cycles
            : 0;
        elapsedCycles += waitCycles;
        stats.dmaWaitCycles += waitCycles;
        stats.totalCycles += waitCycles;
    }
    const auto token = activeRequest->operation.token;
    activeRequest.reset();
    const auto completed = semanticDevice.complete(token, std::move(result));
    fatal_if(!completed, "HolonNPU semantic completion protocol violation");
    handleSemanticEvent(*completed);
}

void HolonNpuDevice::updateInterrupt() {
    const auto asserted = (irqStatus & irqEnable) != 0;
    if (asserted && !interruptAsserted) {
        platform->postPciInt(interruptId);
        interruptAsserted = true;
        stats.irqCount++;
    } else if (!asserted && interruptAsserted) {
        platform->clearPciInt(interruptId);
        interruptAsserted = false;
    }
}

bool HolonNpuDevice::isQuiescent() const {
    return !activeRequest && !dmaPending() && !kickEvent.scheduled() &&
        !dmaEvent.scheduled() && !operationEvent.scheduled();
}

void HolonNpuDevice::notifyDrainIfQuiescent() {
    if (drainState() == DrainState::Draining && isQuiescent()) {
        signalDrainDone();
    }
}

DrainState HolonNpuDevice::drain() {
    return isQuiescent() ? DrainState::Drained : DrainState::Draining;
}

void HolonNpuDevice::serialize(CheckpointOut& checkpoint) const {
    fatal_if(
        semanticDevice.state() != holon_npu::semantic::lifecycle_state::idle || !isQuiescent(),
        "HolonNPU checkpoints require an idle, quiescent device"
    );
    paramOut(checkpoint, "descriptorAddress", descriptorAddress);
    paramOut(checkpoint, "irqEnable", irqEnable);
    paramOut(checkpoint, "irqStatus", irqStatus);
    paramOut(checkpoint, "interruptAsserted", interruptAsserted);
    paramOut(checkpoint, "elapsedCycles", elapsedCycles);
}

void HolonNpuDevice::unserialize(CheckpointIn& checkpoint) {
    semanticDevice.reset();
    paramIn(checkpoint, "descriptorAddress", descriptorAddress);
    paramIn(checkpoint, "irqEnable", irqEnable);
    paramIn(checkpoint, "irqStatus", irqStatus);
    paramIn(checkpoint, "interruptAsserted", interruptAsserted);
    paramIn(checkpoint, "elapsedCycles", elapsedCycles);
    updateInterrupt();
}

}  // namespace gem5
