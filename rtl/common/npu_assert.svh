// HolonNPU assertion and functional cover macros.
//
// Assertions and coverpoints are compiled only when the corresponding guard is
// passed to Verilator/CMake. The empty fallback keeps synthesis and quick lint
// flows free of verification-only behavior.
`ifndef HOLON_NPU_ASSERT_SVH
`define HOLON_NPU_ASSERT_SVH

`ifdef HOLON_NPU_ASSERT_ON
`define HOLON_NPU_ASSERT(NAME, PROPERTY) NAME: assert property (PROPERTY);
`else
`define HOLON_NPU_ASSERT(NAME, PROPERTY)
`endif

`ifdef HOLON_NPU_COVER_ON
`define HOLON_NPU_COVER(NAME, PROPERTY) NAME: cover property (PROPERTY);
`else
`define HOLON_NPU_COVER(NAME, PROPERTY)
`endif

`endif
