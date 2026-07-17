#pragma once

#include "holon_npu_isa.h"
#include "holon_npu_program.h"
#include "holon_npu_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace holon_npu::v2::model {

enum class lifecycle_state : std::uint8_t {
    idle,
    running,
    done,
    fault,
};

using model_error = ::holon_npu::v2::runtime::fault_code;
using v2_opcode = ::holon_npu::v2::runtime::opcode;
using vector_element_width = ::holon_npu::v2::runtime::vector_element_width;
using vector_rounding = ::holon_npu::v2::runtime::vector_rounding;
using csr = ::holon_npu::v2::runtime::csr;
using ::holon_npu::v2::runtime::encode_csr_read;
using ::holon_npu::v2::runtime::encode_dma_load;
using ::holon_npu::v2::runtime::encode_dma_store;
using ::holon_npu::v2::runtime::encode_matrix_gemm;
using ::holon_npu::v2::runtime::encode_predicate_load;
using ::holon_npu::v2::runtime::encode_predicate_ptrue;
using ::holon_npu::v2::runtime::encode_quant_requantize;
using ::holon_npu::v2::runtime::encode_scalar_add;
using ::holon_npu::v2::runtime::encode_scalar_addi;
using ::holon_npu::v2::runtime::encode_scalar_beq;
using ::holon_npu::v2::runtime::encode_scalar_bne;
using ::holon_npu::v2::runtime::encode_scalar_load;
using ::holon_npu::v2::runtime::encode_scalar_movi;
using ::holon_npu::v2::runtime::encode_scalar_store;
using ::holon_npu::v2::runtime::encode_sync_fence_dma;
using ::holon_npu::v2::runtime::encode_sync_fence_local;
using ::holon_npu::v2::runtime::encode_sync_wait_dma;
using ::holon_npu::v2::runtime::encode_system_exit;
using ::holon_npu::v2::runtime::encode_system_fault;
using ::holon_npu::v2::runtime::encode_vector_add;
using ::holon_npu::v2::runtime::encode_vector_config;
using ::holon_npu::v2::runtime::encode_vector_eq;
using ::holon_npu::v2::runtime::encode_vector_gather;
using ::holon_npu::v2::runtime::encode_vector_load;
using ::holon_npu::v2::runtime::encode_vector_lt;
using ::holon_npu::v2::runtime::encode_vector_max;
using ::holon_npu::v2::runtime::encode_vector_min;
using ::holon_npu::v2::runtime::encode_vector_reduce_max;
using ::holon_npu::v2::runtime::encode_vector_reduce_min;
using ::holon_npu::v2::runtime::encode_vector_reduce_sum;
using ::holon_npu::v2::runtime::encode_vector_select;
using ::holon_npu::v2::runtime::encode_vector_shl;
using ::holon_npu::v2::runtime::encode_vector_sra;
using ::holon_npu::v2::runtime::encode_vector_srl;
using ::holon_npu::v2::runtime::encode_vector_store;
using ::holon_npu::v2::runtime::encode_vector_sub;
using ::holon_npu::v2::runtime::encode_vector_transpose4;
using ::holon_npu::v2::runtime::encode_vector_unzip_even;
using ::holon_npu::v2::runtime::encode_vector_unzip_odd;
using ::holon_npu::v2::runtime::encode_vector_zip_hi;
using ::holon_npu::v2::runtime::encode_vector_zip_lo;
using ::holon_npu::v2::runtime::program_builder;

struct decoded_instruction {
    std::uint32_t word = 0;
    holon_npu_isa_class_t isa_class = HOLON_NPU_ISA_ENUM_RESERVED_F;
    std::uint8_t opcode = 0;
    std::uint8_t rd = 0;
    std::uint8_t rs1 = 0;
    std::uint8_t rs2 = 0;
    std::uint16_t imm = 0;
};

struct run_result {
    lifecycle_state state = lifecycle_state::idle;
    model_error fault = model_error::none;
    std::uint32_t pc = 0;
    std::uint64_t retired = 0;
};

