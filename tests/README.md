# tests

This directory contains host-side tests that are not tied to a single RTL
module wrapper.

- `driver_test.cpp` validates the public C driver, descriptor layout static
  checks, argument validation, submit/wait/status/error/clear/performance
  flows, and the shared ABI headers.
- `instruction_test.cpp` checks mixed-width framing and RV32 scalar decoding;
  `isa_schema_test.py` tests malformed metadata and the RTL generation boundary.
- `scalar_test.cpp` verifies all selected scalar effects with independent
  arithmetic scoreboards, captured memory requests, load extension, CSR enable
  matrices, precise exceptions and source/destination aliases. It does not
  substitute for full RV32 program-machine integration.
- `hart_test.cpp` verifies shared M-mode CSR/trap/interrupt state and precise
  memory/fence completion, independent CSR/register scoreboards and budget-safe
  architectural counters. It does not replace program-level boot/router tests.
- `scalar_toolchain_test.py` cross-checks decoding with upstream RISC-V tools
  in the gem5 preset. It generates assembly/ELF/disassembly artifacts, not a
  second ISA implementation or a program-execution reference.

RTL-oriented Verilator testbenches live in `sim/` and are registered with CTest
from the root `CMakeLists.txt`. Run the project test suite with:

```sh
ctest --preset debug --output-on-failure
```
