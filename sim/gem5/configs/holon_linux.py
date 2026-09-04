import argparse
import json
import pathlib
import re
import subprocess

from m5.objects import AddrRange, HolonNpuDevice
from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_walk_cache_hierarchy import (
    PrivateL1PrivateL2WalkCacheHierarchy,
)
from gem5.components.memory.simple import SingleChannelSimpleMemory
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import (
    BootloaderResource,
    DiskImageResource,
    KernelResource,
)
from gem5.simulate.simulator import Simulator


device_tree_compiler = None
device_tree_overlay = None


class HolonRiscvBoard(RiscvBoard):
    def _setup_board(self):
        super()._setup_board()
        if self.is_fullsystem():
            self.holon = HolonNpuDevice(platform=self.platform)
            self._off_chip_devices.append(self.holon)
            self.platform.plic.n_src = max(int(self.platform.plic.n_src), 0x21)

    def _setup_io_devices(self):
        super()._setup_io_devices()
        self.holon.dma = self.iobus.cpu_side_ports

    def generate_device_tree(self, outdir):
        super().generate_device_tree(outdir)
        output = pathlib.Path(outdir)
        dts = (output / "device.dts").read_text(encoding="utf-8")
        match = re.search(
            r"plic@[0-9a-fA-F]+\s*\{.*?phandle\s*=\s*<(0x[0-9a-fA-F]+|[0-9]+)>;",
            dts,
            re.DOTALL,
        )
        if match is None:
            raise RuntimeError("could not locate the PLIC phandle in gem5's generated DTS")
        plic_phandle = match.group(1)
        overlay_source = output / "holon-npu-overlay.dts"
        overlay_binary = output / "holon-npu-overlay.dtbo"
        patched = output / "device-with-holon.dtb"
        overlay_source.write_text(
            f"""/dts-v1/;
/plugin/;

/ {{
    fragment@0 {{
        target-path = \"/soc\";
        __overlay__ {{
            holon-npu@10010000 {{
                compatible = \"holonnpu,sim-3.0\";
                reg = <0x0 0x10010000 0x0 0x1000>;
                interrupt-parent = <{plic_phandle}>;
                interrupts = <0x20>;
                dma-noncoherent;
            }};
        }};
    }};
}};
""",
            encoding="utf-8",
        )
        # Standalone overlays cannot resolve parent cells or external phandles;
        # fdtoverlay validates them against the complete base tree below.
        subprocess.run(
            [
                device_tree_compiler,
                "-@",
                "-Wno-reg_format",
                "-Wno-avoid_default_addr_size",
                "-Wno-interrupts_property",
                "-I",
                "dts",
                "-O",
                "dtb",
                "-o",
                str(overlay_binary),
                str(overlay_source),
            ],
            check=True,
        )
        subprocess.run(
            [
                device_tree_overlay,
                "-i",
                str(output / "device.dtb"),
                "-o",
                str(patched),
                str(overlay_binary),
            ],
            check=True,
        )
        patched.replace(output / "device.dtb")


def locked_resource(lock, name, resource_directory):
    entry = lock["resources"][name]
    common = {
        "local_path": str(pathlib.Path(resource_directory) / entry["id"]),
        "id": entry["id"],
        "resource_version": entry["resource_version"],
        "architecture": entry["architecture"],
    }
    if name == "bootloader":
        return BootloaderResource(**common)
    if name == "kernel":
        return KernelResource(**common)
    return DiskImageResource(
        local_path=common["local_path"],
        id=common["id"],
        resource_version=common["resource_version"],
        root_partition=entry["root_partition"],
    )


def main():
    global device_tree_compiler, device_tree_overlay
    parser = argparse.ArgumentParser()
    parser.add_argument("--resource-directory", required=True)
    parser.add_argument("--resource-lock", type=pathlib.Path, required=True)
    parser.add_argument("--guest-bundle", type=pathlib.Path, required=True)
    parser.add_argument("--workload-id", required=True)
    parser.add_argument("--workload-version", required=True)
    parser.add_argument("--dtc", required=True)
    parser.add_argument("--fdtoverlay", required=True)
    args = parser.parse_args()
    lock = json.loads(args.resource_lock.read_text(encoding="utf-8"))
    workload = lock["workload"]
    if (args.workload_id, args.workload_version) != (
        workload["id"], workload["resource_version"]
    ):
        raise RuntimeError("requested workload does not match resources.lock.json")

    board = HolonRiscvBoard(
        clk_freq="1GHz",
        processor=SimpleProcessor(
            # Linux is a functional system gate; NPU timing is modeled by the
            # device and calibrated independently of host boot execution.
            cpu_type=CPUTypes.ATOMIC, isa=ISA.RISCV, num_cores=1
        ),
        memory=SingleChannelSimpleMemory(
            latency="30ns",
            latency_var="0ns",
            bandwidth="12.8GiB/s",
            size="3GiB",
        ),
        cache_hierarchy=PrivateL1PrivateL2WalkCacheHierarchy(
            l1d_size="16KiB", l1i_size="16KiB", l2_size="256KiB"
        ),
    )
    device_tree_compiler = args.dtc
    device_tree_overlay = args.fdtoverlay
    board.set_kernel_disk_workload(
        kernel=locked_resource(lock, "kernel", args.resource_directory),
        disk_image=locked_resource(lock, "disk_image", args.resource_directory),
        bootloader=locked_resource(lock, "bootloader", args.resource_directory),
        readfile=args.guest_bundle,
        kernel_args=[
            "earlycon=sbi",
            "console=ttyS0",
            "root={root_value}",
            "disk_device={disk_device}",
            "rw",
            "no_systemd=true",
        ],
    )
    Simulator(board=board).run()


if __name__ == "__m5_main__":
    main()
