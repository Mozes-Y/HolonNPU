cmake_minimum_required(VERSION 4.0)

project(my_npu LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(verilator REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(NPU_VERILATOR_ARGS -Wall --output-split 100000000 --output-split-cfuncs 100000000)

set(NPU_COMMON_MODULE_SOURCES
  rtl/common/npu_pkg.sv
  rtl/common/npu_fifo.sv
  rtl/common/npu_skid_buffer.sv
  rtl/common/npu_register_slice.sv
  rtl/common/npu_common_smoke_top.sv
)

set(NPU_COMMON_INTERFACE_SOURCES
  rtl/common/npu_vr_if.sv
  rtl/common/npu_axi_lite_if.sv
  rtl/common/npu_axi4_if.sv
)

set(NPU_COMMON_LINT_SOURCES
  ${NPU_COMMON_INTERFACE_SOURCES}
  ${NPU_COMMON_MODULE_SOURCES}
)

set(NPU_MATRIX_MODULE_SOURCES
  rtl/matrix/npu_pe_i8.sv
  rtl/matrix/npu_systolic_array.sv
)

set(NPU_MATRIX_TEST_SOURCES
  ${NPU_MATRIX_MODULE_SOURCES}
  rtl/matrix/npu_systolic_array_test_top.sv
)

set(NPU_DATAPATH_SOURCES
  ${NPU_MATRIX_MODULE_SOURCES}
  rtl/datapath/npu_tile_mask.sv
  rtl/datapath/npu_banked_scratchpad.sv
  rtl/datapath/npu_i8_tile_buffer.sv
  rtl/datapath/npu_c_accum_buffer.sv
  rtl/datapath/npu_gemm_tile_scratchpad.sv
  rtl/datapath/npu_ping_pong_ctrl.sv
  rtl/datapath/npu_tiling_datapath_test_top.sv
)

set(NPU_CONTROL_SOURCES
  rtl/common/npu_pkg.sv
  rtl/control/npu_control_regs.sv
)

set(NPU_DMA_MODULE_SOURCES
  rtl/common/npu_pkg.sv
  rtl/dma/npu_axi4_read_dma.sv
  rtl/dma/npu_axi4_write_dma.sv
)

set(NPU_READ_DMA_TEST_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  rtl/dma/npu_read_dma_test_top.sv
)

set(NPU_WRITE_DMA_TEST_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  rtl/dma/npu_write_dma_test_top.sv
)

set(NPU_DMA_LINT_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  rtl/dma/npu_read_dma_test_top.sv
  rtl/dma/npu_write_dma_test_top.sv
)

set(NPU_COMMAND_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  rtl/command/npu_command_processor.sv
)

set(NPU_COMMAND_TEST_SOURCES
  ${NPU_COMMAND_SOURCES}
  rtl/command/npu_command_processor_test_top.sv
)

set(NPU_GEMM_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  ${NPU_MATRIX_MODULE_SOURCES}
  rtl/datapath/npu_gemm_tile_scratchpad.sv
  rtl/integration/npu_gemm_accelerator.sv
)

set(NPU_TOP_SOURCES
  rtl/common/npu_pkg.sv
  rtl/dma/npu_axi4_read_dma.sv
  rtl/dma/npu_axi4_write_dma.sv
  rtl/matrix/npu_pe_i8.sv
  rtl/matrix/npu_systolic_array.sv
  rtl/datapath/npu_gemm_tile_scratchpad.sv
  rtl/control/npu_control_regs.sv
  rtl/command/npu_command_processor.sv
  rtl/integration/npu_gemm_accelerator.sv
  rtl/integration/npu_top.sv
)

set(NPU_INTEGRATION_SOURCES
  ${NPU_TOP_SOURCES}
)

function(my_npu_target_options target_name)
  target_compile_features(${target_name} PRIVATE cxx_std_26)
  target_compile_options(
    ${target_name}
    PRIVATE
      $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-deprecated-variadic-comma-omission>
  )
endfunction()

add_library(my_npu_driver sw/my_npu_driver.c)
target_compile_features(my_npu_driver PRIVATE c_std_11)
target_include_directories(
  my_npu_driver
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/sw
)

add_executable(npu_smoke_tb sim/smoke_tb.cpp)
my_npu_target_options(npu_smoke_tb)

verilate(
  npu_smoke_tb
  SOURCES rtl/npu_smoke_top.sv
  TOP_MODULE npu_smoke_top
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_smoke COMMAND npu_smoke_tb)

add_test(
  NAME abi_consistency
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_abi_consistency.py
)

add_custom_target(
  abi_consistency_check
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_abi_consistency.py
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)

add_executable(npu_common_tb sim/common_tb.cpp)
my_npu_target_options(npu_common_tb)

verilate(
  npu_common_tb
  SOURCES ${NPU_COMMON_MODULE_SOURCES}
  TOP_MODULE npu_common_smoke_top
  PREFIX Vnpu_common_smoke_top
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_common COMMAND npu_common_tb)

add_custom_target(
  common_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_COMMON_LINT_SOURCES}
          --top-module npu_common_smoke_top
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)

add_test(
  NAME common_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_COMMON_LINT_SOURCES}
          --top-module npu_common_smoke_top
)
set_tests_properties(
  common_rtl_lint
  PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(npu_pe_tb sim/pe_tb.cpp)
my_npu_target_options(npu_pe_tb)

verilate(
  npu_pe_tb
  SOURCES rtl/matrix/npu_pe_i8.sv
  TOP_MODULE npu_pe_i8
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_pe COMMAND npu_pe_tb)

add_executable(npu_array_tb sim/array_tb.cpp)
my_npu_target_options(npu_array_tb)

verilate(
  npu_array_tb
  SOURCES ${NPU_MATRIX_TEST_SOURCES}
  TOP_MODULE npu_systolic_array_test_top
  PREFIX Vnpu_systolic_array_test_top
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_array COMMAND npu_array_tb)

add_custom_target(
  matrix_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_MATRIX_TEST_SOURCES}
          --top-module npu_systolic_array_test_top
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)

