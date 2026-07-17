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
  cmake_parse_arguments(ARG "" "TARGET;CPP;TOP;PREFIX" "RTL_TARGETS;EXTRA_GENERATED_CLASSES;LIBRARIES" ${ARGN})

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
  target_link_libraries(${ARG_TARGET} PRIVATE holon_npu_public_headers ${ARG_LIBRARIES})

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

  if(ARG_EXTRA_GENERATED_CLASSES)
    set(generated_prefix ${ARG_PREFIX})
    if(NOT generated_prefix)
      set(generated_prefix V${ARG_TOP})
    endif()

    foreach(generated_class IN LISTS ARG_EXTRA_GENERATED_CLASSES)
      set(generated_dir ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${ARG_TARGET}.dir/${generated_prefix}.dir)
      set(generated_sources
        ${generated_dir}/${generated_prefix}_${generated_class}__0.cpp
        ${generated_dir}/${generated_prefix}_${generated_class}__Slow.cpp
        ${generated_dir}/${generated_prefix}_${generated_class}__0__Slow.cpp
      )
      get_target_property(existing_sources ${ARG_TARGET} SOURCES)
      if(NOT existing_sources)
        set(existing_sources)
      endif()
      set(missing_generated_sources)
      foreach(generated_source IN LISTS generated_sources)
        list(FIND existing_sources ${generated_source} generated_source_index)
        if(generated_source_index EQUAL -1)
          list(APPEND missing_generated_sources ${generated_source})
        endif()
      endforeach()
      if(missing_generated_sources)
        set_source_files_properties(${missing_generated_sources} PROPERTIES GENERATED TRUE)
        target_sources(${ARG_TARGET} PRIVATE ${missing_generated_sources})
      endif()
    endforeach()
  endif()
endfunction()

function(holon_npu_add_ctest)
  cmake_parse_arguments(ARG "VERILATED;WILL_FAIL" "TEST;TARGET" "LABELS;ARGS" ${ARGN})

  set(test_command ${ARG_TARGET} ${ARG_ARGS})
  if(ARG_VERILATED AND HOLON_NPU_ENABLE_COVERAGE)
    list(APPEND test_command --tb-coverage-root=${CMAKE_BINARY_DIR}/coverage)
  endif()

  add_test(NAME ${ARG_TEST} COMMAND ${test_command})
  holon_npu_test_labels(${ARG_TEST} ${ARG_LABELS})

  if(ARG_VERILATED AND HOLON_NPU_ENABLE_COVERAGE AND NOT ARG_WILL_FAIL)
    set_tests_properties(${ARG_TEST} PROPERTIES FIXTURES_REQUIRED holon_npu_coverage_run)
    set_property(GLOBAL APPEND PROPERTY HOLON_NPU_COVERAGE_TESTS ${ARG_TEST})
  endif()

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
    PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      RESOURCE_LOCK holon_npu_build_tree
  )
endfunction()

add_library(holon_npu_public_headers INTERFACE)
target_compile_features(holon_npu_public_headers INTERFACE c_std_23)
target_sources(
  holon_npu_public_headers
  INTERFACE
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include sw
    FILES
      include/holon_npu_isa.h
      include/holon_npu_program.h
      sw/holon_npu_driver.h
)

add_library(holon_npu_driver)
target_sources(holon_npu_driver PRIVATE sw/holon_npu_driver.c)
target_compile_features(holon_npu_driver PUBLIC c_std_23)
target_link_libraries(holon_npu_driver PUBLIC holon_npu_public_headers)

add_library(holon_npu_runtime)
target_sources(
  holon_npu_runtime
  PRIVATE
    sw/holon_npu_runtime.cpp
  PUBLIC
    FILE_SET runtime_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES include/holon_npu_runtime.hpp
)
target_compile_features(holon_npu_runtime PUBLIC cxx_std_26)
target_link_libraries(holon_npu_runtime PUBLIC holon_npu_public_headers)

