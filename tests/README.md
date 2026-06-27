# tests

This directory contains host-side tests that are not tied to a single RTL
module wrapper.

- `driver_test.cpp` validates the public C driver, descriptor layout static
  checks, argument validation, submit/wait/status/error/clear/performance
  flows, and the shared ABI headers.

RTL-oriented Verilator testbenches live in `sim/` and are registered with CTest
from the root `CMakeLists.txt`. Run the project test suite with:

```sh
ctest --preset debug --output-on-failure
```
