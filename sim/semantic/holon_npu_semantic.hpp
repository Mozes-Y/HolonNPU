#pragma once

#include "holon_npu_isa.h"
#include "holon_npu_program.h"
#include "holon_npu_runtime.hpp"
#include "holon_npu_types.hpp"
#include "holon_npu_hart.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace holon_npu::semantic {

enum class lifecycle_state : std::uint8_t {
    idle,
    loading,
    running,
    halted,
    resetting,
    done,
    fault,
};

using architectural_fault = ::holon_npu::runtime::fault_code;
using instruction_opcode = ::holon_npu::runtime::opcode;
using vector_element_width = ::holon_npu::runtime::vector_element_width;
using vector_rounding = ::holon_npu::runtime::vector_rounding;
using csr = ::holon_npu::runtime::csr;
using ::holon_npu::runtime::encode_csr_read;
using ::holon_npu::runtime::encode_dma_load;
using ::holon_npu::runtime::encode_dma_store;
using ::holon_npu::runtime::encode_matrix_gemm;
using ::holon_npu::runtime::encode_predicate_load;
using ::holon_npu::runtime::encode_predicate_ptrue;
using ::holon_npu::runtime::encode_quant_requantize;
using ::holon_npu::runtime::encode_scalar_add;
using ::holon_npu::runtime::encode_scalar_addi;
using ::holon_npu::runtime::encode_scalar_beq;
using ::holon_npu::runtime::encode_scalar_bne;
using ::holon_npu::runtime::encode_scalar_load;
using ::holon_npu::runtime::encode_scalar_movi;
using ::holon_npu::runtime::encode_scalar_store;
using ::holon_npu::runtime::encode_sync_fence_dma;
using ::holon_npu::runtime::encode_sync_fence_local;
using ::holon_npu::runtime::encode_sync_wait_dma;
using ::holon_npu::runtime::encode_system_exit;
using ::holon_npu::runtime::encode_system_fault;
using ::holon_npu::runtime::encode_vector_add;
using ::holon_npu::runtime::encode_vector_config;
using ::holon_npu::runtime::encode_vector_eq;
using ::holon_npu::runtime::encode_vector_gather;
using ::holon_npu::runtime::encode_vector_load;
using ::holon_npu::runtime::encode_vector_lt;
using ::holon_npu::runtime::encode_vector_max;
using ::holon_npu::runtime::encode_vector_min;
using ::holon_npu::runtime::encode_vector_reduce_max;
using ::holon_npu::runtime::encode_vector_reduce_min;
using ::holon_npu::runtime::encode_vector_reduce_sum;
using ::holon_npu::runtime::encode_vector_select;
using ::holon_npu::runtime::encode_vector_shl;
using ::holon_npu::runtime::encode_vector_sra;
using ::holon_npu::runtime::encode_vector_srl;
using ::holon_npu::runtime::encode_vector_store;
using ::holon_npu::runtime::encode_vector_sub;
using ::holon_npu::runtime::encode_vector_transpose4;
using ::holon_npu::runtime::encode_vector_unzip_even;
using ::holon_npu::runtime::encode_vector_unzip_odd;
using ::holon_npu::runtime::encode_vector_zip_hi;
using ::holon_npu::runtime::encode_vector_zip_lo;
using ::holon_npu::runtime::program_builder;

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
    architectural_fault fault = architectural_fault::none;
    std::uint32_t pc = 0;
    std::uint64_t retired = 0;
};

struct loader_config {
    std::uint64_t implemented_caps =
        HOLON_NPU_CAP_PROGRAM_DESCRIPTOR |
        HOLON_NPU_CAP_LOCAL_PROGRAM_MEMORY |
        HOLON_NPU_CAP_ARGUMENT_SCRATCHPAD_COPY |
        HOLON_NPU_CAP_IN_ORDER_DMA_QUEUE |
        HOLON_NPU_CAP_MATRIX_MICRO_OP |
        HOLON_NPU_CAP_INTEGER_VECTOR_BASE |
        HOLON_NPU_CAP_QUANT_VECTOR;
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
    system_address system_byte_offset{};
    local_address local_byte_offset{};
    std::uint32_t byte_count = 0;
};

struct matrix_gemm_i8_i32_op {
    local_address a_offset{};
    local_address b_offset{};
    local_address c_offset{};
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
    local_address c_offset{};
    std::uint8_t accumulator_id = 0;
    bool stored = false;
};

struct descriptor_fetch {
    system_address address{};
    std::uint32_t byte_count = HOLON_NPU_PROGRAM_DESC_SIZE;
};

struct code_fetch {
    system_address address{};
    std::uint32_t byte_count = 0;
};

struct argument_fetch {
    system_address address{};
    std::uint32_t byte_count = 0;
};