add_test(
  NAME matrix_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_MATRIX_TEST_SOURCES}
          --top-module npu_systolic_array_test_top
)
set_tests_properties(
  matrix_rtl_lint
  PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(npu_tiling_tb sim/tiling_tb.cpp)
my_npu_target_options(npu_tiling_tb)

verilate(
  npu_tiling_tb
  SOURCES ${NPU_DATAPATH_SOURCES}
  TOP_MODULE npu_tiling_datapath_test_top
  PREFIX Vnpu_tiling_datapath_test_top
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_tiling COMMAND npu_tiling_tb)

add_custom_target(
  datapath_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_DATAPATH_SOURCES}
          --top-module npu_tiling_datapath_test_top
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)

add_test(
  NAME datapath_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_DATAPATH_SOURCES}
          --top-module npu_tiling_datapath_test_top
)
set_tests_properties(
  datapath_rtl_lint
  PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(npu_control_tb sim/control_tb.cpp)
my_npu_target_options(npu_control_tb)
target_include_directories(npu_control_tb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

verilate(
  npu_control_tb
  SOURCES ${NPU_CONTROL_SOURCES}
  TOP_MODULE npu_control_regs
  PREFIX Vnpu_control_regs
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_control COMMAND npu_control_tb)

add_custom_target(
  control_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_CONTROL_SOURCES}
          --top-module npu_control_regs
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)

add_test(
  NAME control_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_CONTROL_SOURCES}
          --top-module npu_control_regs
)
set_tests_properties(
  control_rtl_lint
  PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(npu_read_dma_tb sim/read_dma_tb.cpp)
my_npu_target_options(npu_read_dma_tb)

verilate(
  npu_read_dma_tb
  SOURCES ${NPU_READ_DMA_TEST_SOURCES}
  TOP_MODULE npu_read_dma_test_top
  PREFIX Vnpu_read_dma_test_top
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_read_dma COMMAND npu_read_dma_tb)

add_executable(npu_write_dma_tb sim/write_dma_tb.cpp)
my_npu_target_options(npu_write_dma_tb)

verilate(
  npu_write_dma_tb
  SOURCES ${NPU_WRITE_DMA_TEST_SOURCES}
  TOP_MODULE npu_write_dma_test_top
  PREFIX Vnpu_write_dma_test_top
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_write_dma COMMAND npu_write_dma_tb)

add_custom_target(
  dma_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_DMA_LINT_SOURCES}
          --top-module npu_read_dma_test_top
  COMMAND verilator --lint-only --sv -Wall ${NPU_DMA_LINT_SOURCES}
          --top-module npu_write_dma_test_top
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)