add_library(holon_npu_model)
target_sources(
  holon_npu_model
  PRIVATE
    sim/model/holon_npu_model.cpp
  PUBLIC
    FILE_SET model_headers
    TYPE HEADERS
    BASE_DIRS sim/model
    FILES sim/model/holon_npu_model.hpp
)
target_compile_features(holon_npu_model PUBLIC cxx_std_26)
target_link_libraries(holon_npu_model PUBLIC holon_npu_runtime)

holon_npu_add_sv_source_target(holon_npu_rtl_pkg rtl/common/npu_pkg.sv)
holon_npu_add_sv_source_target(holon_npu_rtl_isa_pkg rtl/common/npu_isa_pkg.sv)
holon_npu_add_sv_source_target(
  holon_npu_rtl_interfaces
  rtl/common/npu_vr_if.sv
  rtl/common/npu_axi_lite_if.sv
  rtl/common/npu_axi4_if.sv
)

holon_npu_add_sv_source_target(holon_npu_rtl_pe rtl/matrix/npu_pe_i8.sv)
holon_npu_add_sv_source_target(holon_npu_rtl_array rtl/matrix/npu_systolic_array.sv)
target_link_libraries(holon_npu_rtl_array INTERFACE holon_npu_rtl_pe)

holon_npu_add_sv_source_target(holon_npu_rtl_control rtl/control/npu_control_regs.sv)
target_link_libraries(holon_npu_rtl_control INTERFACE holon_npu_rtl_pkg holon_npu_rtl_interfaces)

holon_npu_add_sv_source_target(holon_npu_rtl_loader rtl/control/npu_program_loader.sv)
target_link_libraries(holon_npu_rtl_loader INTERFACE holon_npu_rtl_pkg holon_npu_rtl_interfaces)

holon_npu_add_sv_source_target(
  holon_npu_rtl_completion
  rtl/control/npu_completion_writer.sv
)
target_link_libraries(
  holon_npu_rtl_completion
  INTERFACE
    holon_npu_rtl_pkg
    holon_npu_rtl_interfaces
)

holon_npu_add_sv_source_target(
  holon_npu_rtl_write_arbiter
  rtl/integration/npu_axi_write_arbiter.sv
)
target_link_libraries(
  holon_npu_rtl_write_arbiter
  INTERFACE
    holon_npu_rtl_interfaces
)

holon_npu_add_sv_source_target(
  holon_npu_rtl_localmem_interfaces
  rtl/localmem/npu_localmem_rd_if.sv
  rtl/localmem/npu_localmem_wr_if.sv
)
target_link_libraries(holon_npu_rtl_localmem_interfaces INTERFACE holon_npu_rtl_interfaces)

holon_npu_add_sv_source_target(
  holon_npu_rtl_localmem
  rtl/localmem/npu_local_memory.sv
)
target_link_libraries(holon_npu_rtl_localmem INTERFACE holon_npu_rtl_localmem_interfaces)

holon_npu_add_sv_source_target(
  holon_npu_rtl_data_port_arbiter
  rtl/localmem/npu_data_port_arbiter.sv
)
target_link_libraries(
  holon_npu_rtl_data_port_arbiter
  INTERFACE holon_npu_rtl_localmem_interfaces
)

holon_npu_add_sv_source_target(
  holon_npu_rtl_engine_data_arbiter
  rtl/localmem/npu_engine_data_arbiter.sv
)
target_link_libraries(
  holon_npu_rtl_engine_data_arbiter
  INTERFACE holon_npu_rtl_localmem_interfaces
)
target_link_libraries(holon_npu_rtl_loader INTERFACE holon_npu_rtl_localmem_interfaces)

holon_npu_add_sv_source_target(
  holon_npu_rtl_frontend_interface
  rtl/frontend/npu_frontend_if.sv
)

holon_npu_add_sv_source_target(
  holon_npu_rtl_frontend
  rtl/frontend/npu_reference_frontend.sv
)
target_link_libraries(
  holon_npu_rtl_frontend
  INTERFACE
    holon_npu_rtl_frontend_interface
    holon_npu_rtl_pkg
    holon_npu_rtl_isa_pkg
    holon_npu_rtl_localmem_interfaces
)

