# Public Headers

`holon_npu_runtime.hpp` exposes typed program construction for the current
RV32/Holon contract. Semantic headers are exported by the CMake semantic target
from `sim/semantic/`; consumers inherit C++26 and header search paths.
There is no descriptor/MMIO C API in the 3.0 research mainline.
