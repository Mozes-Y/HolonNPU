# Program Construction

`holon_npu_runtime.cpp` implements the typed byte-oriented program builder.
It uses the shared codec, not a second interpreter. Guest freestanding support
is under `sim/guest/`. A graph/task compiler and serving runtime are future work.
