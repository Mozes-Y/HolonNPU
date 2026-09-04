from m5.objects.Device import DmaDevice
from m5.params import Addr, Clock, Int, Latency, Param
from m5.proxy import Parent
from m5.util.fdthelper import FdtProperty, FdtPropertyWords


class HolonNpuDevice(DmaDevice):
    type = "HolonNpuDevice"
    cxx_header = "holon_npu_device.hh"
    cxx_class = "gem5::HolonNpuDevice"

    pio_addr = Param.Addr(0x10010000, "HolonNPU MMIO base")
    pio_size = Param.Addr(0x1000, "HolonNPU MMIO aperture")
    pio_latency = Param.Latency("10ns", "MMIO response latency")
    platform = Param.Platform(Parent.any, "RISC-V interrupt platform")
    interrupt_id = Param.Int(0x20, "PLIC interrupt source")
    device_clock = Param.Clock("1GHz", "HolonNPU timing clock")
    vector_lanes = Param.Int(16, "Implemented vector lanes")
    matrix_m = Param.Int(16, "Matrix output rows per micro-op")
    matrix_k = Param.Int(16, "Matrix K lanes")
    matrix_n = Param.Int(16, "Matrix N lanes")
    frontend_cycles = Param.Int(1, "Frontend operation cycles")
    scalar_local_cycles = Param.Int(2, "Scalar local-memory operation cycles")
    vector_issue_cycles = Param.Int(1, "Vector issue-to-execution cycles")
    quant_parameter_words = Param.Int(6, "Quantization parameter words")
    matrix_descriptor_words = Param.Int(8, "Matrix command words")
    matrix_validate_cycles = Param.Int(1, "Matrix command validation cycles")
    matrix_clear_cycles = Param.Int(1, "Matrix accumulator clear cycles")
    matrix_drain_cycles = Param.Int(1, "Matrix array drain transition cycles")
    dma_setup_cycles = Param.Int(4, "Architectural DMA setup cycles")
    sync_cycles = Param.Int(1, "Synchronization operation cycles")
    scratchpad_read_cycles = Param.Int(2, "Scratchpad read request/response cycles")
    scratchpad_write_cycles = Param.Int(2, "Scratchpad write request/response cycles")

    def generateDeviceTree(self, state):
        node = self.generateBasicPioDeviceNode(
            state, "holon-npu", self.pio_addr, self.pio_size
        )
        plic = self.platform.unproxy(self).plic
        node.appendCompatible(["holonnpu,sim-3.0"])
        node.append(FdtPropertyWords("interrupts", [self.interrupt_id]))
        node.append(FdtPropertyWords("interrupt-parent", state.phandle(plic)))
        node.append(FdtProperty("dma-noncoherent"))
        yield node