struct scalar_local_operation {
    local_address address{};
    bool write = false;
};

struct vector_operation {
    decoded_instruction instruction{};
    std::uint32_t vl = 0;
    std::uint32_t active_lanes = 0;
    std::uint8_t element_bytes = 0;
};

struct matrix_operation {
    matrix_gemm_i8_i32_op command{};
};

struct program_dma_operation {
    dma_direction direction = dma_direction::system_to_local;
    system_address system{};
    local_address local{};
    std::uint32_t byte_count = 0;
    std::vector<std::byte> store_payload{};
};

struct sync_operation {
    instruction_opcode opcode = instruction_opcode::sync_wait_dma;
};

struct completion_record_write {
    system_address address{};
    std::array<std::byte, HOLON_NPU_COMPLETION_RECORD_SIZE> payload{};
};

using operation = std::variant<
    descriptor_fetch,
    code_fetch,
    argument_fetch,
    scalar_local_operation,
    vector_operation,
    matrix_operation,
    program_dma_operation,
    sync_operation,
    completion_record_write>;

struct pending_operation {
    operation_token token{};
    instruction_address pc{};
    operation value{};
};

struct retired_event {
    instruction_address pc{};
    std::uint32_t word = 0;
    std::uint64_t instret = 0;
};

struct terminal_event {
    lifecycle_state state = lifecycle_state::idle;
    architectural_fault fault = architectural_fault::none;
    instruction_address pc{};
    std::uint64_t instret = 0;
};

using execution_event = std::variant<retired_event, pending_operation, terminal_event>;

struct operation_success {};
struct read_payload { std::vector<std::byte> bytes; };
struct operation_failure { architectural_fault fault = architectural_fault::none; };
using operation_result = std::variant<operation_success, read_payload, operation_failure>;

enum class api_error : std::uint8_t {
    operation_pending,
    no_pending_operation,
    token_mismatch,
    invalid_completion,
    invalid_state,
};

struct boot_image {
    std::span<const std::uint32_t> instructions;
    instruction_address entry{};
    std::size_t local_memory_bytes = HOLON_NPU_LOCAL_MEM_MAX_BYTES;
    std::span<const std::byte> initial_data{};
    local_address data_address{};
};

enum class boot_error : std::uint8_t {
    operation_pending,
    invalid_program_size,
    invalid_entry,
    invalid_local_memory_size,
    invalid_data_range,
};

decoded_instruction decode(std::uint32_t word);
std::string class_name(holon_npu_isa_class_t isa_class);
std::string disassemble(const decoded_instruction& inst);

class program_machine {
public:
    static constexpr std::size_t vector_register_count = 16;
    static constexpr std::size_t default_max_vl = 16;

    explicit program_machine(
        std::size_t scratchpad_bytes = HOLON_NPU_LOCAL_MEM_MAX_BYTES,
        std::size_t max_vl = default_max_vl
    );

    void reset();
    [[nodiscard]] std::expected<void, boot_error> boot(const boot_image& image);
    void initialize(
        std::span<const std::uint32_t> words,
        std::size_t active_local_mem_bytes,
        instruction_address entry = {}
    );
    bool load_arguments(std::span<const std::byte> bytes, local_address destination = {});
    bool write_i8(local_address destination, std::span<const std::int8_t> values);
    std::vector<std::int8_t> read_i8(local_address source, std::size_t count) const;
    bool write_i16(local_address destination, std::span<const std::int16_t> values);
    std::vector<std::int16_t> read_i16(local_address source, std::size_t count) const;
    bool write_i32(local_address destination, std::span<const std::int32_t> values);
    std::vector<std::int32_t> read_i32(local_address source, std::size_t count) const;

    std::expected<execution_event, api_error> advance();
    std::expected<execution_event, api_error> complete(
        operation_token token,
        operation_result result
    );

    [[nodiscard]] const std::vector<dma_event>& dma_events() const { return dma_events_; }
    [[nodiscard]] const std::vector<matrix_event>& matrix_events() const { return matrix_events_; }
    void clear_dma_events();
    void clear_matrix_events();
    [[nodiscard]] run_result snapshot() const { return {state_, fault_, scalar_.pc_, scalar_.retired_}; }
    [[nodiscard]] lifecycle_state state() const { return state_; }
    [[nodiscard]] architectural_fault fault() const { return fault_; }
    [[nodiscard]] std::uint32_t pc() const { return scalar_.pc_; }
    [[nodiscard]] std::uint32_t vl() const { return vl_; }
    [[nodiscard]] vector_element_width element_width() const { return element_width_; }
    [[nodiscard]] bool elements_signed() const { return elements_signed_; }
    [[nodiscard]] std::uint64_t retired() const { return scalar_.retired_; }
    [[nodiscard]] std::int32_t scalar_register(std::size_t index) const {
        return scalar_.registers_.at(index);
    }

private:
    friend class direct_runner;
    friend class device;

