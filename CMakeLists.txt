cmake_minimum_required(VERSION 4.0)
project(HolonNPU VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(HOLON_NPU_VERIFY_TOOLCHAIN "Require upstream RV32 C23/C++26 execution probes" OFF)

add_library(holon_npu_semantic)
target_sources(holon_npu_semantic
  PRIVATE
    sim/semantic/holon_npu_semantic.cpp
    sim/semantic/holon_npu_npu.cpp
    sim/semantic/holon_npu_execution.cpp
    sim/semantic/holon_npu_instruction.cpp
    sim/semantic/holon_npu_scalar.cpp
    sim/semantic/holon_npu_hart.cpp
    sim/semantic/holon_npu_memory.cpp
    sim/semantic/holon_npu_elf.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS sim/semantic FILES
    sim/semantic/holon_npu_semantic.hpp
    sim/semantic/holon_npu_execution.hpp
    sim/semantic/holon_npu_types.hpp
    sim/semantic/holon_npu_instruction.hpp
    sim/semantic/holon_npu_scalar.hpp
    sim/semantic/holon_npu_hart.hpp
    sim/semantic/holon_npu_memory.hpp
    sim/semantic/holon_npu_elf.hpp
    sim/semantic/holon_npu_scalar_metadata.hpp
    sim/semantic/holon_npu_operand_metadata.hpp
)
target_compile_features(holon_npu_semantic PUBLIC cxx_std_26)
target_compile_options(holon_npu_semantic PRIVATE
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-frounding-math;-ffp-contract=off>
)

add_library(holon_npu_runtime)
target_sources(holon_npu_runtime
  PRIVATE sw/holon_npu_runtime.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/holon_npu_runtime.hpp
)
target_compile_features(holon_npu_runtime PUBLIC cxx_std_26)
target_link_libraries(holon_npu_runtime PUBLIC holon_npu_semantic)

if(NOT BUILD_TESTING)
  return()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Register tests only; target construction never runs a test.
function(holon_npu_add_ctest name)
  cmake_parse_arguments(ARG "" "LABEL" "COMMAND" ${ARGN})
  add_test(NAME ${name} COMMAND ${ARG_COMMAND})
  set_tests_properties(${name} PROPERTIES
    LABELS "${ARG_LABEL}"
    TIMEOUT 120
    ENVIRONMENT PYTHONDONTWRITEBYTECODE=1
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
endfunction()

holon_npu_add_ctest(isa_generate_check LABEL static
  COMMAND ${Python3_EXECUTABLE} tools/gen_isa.py --check)
holon_npu_add_ctest(isa_metadata LABEL static
  COMMAND ${Python3_EXECUTABLE} tools/check_isa.py)
holon_npu_add_ctest(isa_schema_tests LABEL static
  COMMAND ${Python3_EXECUTABLE} tests/isa_schema_test.py)
holon_npu_add_ctest(macro_policy LABEL static
  COMMAND ${Python3_EXECUTABLE} tools/check_macro_policy.py)

add_executable(holon_npu_semantic_test)
target_sources(holon_npu_semantic_test PRIVATE tests/semantic_test.cpp)
target_link_libraries(holon_npu_semantic_test PRIVATE holon_npu_runtime)
holon_npu_add_ctest(holon_npu_semantic LABEL unit
  COMMAND holon_npu_semantic_test)

add_executable(holon_npu_execution_test)
target_sources(holon_npu_execution_test PRIVATE tests/execution_test.cpp)
target_link_libraries(holon_npu_execution_test PRIVATE holon_npu_runtime)
holon_npu_add_ctest(holon_npu_execution LABEL integration
  COMMAND holon_npu_execution_test)

add_executable(holon_npu_instruction_test)
target_sources(holon_npu_instruction_test PRIVATE tests/instruction_test.cpp tests/npu_instruction_test.cpp)
target_link_libraries(holon_npu_instruction_test PRIVATE holon_npu_semantic)
holon_npu_add_ctest(holon_npu_instruction LABEL unit
  COMMAND holon_npu_instruction_test)

add_executable(holon_npu_scalar_test)
target_sources(holon_npu_scalar_test PRIVATE tests/scalar_test.cpp)
target_link_libraries(holon_npu_scalar_test PRIVATE holon_npu_semantic)
holon_npu_add_ctest(holon_npu_scalar LABEL unit
  COMMAND holon_npu_scalar_test)

add_executable(holon_npu_hart_test)
target_sources(holon_npu_hart_test PRIVATE tests/hart_test.cpp)
target_link_libraries(holon_npu_hart_test PRIVATE holon_npu_semantic)
holon_npu_add_ctest(holon_npu_hart LABEL unit
  COMMAND holon_npu_hart_test)

add_executable(holon_npu_memory_test)
target_sources(holon_npu_memory_test PRIVATE tests/memory_test.cpp)
target_link_libraries(holon_npu_memory_test PRIVATE holon_npu_semantic)
holon_npu_add_ctest(holon_npu_memory LABEL unit
  COMMAND holon_npu_memory_test)

add_executable(holon_npu_elf_test)
target_sources(holon_npu_elf_test PRIVATE tests/elf_test.cpp)
target_link_libraries(holon_npu_elf_test PRIVATE holon_npu_semantic)
holon_npu_add_ctest(holon_npu_elf LABEL unit
  COMMAND holon_npu_elf_test)

add_executable(holon_npu_runtime_test)
target_sources(holon_npu_runtime_test PRIVATE tests/runtime_test.cpp)
target_link_libraries(holon_npu_runtime_test PRIVATE holon_npu_runtime)
holon_npu_add_ctest(holon_npu_runtime LABEL unit
  COMMAND holon_npu_runtime_test)

add_executable(holon_npu_transformer_test)
target_sources(holon_npu_transformer_test PRIVATE tests/transformer_test.cpp)
target_link_libraries(holon_npu_transformer_test PRIVATE holon_npu_runtime)
holon_npu_add_ctest(holon_npu_transformer LABEL integration
  COMMAND holon_npu_transformer_test)

add_executable(holon_npu_system_memory_test)
target_sources(holon_npu_system_memory_test PRIVATE tests/system_memory_test.cpp)
target_link_libraries(holon_npu_system_memory_test PRIVATE holon_npu_runtime)
holon_npu_add_ctest(holon_npu_system_memory LABEL integration
  COMMAND holon_npu_system_memory_test)

if(HOLON_NPU_VERIFY_TOOLCHAIN)
  find_program(HOLON_NPU_RISCV_GCC NAMES riscv64-linux-gnu-gcc riscv64-unknown-elf-gcc REQUIRED)
  holon_npu_add_ctest(scalar_toolchain_check LABEL toolchain
    COMMAND ${Python3_EXECUTABLE} tests/scalar_toolchain_test.py
      --compiler ${HOLON_NPU_RISCV_GCC}
      --decoder $<TARGET_FILE:holon_npu_instruction_test>
      --runner $<TARGET_FILE:holon_npu_memory_test>
      --output ${CMAKE_BINARY_DIR}/scalar-toolchain
  )
endif()