holon_npu_add_sv_source_target(holon_npu_rtl_vector rtl/vector/npu_vector_engine.sv)
target_link_libraries(
  holon_npu_rtl_vector
  INTERFACE
    holon_npu_rtl_pkg
    holon_npu_rtl_isa_pkg
    holon_npu_rtl_interfaces
    holon_npu_rtl_localmem_interfaces
)

holon_npu_add_sv_source_target(holon_npu_rtl_matrix rtl/matrix/npu_matrix_engine.sv)
target_link_libraries(
  holon_npu_rtl_matrix
  INTERFACE
    holon_npu_rtl_pkg
    holon_npu_rtl_isa_pkg
    holon_npu_rtl_interfaces
    holon_npu_rtl_localmem_interfaces
    holon_npu_rtl_array
)

holon_npu_add_sv_source_target(holon_npu_rtl_dma rtl/dma/npu_dma_fabric.sv)
target_link_libraries(
  holon_npu_rtl_dma
  INTERFACE
    holon_npu_rtl_pkg
    holon_npu_rtl_interfaces
    holon_npu_rtl_frontend_interface
    holon_npu_rtl_localmem_interfaces
)

holon_npu_add_sv_source_target(holon_npu_rtl_control_plane rtl/integration/npu_control_plane.sv)
target_link_libraries(
  holon_npu_rtl_control_plane
  INTERFACE
    holon_npu_rtl_control
    holon_npu_rtl_loader
    holon_npu_rtl_localmem
    holon_npu_rtl_data_port_arbiter
)

holon_npu_add_sv_source_target(holon_npu_rtl_frontend_tile rtl/integration/npu_frontend_tile.sv)
target_link_libraries(
  holon_npu_rtl_frontend_tile
  INTERFACE
    holon_npu_rtl_control_plane
    holon_npu_rtl_frontend
    holon_npu_rtl_dma
    holon_npu_rtl_completion
    holon_npu_rtl_write_arbiter
    holon_npu_rtl_engine_data_arbiter
    holon_npu_rtl_vector
    holon_npu_rtl_matrix
)

holon_npu_add_sv_source_target(holon_npu_rtl_top rtl/integration/npu_top.sv)
target_link_libraries(holon_npu_rtl_top INTERFACE holon_npu_rtl_frontend_tile)
holon_npu_add_sv_source_target(holon_npu_sim_assert_fail sim/rtl/common/npu_assert_fail_test_top.sv)
target_link_libraries(holon_npu_sim_assert_fail INTERFACE holon_npu_rtl_interfaces)

holon_npu_add_sv_source_target(holon_npu_sim_array sim/rtl/matrix/npu_systolic_array_test_top.sv)
target_link_libraries(holon_npu_sim_array INTERFACE holon_npu_rtl_array)

holon_npu_add_sv_source_target(holon_npu_sim_control sim/rtl/control/npu_control_regs_test_wrapper.sv)
target_link_libraries(holon_npu_sim_control INTERFACE holon_npu_rtl_control)

holon_npu_add_sv_source_target(
  holon_npu_sim_loader
  sim/rtl/control/npu_program_loader_test_wrapper.sv
)
target_link_libraries(holon_npu_sim_loader INTERFACE holon_npu_rtl_loader)

holon_npu_add_sv_source_target(
  holon_npu_sim_completion
  sim/rtl/control/npu_completion_writer_test_wrapper.sv
)
target_link_libraries(holon_npu_sim_completion INTERFACE holon_npu_rtl_completion)

holon_npu_add_sv_source_target(
  holon_npu_sim_localmem
  sim/rtl/localmem/npu_local_memory_test_wrapper.sv
)
target_link_libraries(
  holon_npu_sim_localmem
  INTERFACE
    holon_npu_rtl_localmem
    holon_npu_rtl_data_port_arbiter
)

