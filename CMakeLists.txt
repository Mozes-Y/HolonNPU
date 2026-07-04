cmake_minimum_required(VERSION 4.0)

project(HolonNPU LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(verilator REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

option(HOLON_NPU_ENABLE_ASSERTIONS "Enable RTL assertions in Verilator builds" ON)
option(HOLON_NPU_ENABLE_COVERAGE "Enable Verilator coverage instrumentation" OFF)

set(HOLON_NPU_VERILATED_COV_SOURCE "${VERILATOR_ROOT}/include/verilated_cov.cpp")
if(NOT EXISTS "${HOLON_NPU_VERILATED_COV_SOURCE}")
  message(FATAL_ERROR "Cannot find Verilator coverage runtime: ${HOLON_NPU_VERILATED_COV_SOURCE}")
endif()

set(
  NPU_VERILATOR_ARGS
  -Wall
  -Wno-SYNCASYNCNET
  +incdir+${CMAKE_CURRENT_SOURCE_DIR}/rtl/common
  --output-split 100000000
  --output-split-cfuncs 100000000
)

if(HOLON_NPU_ENABLE_ASSERTIONS)
  list(APPEND NPU_VERILATOR_ARGS -DHOLON_NPU_ASSERT_ON)
endif()

if(HOLON_NPU_ENABLE_COVERAGE)
  list(
    APPEND NPU_VERILATOR_ARGS
    -DHOLON_NPU_COVER_ON
    --coverage-line
    --coverage-toggle
    --coverage-fsm
    --coverage-user
  )
endif()

set(NPU_SMOKE_TEST_SOURCES
  sim/rtl/common/npu_smoke_top.sv
)

set(NPU_ASSERT_FAIL_TEST_SOURCES
  rtl/common/npu_vr_if.sv
  sim/rtl/common/npu_assert_fail_test_top.sv
)

set(NPU_COMMON_MODULE_SOURCES
  rtl/common/npu_pkg.sv
  rtl/common/npu_fifo.sv
  rtl/common/npu_skid_buffer.sv
  rtl/common/npu_register_slice.sv
)

set(NPU_COMMON_INTERFACE_SOURCES
  rtl/common/npu_vr_if.sv
  rtl/common/npu_axi_lite_if.sv
  rtl/common/npu_axi4_if.sv
)

set(NPU_COMMON_TEST_SOURCES
  ${NPU_COMMON_INTERFACE_SOURCES}
  ${NPU_COMMON_MODULE_SOURCES}
  sim/rtl/common/npu_common_smoke_top.sv
)

set(NPU_COMMON_LINT_SOURCES
  ${NPU_COMMON_TEST_SOURCES}
)

set(NPU_MATRIX_MODULE_SOURCES
  rtl/matrix/npu_pe_i8.sv
  rtl/matrix/npu_systolic_array.sv
)

set(NPU_MATRIX_TEST_SOURCES
  ${NPU_MATRIX_MODULE_SOURCES}
  sim/rtl/matrix/npu_systolic_array_test_top.sv
)

set(NPU_DATAPATH_MODULE_SOURCES
  ${NPU_MATRIX_MODULE_SOURCES}
  rtl/datapath/npu_tile_mask.sv
  rtl/datapath/npu_banked_scratchpad.sv
  rtl/datapath/npu_i8_tile_buffer.sv
  rtl/datapath/npu_c_accum_buffer.sv
  rtl/datapath/npu_gemm_tile_scratchpad.sv
  rtl/datapath/npu_ping_pong_ctrl.sv
)

set(NPU_DATAPATH_TEST_SOURCES
  ${NPU_DATAPATH_MODULE_SOURCES}
  sim/rtl/datapath/npu_tiling_datapath_test_top.sv
)

set(NPU_CONTROL_SOURCES
  ${NPU_COMMON_INTERFACE_SOURCES}
  rtl/common/npu_pkg.sv
  rtl/control/npu_control_regs.sv
)

set(NPU_CONTROL_TEST_SOURCES
  ${NPU_CONTROL_SOURCES}
  sim/rtl/control/npu_control_regs_test_wrapper.sv
)

set(NPU_DMA_MODULE_SOURCES
  ${NPU_COMMON_INTERFACE_SOURCES}
  rtl/common/npu_pkg.sv
  rtl/dma/npu_axi4_read_dma.sv
  rtl/dma/npu_axi4_write_dma.sv
)

set(NPU_READ_DMA_TEST_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  sim/rtl/dma/npu_axi4_read_dma_test_wrapper.sv
  sim/rtl/dma/npu_read_dma_test_top.sv
)

set(NPU_WRITE_DMA_TEST_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  sim/rtl/dma/npu_axi4_write_dma_test_wrapper.sv
  sim/rtl/dma/npu_write_dma_test_top.sv
)

set(NPU_DMA_LINT_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  sim/rtl/dma/npu_axi4_read_dma_test_wrapper.sv
  sim/rtl/dma/npu_axi4_write_dma_test_wrapper.sv
  sim/rtl/dma/npu_read_dma_test_top.sv
  sim/rtl/dma/npu_write_dma_test_top.sv
)

set(NPU_COMMAND_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  rtl/command/npu_command_processor.sv
)

set(NPU_COMMAND_TEST_SOURCES
  ${NPU_COMMAND_SOURCES}
  sim/rtl/command/npu_command_processor_test_top.sv
)

set(NPU_GEMM_SOURCES
  ${NPU_DMA_MODULE_SOURCES}
  ${NPU_MATRIX_MODULE_SOURCES}
  rtl/datapath/npu_gemm_tile_scratchpad.sv
  rtl/integration/npu_gemm_accelerator.sv
)

set(NPU_GEMM_TEST_SOURCES
  ${NPU_GEMM_SOURCES}
  sim/rtl/integration/npu_gemm_accelerator_test_wrapper.sv
)

set(NPU_TOP_SOURCES
  ${NPU_COMMON_INTERFACE_SOURCES}
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

function(holon_npu_target_options target_name)
  target_compile_features(${target_name} PRIVATE cxx_std_26)
  target_compile_options(
    ${target_name}
    PRIVATE
      $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-deprecated-variadic-comma-omission>
  )
endfunction()

function(holon_npu_test_labels test_name)
  if(ARGN)
    set_tests_properties(${test_name} PROPERTIES LABELS "${ARGN}")
  endif()
endfunction()

function(holon_npu_add_python_check)
  cmake_parse_arguments(ARG "" "TARGET;TEST;SCRIPT" "LABELS;ARGS" ${ARGN})

  add_test(
    NAME ${ARG_TEST}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_SCRIPT} ${ARG_ARGS}
  )
  holon_npu_test_labels(${ARG_TEST} ${ARG_LABELS})
  set_tests_properties(
    ${ARG_TEST}
    PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )

  add_custom_target(
    ${ARG_TARGET}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_SCRIPT} ${ARG_ARGS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM
  )
endfunction()

function(holon_npu_add_verilated_test)
  cmake_parse_arguments(ARG "" "TARGET;TEST;CPP;TOP;PREFIX" "SOURCES;LABELS;INCLUDE_DIRS" ${ARGN})

  add_executable(
    ${ARG_TARGET}
    ${ARG_CPP}
    sim/tb_coverage.cpp
  )
  holon_npu_target_options(${ARG_TARGET})
  target_include_directories(${ARG_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sim)

  if(ARG_INCLUDE_DIRS)
    target_include_directories(${ARG_TARGET} PRIVATE ${ARG_INCLUDE_DIRS})
  endif()

  set(prefix_args)
  if(ARG_PREFIX)
    list(APPEND prefix_args PREFIX ${ARG_PREFIX})
  endif()

  verilate(
    ${ARG_TARGET}
    SOURCES ${ARG_SOURCES}
    TOP_MODULE ${ARG_TOP}
    ${prefix_args}
    VERILATOR_ARGS ${NPU_VERILATOR_ARGS}
  )

  if(NOT HOLON_NPU_ENABLE_COVERAGE)
    target_sources(${ARG_TARGET} PRIVATE ${HOLON_NPU_VERILATED_COV_SOURCE})
  endif()

  set(test_command ${ARG_TARGET})
  if(HOLON_NPU_ENABLE_COVERAGE)
    list(APPEND test_command --tb-coverage-root=${CMAKE_BINARY_DIR}/coverage)
  endif()

  add_test(NAME ${ARG_TEST} COMMAND ${test_command})
  holon_npu_test_labels(${ARG_TEST} ${ARG_LABELS})
endfunction()

function(holon_npu_add_lint_test)
  cmake_parse_arguments(ARG "" "TARGET;TOP" "SOURCES" ${ARGN})

  add_custom_target(
    ${ARG_TARGET}
    COMMAND verilator --lint-only --sv -Wall -Wno-SYNCASYNCNET +incdir+${CMAKE_CURRENT_SOURCE_DIR}/rtl/common ${ARG_SOURCES} --top-module ${ARG_TOP}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM
  )

  add_test(
    NAME ${ARG_TARGET}
    COMMAND verilator --lint-only --sv -Wall -Wno-SYNCASYNCNET +incdir+${CMAKE_CURRENT_SOURCE_DIR}/rtl/common ${ARG_SOURCES} --top-module ${ARG_TOP}
  )
  holon_npu_test_labels(${ARG_TARGET} lint)
  set_tests_properties(
    ${ARG_TARGET}
    PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
endfunction()

add_library(holon_npu_driver sw/holon_npu_driver.c)
target_compile_features(holon_npu_driver PRIVATE c_std_11)
target_include_directories(
  holon_npu_driver
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/sw
)

holon_npu_add_python_check(
  TARGET abi_generate_check
  TEST abi_generate_check
  SCRIPT tools/gen_abi.py
  ARGS --check
  LABELS fast static
)

holon_npu_add_python_check(
  TARGET rtl_interface_usage_check
  TEST rtl_interface_usage
  SCRIPT tools/check_rtl_interface_usage.py
  LABELS fast static
)

holon_npu_add_verilated_test(
  TARGET npu_smoke_tb
  TEST npu_smoke
  CPP sim/smoke_tb.cpp
  SOURCES ${NPU_SMOKE_TEST_SOURCES}
  TOP npu_smoke_top
  LABELS fast
)

holon_npu_add_verilated_test(
  TARGET npu_assert_fail_tb
  TEST npu_assert_fail
  CPP sim/assert_fail_tb.cpp
  SOURCES ${NPU_ASSERT_FAIL_TEST_SOURCES}
  TOP npu_assert_fail_test_top
  PREFIX Vnpu_assert_fail_test_top
  LABELS fast assertion
)

set_tests_properties(npu_assert_fail PROPERTIES WILL_FAIL TRUE)

holon_npu_add_verilated_test(
  TARGET npu_common_tb
  TEST npu_common
  CPP sim/common_tb.cpp
  SOURCES ${NPU_COMMON_TEST_SOURCES}
  TOP npu_common_smoke_top
  PREFIX Vnpu_common_smoke_top
  LABELS fast
)

holon_npu_add_lint_test(
  TARGET common_rtl_lint
  SOURCES ${NPU_COMMON_LINT_SOURCES}
  TOP npu_common_smoke_top
)

holon_npu_add_verilated_test(
  TARGET npu_pe_tb
  TEST npu_pe
  CPP sim/pe_tb.cpp
  SOURCES rtl/matrix/npu_pe_i8.sv
  TOP npu_pe_i8
  LABELS fast
)

holon_npu_add_verilated_test(
  TARGET npu_array_tb
  TEST npu_array
  CPP sim/array_tb.cpp
  SOURCES ${NPU_MATRIX_TEST_SOURCES}
  TOP npu_systolic_array_test_top
  PREFIX Vnpu_systolic_array_test_top
  LABELS fast
)

holon_npu_add_lint_test(
  TARGET matrix_rtl_lint
  SOURCES ${NPU_MATRIX_TEST_SOURCES}
  TOP npu_systolic_array_test_top
)

holon_npu_add_verilated_test(
  TARGET npu_tiling_tb
  TEST npu_tiling
  CPP sim/tiling_tb.cpp
  SOURCES ${NPU_DATAPATH_TEST_SOURCES}
  TOP npu_tiling_datapath_test_top
  PREFIX Vnpu_tiling_datapath_test_top
  LABELS fast
)

holon_npu_add_lint_test(
  TARGET datapath_rtl_lint
  SOURCES ${NPU_DATAPATH_TEST_SOURCES}
  TOP npu_tiling_datapath_test_top
)

holon_npu_add_verilated_test(
  TARGET npu_control_tb
  TEST npu_control
  CPP sim/control_tb.cpp
  SOURCES ${NPU_CONTROL_TEST_SOURCES}
  TOP npu_control_regs_test_wrapper
  PREFIX Vnpu_control_regs
  LABELS fast
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
)

holon_npu_add_lint_test(
  TARGET control_rtl_lint
  SOURCES ${NPU_CONTROL_TEST_SOURCES}
  TOP npu_control_regs_test_wrapper
)

holon_npu_add_verilated_test(
  TARGET npu_read_dma_tb
  TEST npu_read_dma
  CPP sim/read_dma_tb.cpp
  SOURCES ${NPU_READ_DMA_TEST_SOURCES}
  TOP npu_read_dma_test_top
  PREFIX Vnpu_read_dma_test_top
  LABELS fast
)

holon_npu_add_verilated_test(
  TARGET npu_write_dma_tb
  TEST npu_write_dma
  CPP sim/write_dma_tb.cpp
  SOURCES ${NPU_WRITE_DMA_TEST_SOURCES}
  TOP npu_write_dma_test_top
  PREFIX Vnpu_write_dma_test_top
  LABELS fast
)

holon_npu_add_lint_test(
  TARGET dma_rtl_lint_read
  SOURCES ${NPU_DMA_LINT_SOURCES}
  TOP npu_read_dma_test_top
)

holon_npu_add_lint_test(
  TARGET dma_rtl_lint_write
  SOURCES ${NPU_DMA_LINT_SOURCES}
  TOP npu_write_dma_test_top
)

add_custom_target(
  dma_rtl_lint
  DEPENDS
    dma_rtl_lint_read
    dma_rtl_lint_write
)

holon_npu_add_verilated_test(
  TARGET npu_command_tb
  TEST npu_command
  CPP sim/command_tb.cpp
  SOURCES ${NPU_COMMAND_TEST_SOURCES}
  TOP npu_command_processor_test_top
  PREFIX Vnpu_command_processor_test_top
  LABELS fast
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
)

holon_npu_add_lint_test(
  TARGET command_rtl_lint
  SOURCES ${NPU_COMMAND_TEST_SOURCES}
  TOP npu_command_processor_test_top
)

holon_npu_add_verilated_test(
  TARGET npu_gemm_tb
  TEST npu_gemm
  CPP sim/gemm_tb.cpp
  SOURCES ${NPU_GEMM_TEST_SOURCES}
  TOP npu_gemm_accelerator_test_wrapper
  PREFIX Vnpu_gemm_accelerator
  LABELS slow
)

holon_npu_add_verilated_test(
  TARGET npu_top_tb
  TEST npu_top
  CPP sim/top_tb.cpp
  SOURCES ${NPU_TOP_SOURCES}
  TOP npu_top
  PREFIX Vnpu_top
  LABELS slow
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
)

holon_npu_add_lint_test(
  TARGET integration_rtl_lint
  SOURCES ${NPU_INTEGRATION_SOURCES}
  TOP npu_top
)

add_executable(holon_npu_driver_test tests/driver_test.cpp)
holon_npu_target_options(holon_npu_driver_test)
target_link_libraries(holon_npu_driver_test PRIVATE holon_npu_driver)

add_test(NAME holon_npu_driver COMMAND holon_npu_driver_test)
holon_npu_test_labels(holon_npu_driver fast)

if(HOLON_NPU_ENABLE_COVERAGE)
  holon_npu_add_python_check(
    TARGET coverage_generate_check
    TEST coverage_check
    SCRIPT tools/check_coverage.py
    ARGS --build-dir ${CMAKE_BINARY_DIR}
    LABELS coverage
  )

  set_tests_properties(
    coverage_check
    PROPERTIES
      DEPENDS
        "npu_smoke;npu_common;npu_pe;npu_array;npu_tiling;npu_control;npu_read_dma;npu_write_dma;npu_command;npu_gemm;npu_top"
  )
endif()

add_custom_target(
  lint
  DEPENDS
    common_rtl_lint
    matrix_rtl_lint
    datapath_rtl_lint
    control_rtl_lint
    dma_rtl_lint
    command_rtl_lint
    integration_rtl_lint
)