add_test(
  NAME dma_rtl_lint_read
  COMMAND verilator --lint-only --sv -Wall ${NPU_DMA_LINT_SOURCES}
          --top-module npu_read_dma_test_top
)
set_tests_properties(
  dma_rtl_lint_read
  PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(
  NAME dma_rtl_lint_write
  COMMAND verilator --lint-only --sv -Wall ${NPU_DMA_LINT_SOURCES}
          --top-module npu_write_dma_test_top
)
set_tests_properties(
  dma_rtl_lint_write
  PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(npu_command_tb sim/command_tb.cpp)
my_npu_target_options(npu_command_tb)
target_include_directories(npu_command_tb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

verilate(
  npu_command_tb
  SOURCES ${NPU_COMMAND_TEST_SOURCES}
  TOP_MODULE npu_command_processor_test_top
  PREFIX Vnpu_command_processor_test_top
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_command COMMAND npu_command_tb)

add_custom_target(
  command_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_COMMAND_TEST_SOURCES}
          --top-module npu_command_processor_test_top
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)

add_test(
  NAME command_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_COMMAND_TEST_SOURCES}
          --top-module npu_command_processor_test_top
)
set_tests_properties(
  command_rtl_lint
  PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(npu_gemm_tb sim/gemm_tb.cpp)
my_npu_target_options(npu_gemm_tb)

verilate(
  npu_gemm_tb
  SOURCES ${NPU_GEMM_SOURCES}
  TOP_MODULE npu_gemm_accelerator
  PREFIX Vnpu_gemm_accelerator
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_gemm COMMAND npu_gemm_tb)

add_executable(npu_top_tb sim/top_tb.cpp)
my_npu_target_options(npu_top_tb)
target_include_directories(npu_top_tb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

verilate(
  npu_top_tb
  SOURCES ${NPU_TOP_SOURCES}
  TOP_MODULE npu_top
  PREFIX Vnpu_top
  VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
)

add_test(NAME npu_top COMMAND npu_top_tb)

add_custom_target(
  integration_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_INTEGRATION_SOURCES}
          --top-module npu_top
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)

add_test(
  NAME integration_rtl_lint
  COMMAND verilator --lint-only --sv -Wall ${NPU_INTEGRATION_SOURCES}
          --top-module npu_top
)
set_tests_properties(
  integration_rtl_lint
  PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(my_npu_driver_test tests/driver_test.cpp)
my_npu_target_options(my_npu_driver_test)
target_link_libraries(my_npu_driver_test PRIVATE my_npu_driver)

add_test(NAME my_npu_driver COMMAND my_npu_driver_test)

add_custom_target(
  v1_lint
  DEPENDS
    common_rtl_lint
    matrix_rtl_lint
    datapath_rtl_lint
    control_rtl_lint
    dma_rtl_lint
    command_rtl_lint
    integration_rtl_lint
)

add_custom_target(
  v1_regression
  COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
  DEPENDS
    npu_smoke_tb
    npu_common_tb
    npu_pe_tb
    npu_array_tb
    npu_tiling_tb
    npu_control_tb
    npu_read_dma_tb
    npu_write_dma_tb
    npu_command_tb
    npu_gemm_tb
    npu_top_tb
    my_npu_driver_test
    abi_consistency_check
    v1_lint
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)