holon_npu_add_sv_source_target(
  holon_npu_sim_control_plane
  sim/rtl/integration/npu_control_plane_test_wrapper.sv
)
target_link_libraries(holon_npu_sim_control_plane INTERFACE holon_npu_rtl_control_plane)

holon_npu_add_sv_source_target(
  holon_npu_sim_frontend_tile
  sim/rtl/integration/npu_frontend_tile_test_wrapper.sv
)
target_link_libraries(holon_npu_sim_frontend_tile INTERFACE holon_npu_rtl_frontend_tile)

holon_npu_add_sv_source_target(
  holon_npu_sim_dma
  sim/rtl/dma/npu_dma_fabric_test_wrapper.sv
)
target_link_libraries(holon_npu_sim_dma INTERFACE holon_npu_rtl_dma)

holon_npu_add_sv_source_target(
  holon_npu_sim_vector
  sim/rtl/vector/npu_vector_engine_test_wrapper.sv
)
target_link_libraries(
  holon_npu_sim_vector
  INTERFACE
    holon_npu_rtl_vector
    holon_npu_rtl_localmem
    holon_npu_rtl_data_port_arbiter
)

holon_npu_add_sv_source_target(
  holon_npu_sim_matrix
  sim/rtl/matrix/npu_matrix_engine_test_wrapper.sv
)
target_link_libraries(
  holon_npu_sim_matrix
  INTERFACE
    holon_npu_rtl_matrix
    holon_npu_rtl_localmem
    holon_npu_rtl_data_port_arbiter
)

holon_npu_add_python_check(
  TARGET abi_generate_check
  TEST abi_generate_check
  SCRIPT tools/gen_abi.py
  ARGS --check
  LABELS fast static
)

holon_npu_add_python_check(
  TARGET isa_generate_check
  TEST isa_generate_check
  SCRIPT tools/gen_isa.py
  ARGS --check
  LABELS fast static
)

holon_npu_add_python_check(
  TARGET isa_metadata_check
  TEST isa_metadata
  SCRIPT tools/check_isa.py
  LABELS fast static
)

holon_npu_add_python_check(
  TARGET abi_schema_check
  TEST abi_schema
  SCRIPT tools/check_abi.py
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
  TARGET npu_pe_tb
  CPP sim/pe_tb.cpp
  RTL_TARGETS holon_npu_rtl_pe
  TOP npu_pe_i8
)
holon_npu_add_ctest(TEST npu_pe TARGET npu_pe_tb VERILATED LABELS fast)

holon_npu_add_verilated_executable(
  TARGET npu_array_tb
  CPP sim/array_tb.cpp
  RTL_TARGETS holon_npu_sim_array
  TOP npu_systolic_array_test_top
  PREFIX Vnpu_systolic_array_test_top
)
holon_npu_add_ctest(TEST npu_array TARGET npu_array_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET array_rtl_lint
  RTL_TARGETS holon_npu_sim_array
  TOP npu_systolic_array_test_top
)
holon_npu_add_lint_test(TEST array_rtl_lint TARGET array_rtl_lint LABELS lint)

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
  TARGET npu_loader_tb
  CPP sim/loader_tb.cpp
  RTL_TARGETS holon_npu_sim_loader
  TOP npu_program_loader_test_wrapper
  PREFIX Vnpu_loader
)
holon_npu_add_ctest(TEST npu_loader TARGET npu_loader_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET loader_rtl_lint
  RTL_TARGETS holon_npu_sim_loader
  TOP npu_program_loader_test_wrapper
)
holon_npu_add_lint_test(TEST loader_rtl_lint TARGET loader_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_completion_writer_tb
  CPP sim/completion_writer_tb.cpp
  RTL_TARGETS holon_npu_sim_completion
  TOP npu_completion_writer_test_wrapper
  PREFIX Vnpu_completion_writer
)
holon_npu_add_ctest(
  TEST npu_completion_writer
  TARGET npu_completion_writer_tb
  VERILATED
  LABELS fast
)