    using vector_register = std::vector<std::int32_t>;
    static constexpr std::size_t matrix_max_dimension = HOLON_NPU_ISA_MATRIX_MAX_DIMENSION;
    using matrix_accumulator =
        std::array<std::array<std::int32_t, matrix_max_dimension>, matrix_max_dimension>;

    struct pending_context {
        pending_operation public_operation{};
        decoded_instruction instruction{};
    };

    run_result execute_current_instruction();
    std::optional<operation> operation_for(const decoded_instruction& inst);
    bool complete_dma(const program_dma_operation& request, const operation_result& result);
    bool issue_matrix_gemm_i8_i32(const matrix_gemm_i8_i32_op& op);
    bool local_range_ok(local_address address, std::size_t byte_count) const;
    bool local_range_ok(std::uint32_t address, std::size_t byte_count) const {
        return local_range_ok(local_address{address}, byte_count);
    }
    std::vector<std::byte> read_local_bytes(local_address address, std::size_t byte_count) const;
    bool write_local_bytes(local_address address, std::span<const std::byte> bytes);
    std::int8_t load_i8(std::uint32_t local_byte_offset) const;
    void store_i8(std::uint32_t local_byte_offset, std::int8_t value);
    std::int16_t load_i16(std::uint32_t local_byte_offset) const;
    void store_i16(std::uint32_t local_byte_offset, std::int16_t value);
    std::int32_t load_i32(std::uint32_t local_byte_offset) const;
    void store_i32(std::uint32_t local_byte_offset, std::int32_t value);
    void raise_fault(architectural_fault fault);
    bool register_index_ok(std::uint8_t index) const;
    bool predicate_index_ok(std::uint8_t index) const;
    [[nodiscard]] std::size_t element_bytes() const;
    [[nodiscard]] std::int32_t normalize_element(std::uint32_t value) const;
    [[nodiscard]] std::int32_t load_element(std::uint32_t local_byte_offset) const;
    void store_element(std::uint32_t local_byte_offset, std::int32_t value);

    std::vector<std::uint32_t> program_;
    std::vector<std::byte> scratchpad_;
    std::size_t active_local_mem_bytes_ = 0;
    std::vector<dma_event> dma_events_;
    std::vector<matrix_event> matrix_events_;
    std::array<vector_register, vector_register_count> vector_registers_;
    scalar::hart_state scalar_;
    std::vector<std::uint8_t> predicate_active_;
    matrix_accumulator matrix_accumulator_{};
    bool matrix_accumulator_valid_ = false;
    std::uint16_t matrix_accumulator_m_ = 0;
    std::uint16_t matrix_accumulator_n_ = 0;
    lifecycle_state state_ = lifecycle_state::idle;
    architectural_fault fault_ = architectural_fault::none;
    std::uint32_t vl_ = 0;
    vector_element_width element_width_ = vector_element_width::bits_32;
    vector_rounding rounding_ = vector_rounding::nearest_even;
    bool elements_signed_ = true;
    bool saturate_ = false;
    std::uint64_t next_token_ = 1;
    std::uint64_t next_dma_sequence_ = 0;
    std::uint64_t next_matrix_sequence_ = 0;
    std::size_t max_vl_ = default_max_vl;
    std::optional<pending_context> pending_;
};

class device {
public:
    explicit device(
        std::size_t scratchpad_bytes = HOLON_NPU_LOCAL_MEM_MAX_BYTES,
        std::size_t max_vl = program_machine::default_max_vl,
        loader_config config = {}
    );

    void reset();
    std::expected<void, api_error> submit(system_address descriptor_address);
    std::expected<void, api_error> soft_reset();
    std::expected<void, api_error> halt();
    std::expected<void, api_error> resume();
    std::expected<void, api_error> debug_step();
    std::expected<void, api_error> clear_terminal();
    std::expected<execution_event, api_error> advance();
    std::expected<execution_event, api_error> complete(
        operation_token token,
        operation_result result
    );
    run_result load_program_descriptor(
        const holon_npu_program_desc_t& descriptor,
        std::span<const std::uint32_t> program_words,
        std::span<const std::byte> arguments
    );

    [[nodiscard]] program_machine& program() { return program_; }
    [[nodiscard]] const program_machine& program() const { return program_; }
    [[nodiscard]] lifecycle_state state() const { return state_; }
    [[nodiscard]] architectural_fault fault() const { return fault_; }
    [[nodiscard]] bool irq_pending() const { return irq_pending_; }
    void clear_irq() { irq_pending_ = false; }

private:
    friend class direct_runner;