struct loader_config {
    std::uint64_t implemented_caps =
        HOLON_NPU_V2_CAP_PROGRAM_DESCRIPTOR |
        HOLON_NPU_V2_CAP_LOCAL_PROGRAM_MEMORY |
        HOLON_NPU_V2_CAP_ARGUMENT_SCRATCHPAD_COPY |
        HOLON_NPU_V2_CAP_IN_ORDER_DMA_QUEUE |
        HOLON_NPU_V2_CAP_MATRIX_MICRO_OP |
        HOLON_NPU_V2_CAP_INTEGER_VECTOR_BASE |
        HOLON_NPU_V2_CAP_QUANT_VECTOR;
    std::uint64_t implemented_op_classes =
        HOLON_NPU_PROGRAM_OP_CLASS_FRONTEND_CONTROL |
        HOLON_NPU_PROGRAM_OP_CLASS_PREDICATE |
        HOLON_NPU_PROGRAM_OP_CLASS_VECTOR |
        HOLON_NPU_PROGRAM_OP_CLASS_QUANTIZATION |
        HOLON_NPU_PROGRAM_OP_CLASS_MATRIX |
        HOLON_NPU_PROGRAM_OP_CLASS_DMA |
        HOLON_NPU_PROGRAM_OP_CLASS_CSR_DEBUG |
        HOLON_NPU_PROGRAM_OP_CLASS_SYNC |
        HOLON_NPU_PROGRAM_OP_CLASS_SYSTEM;
    std::uint16_t isa_major = HOLON_NPU_ISA_MAJOR;
    std::uint16_t isa_minor = HOLON_NPU_ISA_MINOR;
};

enum class dma_direction : std::uint8_t {
    system_to_local,
    local_to_system,
};

struct dma_event {
    std::uint64_t sequence = 0;
    dma_direction direction = dma_direction::system_to_local;
    std::uint64_t system_byte_offset = 0;
    std::uint32_t local_byte_offset = 0;
    std::uint32_t byte_count = 0;
};

struct matrix_gemm_i8_i32_op {
    std::uint32_t a_offset = 0;
    std::uint32_t b_offset = 0;
    std::uint32_t c_offset = 0;
    std::uint32_t a_row_stride_bytes = 0;
    std::uint32_t b_row_stride_bytes = 0;
    std::uint32_t c_row_stride_bytes = 0;
    std::uint16_t m = 0;
    std::uint16_t n = 0;
    std::uint16_t k = 0;
    std::uint8_t accumulator_id = 0;
    bool clear_accumulator = true;
    bool accumulate = false;
    bool store_result = true;
};

struct matrix_event {
    std::uint64_t sequence = 0;
    std::uint16_t m = 0;
    std::uint16_t n = 0;
    std::uint16_t k = 0;
    std::uint32_t c_offset = 0;
    std::uint8_t accumulator_id = 0;
    bool stored = false;
};


decoded_instruction decode(std::uint32_t word);
std::string class_name(holon_npu_isa_class_t isa_class);
std::string disassemble(const decoded_instruction& inst);


class machine {
public:
    static constexpr std::size_t vector_register_count = 16;
    static constexpr std::size_t default_max_vl = 16;

    explicit machine(
        std::size_t scratchpad_bytes = HOLON_NPU_LOCAL_MEM_MAX_BYTES,
        std::size_t max_vl = default_max_vl
    );

    void reset();
    void resize_system_memory(std::size_t byte_count);
    void load_program(std::span<const std::uint32_t> words);
    run_result load_program_descriptor(
        const holon_npu_program_desc_t& desc,
        std::span<const std::uint32_t> program_words,
        std::span<const std::byte> argument_bytes,
        const loader_config& config = {}
    );
    bool load_arguments(std::span<const std::byte> bytes, std::uint32_t local_byte_offset);
    bool write_i8(std::uint32_t local_byte_offset, std::span<const std::int8_t> values);
    std::vector<std::int8_t> read_i8(std::uint32_t local_byte_offset, std::size_t count) const;
    bool write_i16(std::uint32_t local_byte_offset, std::span<const std::int16_t> values);
    std::vector<std::int16_t> read_i16(std::uint32_t local_byte_offset, std::size_t count) const;
    bool write_i32(std::uint32_t local_byte_offset, std::span<const std::int32_t> values);
    std::vector<std::int32_t> read_i32(std::uint32_t local_byte_offset, std::size_t count) const;
    bool write_system_i32(std::uint64_t system_byte_offset, std::span<const std::int32_t> values);
    std::vector<std::int32_t> read_system_i32(std::uint64_t system_byte_offset, std::size_t count) const;
    bool issue_dma_load(std::uint64_t system_byte_offset, std::uint32_t local_byte_offset, std::uint32_t byte_count);
    bool issue_dma_store(std::uint32_t local_byte_offset, std::uint64_t system_byte_offset, std::uint32_t byte_count);
    bool issue_matrix_gemm_i8_i32(const matrix_gemm_i8_i32_op& op);
    [[nodiscard]] const std::vector<dma_event>& dma_events() const { return dma_events_; }
    [[nodiscard]] const std::vector<matrix_event>& matrix_events() const { return matrix_events_; }
    void clear_dma_events();
    void clear_matrix_events();

