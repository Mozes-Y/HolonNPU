import argparse
import pathlib
import sys

import m5
from m5.objects import (
    AddrRange,
    Bridge,
    DDR3_1600_8x8,
    HiFive,
    HolonNpuDevice,
    IOXBar,
    MemCtrl,
    PMAChecker,
    RiscvBareMetal,
    RiscvRTC,
    RiscvTimingSimpleCPU,
    RiscvSystem,
    Root,
    SrcClockDomain,
    SystemXBar,
    VoltageDomain,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    args = parser.parse_args()

    system = RiscvSystem()
    system.mem_mode = "timing"
    system.mem_ranges = [AddrRange(0x80000000, size="128MiB")]
    system.cache_line_size = 64
    system.voltage_domain = VoltageDomain()
    system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=system.voltage_domain)
    system.membus = SystemXBar()
    system.iobus = IOXBar()
    system.system_port = system.membus.cpu_side_ports

    system.workload = RiscvBareMetal()
    system.workload.bootloader = str(args.binary.resolve())
    system.platform = HiFive()
    system.platform.setNumCores(1)
    system.platform.rtc = RiscvRTC(frequency="100MHz")
    system.platform.clint.int_pin = system.platform.rtc.int_pin
    system.platform.attachOnChipIO(system.membus)
    system.platform.attachOffChipIO(system.iobus)
    system.platform.attachPlic()

    system.iobus.mem_side_ports = system.platform.pci_host.up_response_port()
    system.iobus.cpu_side_ports = system.platform.pci_host.up_request_port()
    system.platform.pci_bus.cpu_side_ports = (
        system.platform.pci_host.down_request_port()
    )
    system.platform.pci_bus.default = system.platform.pci_host.down_response_port()
    system.platform.pci_bus.config_error_port = (
        system.platform.pci_host.config_error.pio
    )

    system.bridge = Bridge(
        delay="50ns",
        ranges=[*system.platform._off_chip_ranges(), AddrRange(0x10010000, size=0x1000)],
    )
    system.bridge.cpu_side_port = system.membus.mem_side_ports
    system.bridge.mem_side_port = system.iobus.cpu_side_ports
    system.dma_bridge = Bridge(delay="50ns", ranges=system.mem_ranges)
    system.dma_bridge.cpu_side_port = system.iobus.mem_side_ports
    system.dma_bridge.mem_side_port = system.membus.cpu_side_ports

    system.holon = HolonNpuDevice(platform=system.platform)
    system.holon.pio = system.iobus.mem_side_ports
    system.holon.dma = system.iobus.cpu_side_ports
    system.platform.plic.n_src = max(int(system.platform.plic.n_src), 0x21)

    system.cpu = RiscvTimingSimpleCPU(cpu_id=0)
    system.cpu.createThreads()
    system.cpu.createInterruptController()
    system.cpu.icache_port = system.membus.cpu_side_ports
    system.cpu.dcache_port = system.membus.cpu_side_ports
    system.cpu.mmu.pma_checker = PMAChecker(
        uncacheable=[*system.platform._on_chip_ranges(),
                     *system.platform._off_chip_ranges(),
                     AddrRange(0x10010000, size=0x1000)]
    )

    system.mem_ctrl = MemCtrl()
    system.mem_ctrl.dram = DDR3_1600_8x8(range=system.mem_ranges[0])
    system.mem_ctrl.port = system.membus.mem_side_ports

    Root(full_system=True, system=system)
    m5.instantiate()
    event = m5.simulate()
    print(
        f"HolonNPU bare-metal exit: {event.getCause()} "
        f"code {event.getCode()} at tick {m5.curTick()}"
    )
    return 0 if event.getCode() == 0 else 1


if __name__ == "__m5_main__":
    sys.exit(main())