    enum class phase : std::uint8_t {
        idle,
        descriptor,
        code,
        arguments,
        execute,
        completion,
        terminal,
        resetting,
    };

    struct external_pending {
        pending_operation request{};
        std::optional<operation_token> program_token{};
    };

    run_result validate_and_start(
        const holon_npu_program_desc_t& descriptor,
        std::span<const std::uint32_t> program_words,
        std::span<const std::byte> arguments
    );
    execution_event terminal();
    pending_operation make_pending(operation value, instruction_address pc = {});
    bool validate_descriptor_shape(const holon_npu_program_desc_t& descriptor);
    void enter_fault(architectural_fault fault);
    void finish_reset();

    program_machine program_;
    loader_config config_;
    phase phase_ = phase::idle;
    lifecycle_state state_ = lifecycle_state::idle;
    architectural_fault fault_ = architectural_fault::none;
    system_address descriptor_address_{};
    holon_npu_program_desc_t descriptor_{};
    std::vector<std::uint32_t> fetched_program_;
    std::vector<std::byte> fetched_arguments_;
    std::optional<external_pending> pending_;
    std::optional<terminal_event> deferred_terminal_;
    std::uint64_t next_token_ = 1;
    bool irq_pending_ = false;
    bool reset_requested_ = false;
    bool halted_ = false;
    bool debug_step_active_ = false;
};

class direct_runner {
public:
    explicit direct_runner(
        std::size_t scratchpad_bytes = HOLON_NPU_LOCAL_MEM_MAX_BYTES,
        std::size_t max_vl = program_machine::default_max_vl,
        std::size_t system_memory_bytes = 1U << 20U
    );

    void reset();
    void resize_system_memory(std::size_t byte_count);
    std::expected<void, api_error> submit(system_address descriptor_address);
    void load_program(std::span<const std::uint32_t> words);
    run_result load_program_descriptor(
        const holon_npu_program_desc_t& descriptor,
        std::span<const std::uint32_t> program_words,
        std::span<const std::byte> arguments,
        const loader_config& config = {}
    );
    bool load_arguments(std::span<const std::byte> bytes, local_address destination = {});
    bool write_i8(local_address destination, std::span<const std::int8_t> values);
    std::vector<std::int8_t> read_i8(local_address source, std::size_t count) const;
    bool write_i16(local_address destination, std::span<const std::int16_t> values);
    std::vector<std::int16_t> read_i16(local_address source, std::size_t count) const;
    bool write_i32(local_address destination, std::span<const std::int32_t> values);
    std::vector<std::int32_t> read_i32(local_address source, std::size_t count) const;
    bool write_system_i32(system_address destination, std::span<const std::int32_t> values);
    std::vector<std::int32_t> read_system_i32(system_address source, std::size_t count) const;
    bool write_system_bytes(system_address destination, std::span<const std::byte> bytes);
    std::vector<std::byte> read_system_bytes(system_address source, std::size_t byte_count) const;
    bool issue_dma_load(system_address source, local_address destination, std::uint32_t byte_count);
    bool issue_dma_store(local_address source, system_address destination, std::uint32_t byte_count);
    bool issue_matrix_gemm_i8_i32(const matrix_gemm_i8_i32_op& op);
    run_result step();
    run_result run(std::uint64_t max_instructions);

    [[nodiscard]] const std::vector<dma_event>& dma_events() const {
        return device_.program().dma_events();
    }
    [[nodiscard]] const std::vector<matrix_event>& matrix_events() const {
        return device_.program().matrix_events();
    }
    void clear_dma_events() { device_.program().clear_dma_events(); }
    void clear_matrix_events() { device_.program().clear_matrix_events(); }
    [[nodiscard]] lifecycle_state state() const { return device_.program().state(); }
    [[nodiscard]] architectural_fault fault() const { return device_.program().fault(); }
    [[nodiscard]] std::uint32_t pc() const { return device_.program().pc(); }
    [[nodiscard]] std::uint32_t vl() const { return device_.program().vl(); }
    [[nodiscard]] vector_element_width element_width() const {
        return device_.program().element_width();
    }
    [[nodiscard]] bool elements_signed() const { return device_.program().elements_signed(); }
    [[nodiscard]] std::uint64_t retired() const { return device_.program().retired(); }
    [[nodiscard]] std::int32_t scalar_register(std::size_t index) const {
        return device_.program().scalar_register(index);
    }
    [[nodiscard]] program_machine& semantic_program() { return device_.program(); }
    [[nodiscard]] device& semantic_device() { return device_; }

private:
    bool system_range_ok(system_address address, std::size_t byte_count) const;
    operation_result service(const pending_operation& request);
    [[nodiscard]] run_result snapshot() const;

    device device_;
    std::vector<std::byte> system_memory_;
};

}  // namespace holon_npu::semantic