holon_npu_add_lint_target(
  TARGET completion_writer_rtl_lint
  RTL_TARGETS holon_npu_sim_completion
  TOP npu_completion_writer_test_wrapper
)
holon_npu_add_lint_test(
  TEST completion_writer_rtl_lint
  TARGET completion_writer_rtl_lint
  LABELS lint
)

holon_npu_add_verilated_executable(
  TARGET npu_local_memory_tb
  CPP sim/local_memory_tb.cpp
  RTL_TARGETS holon_npu_sim_localmem
  TOP npu_local_memory_test_wrapper
  PREFIX Vnpu_local_memory
)
holon_npu_add_ctest(TEST npu_local_memory TARGET npu_local_memory_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET local_memory_rtl_lint
  RTL_TARGETS holon_npu_sim_localmem
  TOP npu_local_memory_test_wrapper
)
holon_npu_add_lint_test(TEST local_memory_rtl_lint TARGET local_memory_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_dma_fabric_tb
  CPP sim/dma_fabric_tb.cpp
  RTL_TARGETS holon_npu_sim_dma
  TOP npu_dma_fabric_test_wrapper
  PREFIX Vnpu_dma_fabric
)
holon_npu_add_ctest(TEST npu_dma_fabric TARGET npu_dma_fabric_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET dma_fabric_rtl_lint
  RTL_TARGETS holon_npu_sim_dma
  TOP npu_dma_fabric_test_wrapper
)
holon_npu_add_lint_test(TEST dma_fabric_rtl_lint TARGET dma_fabric_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_control_plane_tb
  CPP sim/control_plane_tb.cpp
  RTL_TARGETS holon_npu_sim_control_plane
  TOP npu_control_plane_test_wrapper
  PREFIX Vnpu_control_plane
)
holon_npu_add_ctest(TEST npu_control_plane TARGET npu_control_plane_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET control_plane_rtl_lint
  RTL_TARGETS holon_npu_sim_control_plane
  TOP npu_control_plane_test_wrapper
)
holon_npu_add_lint_test(TEST control_plane_rtl_lint TARGET control_plane_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_frontend_tile_tb
  CPP sim/frontend_tile_tb.cpp
  RTL_TARGETS holon_npu_sim_frontend_tile
  TOP npu_frontend_tile_test_wrapper
  PREFIX Vnpu_frontend_tile
  LIBRARIES holon_npu_model
)
holon_npu_add_ctest(TEST npu_frontend_tile TARGET npu_frontend_tile_tb VERILATED LABELS fast)

holon_npu_add_verilated_executable(
  TARGET npu_top_tb
  CPP sim/top_tb.cpp
  RTL_TARGETS holon_npu_rtl_top
  TOP npu_top
  PREFIX Vnpu_top
)
holon_npu_add_ctest(TEST npu_top TARGET npu_top_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET frontend_tile_rtl_lint
  RTL_TARGETS holon_npu_sim_frontend_tile
  TOP npu_frontend_tile_test_wrapper
)
holon_npu_add_lint_test(TEST frontend_tile_rtl_lint TARGET frontend_tile_rtl_lint LABELS lint)

holon_npu_add_lint_target(
  TARGET top_rtl_lint
  RTL_TARGETS holon_npu_rtl_top
  TOP npu_top
)
holon_npu_add_lint_test(TEST top_rtl_lint TARGET top_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_vector_engine_tb
  CPP sim/vector_engine_tb.cpp
  RTL_TARGETS holon_npu_sim_vector
  TOP npu_vector_engine_test_wrapper
  PREFIX Vnpu_vector_engine
)
holon_npu_add_ctest(TEST npu_vector_engine TARGET npu_vector_engine_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET vector_engine_rtl_lint
  RTL_TARGETS holon_npu_sim_vector
  TOP npu_vector_engine_test_wrapper
)
holon_npu_add_lint_test(TEST vector_engine_rtl_lint TARGET vector_engine_rtl_lint LABELS lint)