    run_result step();
    run_result run(std::uint64_t max_instructions);

    [[nodiscard]] lifecycle_state state() const { return state_; }
    [[nodiscard]] model_error fault() const { return fault_; }
    [[nodiscard]] std::uint32_t pc() const { return pc_; }
    [[nodiscard]] std::uint32_t vl() const { return vl_; }
    [[nodiscard]] vector_element_width element_width() const { return element_width_; }
    [[nodiscard]] bool elements_signed() const { return elements_signed_; }
    [[nodiscard]] std::uint64_t retired() const { return retired_; }
    [[nodiscard]] std::int32_t scalar_register(std::size_t index) const {
        return scalar_registers_.at(index);
    }

private:
    using vector_register = std::vector<std::int32_t>;
    static constexpr std::size_t matrix_max_dimension = HOLON_NPU_ISA_MATRIX_MAX_DIMENSION;
    using matrix_accumulator = std::array<std::array<std::int32_t, matrix_max_dimension>, matrix_max_dimension>;

    bool local_range_ok(std::uint32_t local_byte_offset, std::size_t byte_count) const;
    bool system_range_ok(std::uint64_t system_byte_offset, std::size_t byte_count) const;
    std::int8_t load_i8(std::uint32_t local_byte_offset) const;
    void store_i8(std::uint32_t local_byte_offset, std::int8_t value);
    std::int16_t load_i16(std::uint32_t local_byte_offset) const;
    void store_i16(std::uint32_t local_byte_offset, std::int16_t value);
    std::int32_t load_i32(std::uint32_t local_byte_offset) const;
    void store_i32(std::uint32_t local_byte_offset, std::int32_t value);
    std::int32_t load_system_i32(std::uint64_t system_byte_offset) const;
    void store_system_i32(std::uint64_t system_byte_offset, std::int32_t value);
    void raise_fault(model_error fault);
    bool register_index_ok(std::uint8_t index) const;
    bool predicate_index_ok(std::uint8_t index) const;
    [[nodiscard]] std::size_t element_bytes() const;
    [[nodiscard]] std::int32_t normalize_element(std::uint32_t value) const;
    [[nodiscard]] std::int32_t load_element(std::uint32_t local_byte_offset) const;
    void store_element(std::uint32_t local_byte_offset, std::int32_t value);

    std::vector<std::uint32_t> program_;
    std::vector<std::byte> scratchpad_;
    std::size_t active_local_mem_bytes_ = 0;
    std::vector<std::byte> system_memory_;
    std::vector<dma_event> dma_events_;
    std::vector<matrix_event> matrix_events_;
    std::array<vector_register, vector_register_count> vector_registers_;
    std::array<std::int32_t, HOLON_NPU_ISA_SCALAR_REGISTER_COUNT> scalar_registers_{};
    std::vector<std::uint8_t> predicate_active_;
    matrix_accumulator matrix_accumulator_{};
    bool matrix_accumulator_valid_ = false;
    std::uint16_t matrix_accumulator_m_ = 0;
    std::uint16_t matrix_accumulator_n_ = 0;
    lifecycle_state state_ = lifecycle_state::idle;
    model_error fault_ = model_error::none;
    std::uint32_t pc_ = 0;
    std::uint32_t vl_ = 0;
    vector_element_width element_width_ = vector_element_width::bits_32;
    vector_rounding rounding_ = vector_rounding::nearest_even;
    bool elements_signed_ = true;
    bool saturate_ = false;
    std::uint64_t retired_ = 0;
    std::uint64_t next_dma_sequence_ = 0;
    std::uint64_t next_matrix_sequence_ = 0;
    std::size_t max_vl_ = default_max_vl;
};

}  // namespace holon_npu::v2::model
