cmake_minimum_required(VERSION 4.0)

project(HolonNPU LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(verilator REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

option(HOLON_NPU_ENABLE_ASSERTIONS "Enable RTL assertions in Verilator builds" ON)
option(HOLON_NPU_ENABLE_COVERAGE "Enable Verilator coverage instrumentation" OFF)

set(
  HOLON_NPU_VERILATOR_ARGS
  -Wall
  -Wno-SYNCASYNCNET
  +incdir+${CMAKE_CURRENT_SOURCE_DIR}/rtl/common
  --output-split 100000000
  --output-split-cfuncs 100000000
)

if(HOLON_NPU_ENABLE_ASSERTIONS)
  list(APPEND HOLON_NPU_VERILATOR_ARGS --assert)
endif()

set(HOLON_NPU_LINT_ARGS --lint-only --sv -Wall -Wno-SYNCASYNCNET +incdir+${CMAKE_CURRENT_SOURCE_DIR}/rtl/common)
if(HOLON_NPU_ENABLE_ASSERTIONS)
  list(APPEND HOLON_NPU_LINT_ARGS --assert)
endif()

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

function(holon_npu_add_sv_source_target target_name)
  add_library(${target_name} INTERFACE)
  if(ARGN)
    target_sources(${target_name} INTERFACE ${ARGN})
  endif()
endfunction()

function(holon_npu_collect_sv_sources_from_target source_target output_var)
  get_target_property(local_sources ${source_target} INTERFACE_SOURCES)
  if(NOT local_sources)
    set(local_sources)
  endif()

  get_target_property(linked_targets ${source_target} INTERFACE_LINK_LIBRARIES)
  if(NOT linked_targets)
    set(linked_targets)
  endif()

  foreach(linked_target IN LISTS linked_targets)
    if(TARGET ${linked_target})
      holon_npu_collect_sv_sources_from_target(${linked_target} dependency_sources)
      list(APPEND local_sources ${dependency_sources})
    endif()
  endforeach()

  set(${output_var} ${local_sources} PARENT_SCOPE)
endfunction()

function(holon_npu_collect_sv_sources output_var)
  set(collected_sources)
  foreach(source_target IN LISTS ARGN)
    holon_npu_collect_sv_sources_from_target(${source_target} target_sources)
    list(APPEND collected_sources ${target_sources})
  endforeach()
  list(REMOVE_DUPLICATES collected_sources)
  set(${output_var} ${collected_sources} PARENT_SCOPE)
endfunction()

function(holon_npu_add_verilated_executable)
  cmake_parse_arguments(ARG "" "TARGET;CPP;TOP;PREFIX" "RTL_TARGETS" ${ARGN})

  holon_npu_collect_sv_sources(verilator_sources ${ARG_RTL_TARGETS})

  add_executable(${ARG_TARGET})
  target_sources(
    ${ARG_TARGET}
    PRIVATE
      ${ARG_CPP}
      sim/tb_coverage.cpp
    PRIVATE
      FILE_SET test_headers
      TYPE HEADERS
      BASE_DIRS sim
      FILES sim/tb_coverage.hpp
  )
  holon_npu_target_options(${ARG_TARGET})
  target_link_libraries(${ARG_TARGET} PRIVATE holon_npu_public_headers)

  if(NOT HOLON_NPU_ENABLE_COVERAGE)
    target_sources(${ARG_TARGET} PRIVATE ${VERILATOR_ROOT}/include/verilated_cov.cpp)
  endif()

  set(prefix_args)
  if(ARG_PREFIX)
    list(APPEND prefix_args PREFIX ${ARG_PREFIX})
  endif()

  set(verilate_feature_args)
  if(HOLON_NPU_ENABLE_COVERAGE)
    list(APPEND verilate_feature_args COVERAGE)
  endif()

  verilate(
    ${ARG_TARGET}
    SOURCES ${verilator_sources}
    TOP_MODULE ${ARG_TOP}
    ${prefix_args}
    ${verilate_feature_args}
    VERILATOR_ARGS ${HOLON_NPU_VERILATOR_ARGS}
  )
endfunction()

function(holon_npu_add_ctest)
  cmake_parse_arguments(ARG "VERILATED;WILL_FAIL" "TEST;TARGET" "LABELS;ARGS" ${ARGN})

  set(test_command ${ARG_TARGET} ${ARG_ARGS})
  if(ARG_VERILATED AND HOLON_NPU_ENABLE_COVERAGE)
    list(APPEND test_command --tb-coverage-root=${CMAKE_BINARY_DIR}/coverage)
  endif()

  add_test(NAME ${ARG_TEST} COMMAND ${test_command})
  holon_npu_test_labels(${ARG_TEST} ${ARG_LABELS})

  if(ARG_WILL_FAIL)
    set_tests_properties(${ARG_TEST} PROPERTIES WILL_FAIL TRUE)
  endif()
endfunction()

function(holon_npu_add_lint_target)
  cmake_parse_arguments(ARG "" "TARGET;TOP" "RTL_TARGETS" ${ARGN})
  holon_npu_collect_sv_sources(lint_sources ${ARG_RTL_TARGETS})

  add_custom_target(
    ${ARG_TARGET}
    COMMAND verilator ${HOLON_NPU_LINT_ARGS} ${lint_sources} --top-module ${ARG_TOP}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM
  )
endfunction()

function(holon_npu_add_lint_test)
  cmake_parse_arguments(ARG "" "TEST;TARGET" "LABELS" ${ARGN})

  add_test(
    NAME ${ARG_TEST}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${ARG_TARGET}
  )
  holon_npu_test_labels(${ARG_TEST} ${ARG_LABELS})
  set_tests_properties(
    ${ARG_TEST}
    PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
endfunction()

add_library(holon_npu_public_headers INTERFACE)
target_sources(
  holon_npu_public_headers
  INTERFACE
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include sw
    FILES
      include/holon_npu_regs.h
      include/holon_npu_desc.h
      sw/holon_npu_driver.h
)

add_library(holon_npu_driver)
target_sources(holon_npu_driver PRIVATE sw/holon_npu_driver.c)
target_compile_features(holon_npu_driver PUBLIC c_std_23)
target_link_libraries(holon_npu_driver PUBLIC holon_npu_public_headers)

holon_npu_add_sv_source_target(holon_npu_rtl_pkg rtl/common/npu_pkg.sv)
holon_npu_add_sv_source_target(
  holon_npu_rtl_interfaces
  rtl/common/npu_vr_if.sv
  rtl/common/npu_axi_lite_if.sv
  rtl/common/npu_axi4_if.sv
)
holon_npu_add_sv_source_target(
  holon_npu_rtl_common
  rtl/common/npu_fifo.sv
  rtl/common/npu_skid_buffer.sv
  rtl/common/npu_register_slice.sv
)
target_link_libraries(holon_npu_rtl_common INTERFACE holon_npu_rtl_pkg holon_npu_rtl_interfaces)

holon_npu_add_sv_source_target(holon_npu_rtl_pe rtl/matrix/npu_pe_i8.sv)
holon_npu_add_sv_source_target(holon_npu_rtl_matrix rtl/matrix/npu_systolic_array.sv)
target_link_libraries(holon_npu_rtl_matrix INTERFACE holon_npu_rtl_pe)

holon_npu_add_sv_source_target(
  holon_npu_rtl_datapath
  rtl/datapath/npu_tile_mask.sv
  rtl/datapath/npu_banked_scratchpad.sv
  rtl/datapath/npu_i8_tile_buffer.sv
  rtl/datapath/npu_c_accum_buffer.sv
  rtl/datapath/npu_gemm_tile_scratchpad.sv
  rtl/datapath/npu_ping_pong_ctrl.sv
)
target_link_libraries(holon_npu_rtl_datapath INTERFACE holon_npu_rtl_matrix)

holon_npu_add_sv_source_target(holon_npu_rtl_control rtl/control/npu_control_regs.sv)
target_link_libraries(holon_npu_rtl_control INTERFACE holon_npu_rtl_pkg holon_npu_rtl_interfaces)

holon_npu_add_sv_source_target(
  holon_npu_rtl_dma
  rtl/dma/npu_axi4_read_dma.sv
  rtl/dma/npu_axi4_write_dma.sv
)
target_link_libraries(holon_npu_rtl_dma INTERFACE holon_npu_rtl_pkg holon_npu_rtl_interfaces)

holon_npu_add_sv_source_target(holon_npu_rtl_command rtl/command/npu_command_processor.sv)
target_link_libraries(holon_npu_rtl_command INTERFACE holon_npu_rtl_dma)

holon_npu_add_sv_source_target(
  holon_npu_rtl_gemm
  rtl/datapath/npu_gemm_tile_scratchpad.sv
  rtl/integration/npu_gemm_accelerator.sv
)
target_link_libraries(holon_npu_rtl_gemm INTERFACE holon_npu_rtl_dma holon_npu_rtl_matrix)

holon_npu_add_sv_source_target(holon_npu_rtl_top rtl/integration/npu_top.sv)
target_link_libraries(
  holon_npu_rtl_top
  INTERFACE
    holon_npu_rtl_control
    holon_npu_rtl_command
    holon_npu_rtl_gemm
)

holon_npu_add_sv_source_target(holon_npu_sim_smoke sim/rtl/common/npu_smoke_top.sv)
holon_npu_add_sv_source_target(holon_npu_sim_assert_fail sim/rtl/common/npu_assert_fail_test_top.sv)
target_link_libraries(holon_npu_sim_assert_fail INTERFACE holon_npu_rtl_interfaces)

holon_npu_add_sv_source_target(holon_npu_sim_common sim/rtl/common/npu_common_smoke_top.sv)
target_link_libraries(holon_npu_sim_common INTERFACE holon_npu_rtl_common)

holon_npu_add_sv_source_target(holon_npu_sim_matrix sim/rtl/matrix/npu_systolic_array_test_top.sv)
target_link_libraries(holon_npu_sim_matrix INTERFACE holon_npu_rtl_matrix)

holon_npu_add_sv_source_target(holon_npu_sim_tiling sim/rtl/datapath/npu_tiling_datapath_test_top.sv)
target_link_libraries(holon_npu_sim_tiling INTERFACE holon_npu_rtl_datapath)

holon_npu_add_sv_source_target(holon_npu_sim_control sim/rtl/control/npu_control_regs_test_wrapper.sv)
target_link_libraries(holon_npu_sim_control INTERFACE holon_npu_rtl_control)

holon_npu_add_sv_source_target(
  holon_npu_sim_dma_read
  sim/rtl/dma/npu_axi4_read_dma_test_wrapper.sv
  sim/rtl/dma/npu_read_dma_test_top.sv
)
target_link_libraries(holon_npu_sim_dma_read INTERFACE holon_npu_rtl_dma)

holon_npu_add_sv_source_target(
  holon_npu_sim_dma_write
  sim/rtl/dma/npu_axi4_write_dma_test_wrapper.sv
  sim/rtl/dma/npu_write_dma_test_top.sv
)
target_link_libraries(holon_npu_sim_dma_write INTERFACE holon_npu_rtl_dma)

holon_npu_add_sv_source_target(holon_npu_sim_dma_lint)
target_link_libraries(holon_npu_sim_dma_lint INTERFACE holon_npu_sim_dma_read holon_npu_sim_dma_write)

holon_npu_add_sv_source_target(holon_npu_sim_command sim/rtl/command/npu_command_processor_test_top.sv)
target_link_libraries(holon_npu_sim_command INTERFACE holon_npu_rtl_command)

holon_npu_add_sv_source_target(holon_npu_sim_gemm sim/rtl/integration/npu_gemm_accelerator_test_wrapper.sv)
target_link_libraries(holon_npu_sim_gemm INTERFACE holon_npu_rtl_gemm)

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

holon_npu_add_python_check(
  TARGET macro_policy_check
  TEST macro_policy
  SCRIPT tools/check_macro_policy.py
  LABELS fast static
)

holon_npu_add_verilated_executable(
  TARGET npu_smoke_tb
  CPP sim/smoke_tb.cpp
  RTL_TARGETS holon_npu_sim_smoke
  TOP npu_smoke_top
)
holon_npu_add_ctest(TEST npu_smoke TARGET npu_smoke_tb VERILATED LABELS fast)

if(HOLON_NPU_ENABLE_ASSERTIONS)
  holon_npu_add_verilated_executable(
    TARGET npu_assert_fail_tb
    CPP sim/assert_fail_tb.cpp
    RTL_TARGETS holon_npu_sim_assert_fail
    TOP npu_assert_fail_test_top
    PREFIX Vnpu_assert_fail_test_top
  )
  holon_npu_add_ctest(
    TEST npu_assert_fail
    TARGET npu_assert_fail_tb
    VERILATED
    WILL_FAIL
    LABELS fast assertion
  )
endif()

holon_npu_add_verilated_executable(
  TARGET npu_common_tb
  CPP sim/common_tb.cpp
  RTL_TARGETS holon_npu_sim_common
  TOP npu_common_smoke_top
  PREFIX Vnpu_common_smoke_top
)
holon_npu_add_ctest(TEST npu_common TARGET npu_common_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET common_rtl_lint
  RTL_TARGETS holon_npu_sim_common
  TOP npu_common_smoke_top
)
holon_npu_add_lint_test(TEST common_rtl_lint TARGET common_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_pe_tb
  CPP sim/pe_tb.cpp
  RTL_TARGETS holon_npu_rtl_pe
  TOP npu_pe_i8
)
holon_npu_add_ctest(TEST npu_pe TARGET npu_pe_tb VERILATED LABELS fast)

holon_npu_add_verilated_executable(
  TARGET npu_array_tb
  CPP sim/array_tb.cpp
  RTL_TARGETS holon_npu_sim_matrix
  TOP npu_systolic_array_test_top
  PREFIX Vnpu_systolic_array_test_top
)
holon_npu_add_ctest(TEST npu_array TARGET npu_array_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET matrix_rtl_lint
  RTL_TARGETS holon_npu_sim_matrix
  TOP npu_systolic_array_test_top
)
holon_npu_add_lint_test(TEST matrix_rtl_lint TARGET matrix_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_tiling_tb
  CPP sim/tiling_tb.cpp
  RTL_TARGETS holon_npu_sim_tiling
  TOP npu_tiling_datapath_test_top
  PREFIX Vnpu_tiling_datapath_test_top
)
holon_npu_add_ctest(TEST npu_tiling TARGET npu_tiling_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET datapath_rtl_lint
  RTL_TARGETS holon_npu_sim_tiling
  TOP npu_tiling_datapath_test_top
)
holon_npu_add_lint_test(TEST datapath_rtl_lint TARGET datapath_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_control_tb
  CPP sim/control_tb.cpp
  RTL_TARGETS holon_npu_sim_control
  TOP npu_control_regs_test_wrapper
  PREFIX Vnpu_control_regs
)
holon_npu_add_ctest(TEST npu_control TARGET npu_control_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET control_rtl_lint
  RTL_TARGETS holon_npu_sim_control
  TOP npu_control_regs_test_wrapper
)
holon_npu_add_lint_test(TEST control_rtl_lint TARGET control_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_read_dma_tb
  CPP sim/read_dma_tb.cpp
  RTL_TARGETS holon_npu_sim_dma_read
  TOP npu_read_dma_test_top
  PREFIX Vnpu_read_dma_test_top
)
holon_npu_add_ctest(TEST npu_read_dma TARGET npu_read_dma_tb VERILATED LABELS fast)

holon_npu_add_verilated_executable(
  TARGET npu_write_dma_tb
  CPP sim/write_dma_tb.cpp
  RTL_TARGETS holon_npu_sim_dma_write
  TOP npu_write_dma_test_top
  PREFIX Vnpu_write_dma_test_top
)
holon_npu_add_ctest(TEST npu_write_dma TARGET npu_write_dma_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET dma_rtl_lint_read
  RTL_TARGETS holon_npu_sim_dma_lint
  TOP npu_read_dma_test_top
)
holon_npu_add_lint_test(TEST dma_rtl_lint_read TARGET dma_rtl_lint_read LABELS lint)

holon_npu_add_lint_target(
  TARGET dma_rtl_lint_write
  RTL_TARGETS holon_npu_sim_dma_lint
  TOP npu_write_dma_test_top
)
holon_npu_add_lint_test(TEST dma_rtl_lint_write TARGET dma_rtl_lint_write LABELS lint)

add_custom_target(
  dma_rtl_lint
  DEPENDS
    dma_rtl_lint_read
    dma_rtl_lint_write
)

holon_npu_add_verilated_executable(
  TARGET npu_command_tb
  CPP sim/command_tb.cpp
  RTL_TARGETS holon_npu_sim_command
  TOP npu_command_processor_test_top
  PREFIX Vnpu_command_processor_test_top
)
holon_npu_add_ctest(TEST npu_command TARGET npu_command_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET command_rtl_lint
  RTL_TARGETS holon_npu_sim_command
  TOP npu_command_processor_test_top
)
holon_npu_add_lint_test(TEST command_rtl_lint TARGET command_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_gemm_tb
  CPP sim/gemm_tb.cpp
  RTL_TARGETS holon_npu_sim_gemm
  TOP npu_gemm_accelerator_test_wrapper
  PREFIX Vnpu_gemm_accelerator
)
holon_npu_add_ctest(TEST npu_gemm TARGET npu_gemm_tb VERILATED LABELS slow)

holon_npu_add_verilated_executable(
  TARGET npu_top_tb
  CPP sim/top_tb.cpp
  RTL_TARGETS holon_npu_rtl_top
  TOP npu_top
  PREFIX Vnpu_top
)
holon_npu_add_ctest(TEST npu_top TARGET npu_top_tb VERILATED LABELS slow)

holon_npu_add_lint_target(
  TARGET integration_rtl_lint
  RTL_TARGETS holon_npu_rtl_top
  TOP npu_top
)
holon_npu_add_lint_test(TEST integration_rtl_lint TARGET integration_rtl_lint LABELS lint)

add_executable(holon_npu_driver_test)
target_sources(holon_npu_driver_test PRIVATE tests/driver_test.cpp)
holon_npu_target_options(holon_npu_driver_test)
target_link_libraries(holon_npu_driver_test PRIVATE holon_npu_driver)
holon_npu_add_ctest(TEST holon_npu_driver TARGET holon_npu_driver_test LABELS fast)

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