holon_npu_add_verilated_executable(
  TARGET npu_matrix_engine_tb
  CPP sim/matrix_engine_tb.cpp
  RTL_TARGETS holon_npu_sim_matrix
  TOP npu_matrix_engine_test_wrapper
  PREFIX Vnpu_matrix_engine
)
holon_npu_add_ctest(TEST npu_matrix_engine TARGET npu_matrix_engine_tb VERILATED LABELS fast)

holon_npu_add_lint_target(
  TARGET matrix_engine_rtl_lint
  RTL_TARGETS holon_npu_sim_matrix
  TOP npu_matrix_engine_test_wrapper
)
holon_npu_add_lint_test(TEST matrix_engine_rtl_lint TARGET matrix_engine_rtl_lint LABELS lint)


add_executable(holon_npu_driver_test)
target_sources(holon_npu_driver_test PRIVATE tests/driver_test.cpp)
holon_npu_target_options(holon_npu_driver_test)
target_link_libraries(holon_npu_driver_test PRIVATE holon_npu_driver)
holon_npu_add_ctest(TEST holon_npu_driver TARGET holon_npu_driver_test LABELS fast)

add_executable(holon_npu_model_test)
target_sources(holon_npu_model_test PRIVATE tests/model_test.cpp)
holon_npu_target_options(holon_npu_model_test)
target_link_libraries(holon_npu_model_test PRIVATE holon_npu_model)
holon_npu_add_ctest(TEST holon_npu_model TARGET holon_npu_model_test LABELS fast)

add_executable(holon_npu_runtime_test)
target_sources(holon_npu_runtime_test PRIVATE tests/runtime_test.cpp)
holon_npu_target_options(holon_npu_runtime_test)
target_link_libraries(holon_npu_runtime_test PRIVATE holon_npu_runtime holon_npu_model)
holon_npu_add_ctest(TEST holon_npu_runtime TARGET holon_npu_runtime_test LABELS fast)

if(HOLON_NPU_ENABLE_COVERAGE)
  get_property(coverage_tests GLOBAL PROPERTY HOLON_NPU_COVERAGE_TESTS)
  set(coverage_prepare_args --coverage-dir ${CMAKE_BINARY_DIR}/coverage)
  foreach(coverage_test IN LISTS coverage_tests)
    list(APPEND coverage_prepare_args --expected-test ${coverage_test})
  endforeach()

  add_test(
    NAME coverage_prepare
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_coverage.py
      ${coverage_prepare_args}
  )
  set_tests_properties(
    coverage_prepare
    PROPERTIES
      FIXTURES_SETUP holon_npu_coverage_run
      LABELS coverage
  )

  set(coverage_check_args --build-dir ${CMAKE_BINARY_DIR})
  foreach(coverage_test IN LISTS coverage_tests)
    list(APPEND coverage_check_args --expected-test ${coverage_test})
  endforeach()
  holon_npu_add_python_check(
    TARGET coverage_generate_check
    TEST coverage_check
    SCRIPT tools/check_coverage.py
    ARGS ${coverage_check_args}
    LABELS coverage
  )

  set_tests_properties(
    coverage_check
    PROPERTIES
      FIXTURES_CLEANUP holon_npu_coverage_run
      DEPENDS
        "npu_pe;npu_array;npu_control;npu_loader;npu_completion_writer;npu_local_memory;npu_dma_fabric;npu_control_plane;npu_frontend_tile;npu_top;npu_vector_engine;npu_matrix_engine"
  )
endif()

add_custom_target(
  lint
  DEPENDS
    array_rtl_lint
    control_rtl_lint
    loader_rtl_lint
    completion_writer_rtl_lint
    local_memory_rtl_lint
    dma_fabric_rtl_lint
    control_plane_rtl_lint
    frontend_tile_rtl_lint
    top_rtl_lint
    vector_engine_rtl_lint
    matrix_engine_rtl_lint
)
