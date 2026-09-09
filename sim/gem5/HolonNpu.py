from m5.objects.ClockedObject import ClockedObject
from m5.params import Param, RequestPort
from m5.proxy import Parent


class HolonNpu(ClockedObject):
    type = "HolonNpu"
    cxx_header = "holon_npu.hh"
    cxx_class = "gem5::HolonNpu"
    system = Param.System(Parent.any, "Memory-system owner, not a Host CPU")
    memory = RequestPort("Autonomous instruction/data timing requests")
    program_file = Param.String("Local mixed-width boot image")
    data_file = Param.String("Initial external memory image")
    output_file = Param.String("Final external memory observation")
    report_file = Param.String("Execution outcome and accounting")
    memory_base = Param.Addr(0x80000000, "External physical memory base")
    memory_bytes = Param.Unsigned(65536, "External memory capacity")
    vector_bytes = Param.Unsigned(64, "Architectural vector capacity")
    vector_lanes = Param.Unsigned(16, "Physical vector lanes")
    frontend_cycles = Param.Unsigned(1, "Fetch/issue cycles")
    scratchpad_bytes_per_cycle = Param.Unsigned(4, "Blocking local memory service bandwidth")
    divide_cycles = Param.Unsigned(12, "Vector divide group latency")
    sqrt_cycles = Param.Unsigned(16, "Vector square-root group latency")
    max_instructions = Param.Unsigned(100000, "Retirement plus trap-entry budget")
