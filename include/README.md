# include

Public host and firmware contracts live here.

- `holon_npu_program.h`: generated ABI 3.0 registers, program descriptor,
  completion record, capabilities, lifecycle, IRQ, and fault constants.
- `holon_npu_isa.h`: generated Holon ISA 1.0 encoding metadata and helpers.
- `holon_npu_runtime.hpp`: C++26 typed program construction API.

Generated headers are owned by the schemas under `spec/` and must not be edited
directly.
