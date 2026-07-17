#include "Vnpu_v2_vector_engine.h"

#include "holon_npu_isa.h"
#include "holon_npu_program.h"
#include "tb_coverage.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <string_view>

#include <verilated.h>

namespace {

constexpr std::uint32_t kLocalMemBytes = 4096;

std::uint32_t encode(
    std::uint32_t isa_class,
    std::uint8_t opcode,
    std::uint8_t rd,
    std::uint8_t rs1,
    std::uint8_t rs2,
    std::uint16_t imm
) {
    return isa_class | (static_cast<std::uint32_t>(opcode) << HOLON_NPU_ISA_OPCODE_SHIFT) |
           ((static_cast<std::uint32_t>(rd) & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_RD_SHIFT) |
           ((static_cast<std::uint32_t>(rs1) & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_RS1_SHIFT) |
           ((static_cast<std::uint32_t>(rs2) & HOLON_NPU_ISA_FIELD_MASK) << HOLON_NPU_ISA_RS2_SHIFT) |
           (static_cast<std::uint32_t>(imm) & HOLON_NPU_ISA_IMM_MASK);
}

std::uint32_t vector_config(
    std::uint16_t vl,
    std::uint8_t sew,
    bool is_signed,
    bool saturate = false
) {
    const auto vl_minus_one = vl == 0
        ? HOLON_NPU_ISA_VTYPE_VL_MINUS_ONE_MASK
        : (static_cast<std::uint32_t>(vl) - 1U) & HOLON_NPU_ISA_VTYPE_VL_MINUS_ONE_MASK;
    const auto immediate = vl_minus_one |
        (static_cast<std::uint32_t>(sew) << HOLON_NPU_ISA_VTYPE_SEW_SHIFT) |
        (is_signed ? HOLON_NPU_ISA_VTYPE_SIGNED : 0U) |
        (saturate ? HOLON_NPU_ISA_VTYPE_SATURATE : 0U);
    return encode(
        HOLON_NPU_ISA_CLASS_VECTOR_CONFIG,
        HOLON_NPU_ISA_OPCODE_VECTOR_CONFIG_SET,
        0,
        0,
        0,
        static_cast<std::uint16_t>(immediate)
    );
}

std::uint32_t vector_config_i32(std::uint16_t vl) {
    return vector_config(vl, HOLON_NPU_ISA_VTYPE_SEW_32, true);
}

std::uint32_t predicate_ptrue(std::uint8_t pd = 0) {
    return encode(HOLON_NPU_ISA_CLASS_PREDICATE, HOLON_NPU_ISA_OPCODE_PREDICATE_PTRUE, pd, 0, 0, 0);
}

std::uint32_t predicate_load(std::uint8_t pd, std::uint16_t local_byte_offset) {
    return encode(HOLON_NPU_ISA_CLASS_PREDICATE, HOLON_NPU_ISA_OPCODE_PREDICATE_LOAD, pd, 0, 0, local_byte_offset);
}

std::uint32_t vector_load(
    std::uint8_t vd,
    std::uint16_t local_byte_offset,
    std::uint8_t predicate = 0
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_MEMORY, HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_LOAD, vd, predicate, 0, local_byte_offset);
}

std::uint32_t vector_store(
    std::uint8_t vs,
    std::uint16_t local_byte_offset,
    std::uint8_t predicate = 0
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_MEMORY, HOLON_NPU_ISA_OPCODE_VECTOR_MEMORY_STORE, vs, predicate, 0, local_byte_offset);
}

std::uint32_t vector_alu(
    std::uint8_t opcode,
    std::uint8_t vd,
    std::uint8_t vs1,
    std::uint8_t vs2,
    std::uint8_t predicate = 0
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, opcode, vd, vs1, vs2, predicate);
}

std::uint32_t vector_gather(
    std::uint8_t vd,
    std::uint8_t vs,
    std::uint8_t indices,
    std::uint8_t predicate = 0
) {
    return encode(
        HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE,
        HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_GATHER,
        vd,
        vs,
        indices,
        predicate
    );
}

std::uint32_t vector_permute(
    std::uint8_t opcode,
    std::uint8_t vd,
    std::uint8_t vs1,
    std::uint8_t vs2,
    std::uint8_t predicate = 0
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_PERMUTE, opcode, vd, vs1, vs2, predicate);
}

std::uint32_t vector_reduce(
    std::uint8_t opcode,
    std::uint8_t vd,
    std::uint8_t vs,
    std::uint8_t predicate = 0
) {
    return encode(HOLON_NPU_ISA_CLASS_VECTOR_REDUCTION, opcode, vd, vs, 0, predicate);
}

std::uint32_t vector_requantize(
    std::uint8_t vd,
    std::uint8_t vs,
    std::uint8_t predicate,
    std::uint16_t command_byte_offset
) {
    return encode(
        HOLON_NPU_ISA_CLASS_QUANTIZATION,
        HOLON_NPU_ISA_OPCODE_QUANTIZATION_REQUANTIZE,
        vd,
        vs,
        predicate,
        command_byte_offset
    );
}

void eval(Vnpu_v2_vector_engine& dut) {
    dut.eval();
}

void tick(Vnpu_v2_vector_engine& dut) {
    dut.clk_i = 0;
    eval(dut);
    dut.clk_i = 1;
    eval(dut);
    dut.clk_i = 0;
    eval(dut);
}

void clear_inputs(Vnpu_v2_vector_engine& dut) {
    dut.soft_reset_i = 0;
    dut.local_mem_bytes_i = kLocalMemBytes;
    dut.issue_valid_i = 0;
    dut.issue_data_i[0] = 0;
    dut.issue_data_i[1] = 0;
    dut.issue_data_i[2] = 0;
    dut.issue_data_i[3] = 0;
    dut.event_ready_i = 1;
    dut.host_wr_valid_i = 0;
    dut.host_wr_addr_i = 0;
    dut.host_wr_data_i = 0;
    dut.host_rd_valid_i = 0;
    dut.host_rd_addr_i = 0;
}

void reset(Vnpu_v2_vector_engine& dut) {
    dut.clk_i = 0;
    dut.rst_ni = 0;
    clear_inputs(dut);
    tick(dut);
    dut.rst_ni = 1;
    eval(dut);
}

bool expect_eq(std::string_view name, std::uint64_t actual, std::uint64_t expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return false;
}

bool host_write(Vnpu_v2_vector_engine& dut, std::uint32_t addr, std::uint32_t data) {
    dut.host_wr_valid_i = 1;
    dut.host_wr_addr_i = addr;
    dut.host_wr_data_i = data;
    bool accepted = false;
    for (int cycle = 0; cycle < 8; ++cycle) {
        eval(dut);
        if (dut.host_wr_ready_o) {
            accepted = true;
            tick(dut);
            break;
        }
        tick(dut);
    }
    dut.host_wr_valid_i = 0;
    eval(dut);
    return accepted;
}

struct ReadResult {
    bool valid = false;
    bool ready = false;
    bool error = false;
    std::uint32_t data = 0;
};

ReadResult host_read(Vnpu_v2_vector_engine& dut, std::uint32_t addr) {
    dut.host_rd_valid_i = 1;
    dut.host_rd_addr_i = addr;
    bool ready = false;
    for (int cycle = 0; cycle < 8; ++cycle) {
        eval(dut);
        if (dut.host_rd_ready_o) {
            ready = true;
            tick(dut);
            break;
        }
        tick(dut);
    }

    ReadResult result{.ready = ready};
    for (int cycle = 0; cycle < 8; ++cycle) {
        eval(dut);
        if (dut.host_rd_resp_valid_o) {
            result.valid = true;
            result.error = dut.host_rd_resp_error_o != 0;
            result.data = static_cast<std::uint32_t>(dut.host_rd_resp_data_o);
            tick(dut);
            break;
        }
        tick(dut);
    }
    dut.host_rd_valid_i = 0;
    eval(dut);
    return result;
}

struct EventResult {
    bool valid = false;
    bool fault = false;
    std::uint32_t fault_code = 0;
};

EventResult issue(Vnpu_v2_vector_engine& dut, std::uint32_t instruction) {
    dut.issue_valid_i = 1;
    dut.issue_data_i[0] = instruction;
    dut.issue_data_i[1] = 0;
    dut.issue_data_i[2] = 0;
    dut.issue_data_i[3] = 0;
    bool accepted = false;
    for (int cycle = 0; cycle < 8; ++cycle) {
        eval(dut);
        if (dut.issue_ready_o) {
            accepted = true;
            tick(dut);
            break;
        }
        tick(dut);
    }
    dut.issue_valid_i = 0;
    eval(dut);

    EventResult result{};
    for (int cycle = 0; cycle < 128; ++cycle) {
        eval(dut);
        if (dut.event_valid_o) {
            result.valid = accepted;
            result.fault = (dut.event_data_o & 1U) != 0;
            result.fault_code = static_cast<std::uint32_t>(dut.event_data_o >> 32U);
            tick(dut);
            break;
        }
        tick(dut);
    }
    return result;
}

std::uint32_t u32(std::int32_t value) {
    return std::bit_cast<std::uint32_t>(value);
}

bool expect_ok_event(std::string_view name, const EventResult& event) {
    bool ok = true;
    ok &= expect_eq(std::string{name} + " valid", event.valid, true);
    ok &= expect_eq(std::string{name} + " fault", event.fault, false);
    ok &= expect_eq(std::string{name} + " fault code", event.fault_code, HOLON_NPU_V2_FAULT_NONE);
    return ok;
}

bool expect_fault_event(std::string_view name, const EventResult& event, std::uint32_t fault_code) {
    bool ok = true;
    ok &= expect_eq(std::string{name} + " valid", event.valid, true);
    ok &= expect_eq(std::string{name} + " fault", event.fault, true);
    ok &= expect_eq(std::string{name} + " fault code", event.fault_code, fault_code);
    return ok;
}

bool expect_memory_word(Vnpu_v2_vector_engine& dut, std::string_view name, std::uint32_t addr, std::uint32_t expected) {
    const auto result = host_read(dut, addr);
    bool ok = true;
    ok &= expect_eq(std::string{name} + " ready", result.ready, true);
    ok &= expect_eq(std::string{name} + " valid", result.valid, true);
    ok &= expect_eq(std::string{name} + " error", result.error, false);
    ok &= expect_eq(name, result.data, expected);
    return ok;
}

bool load_operands(Vnpu_v2_vector_engine& dut, std::uint32_t lhs_base, std::uint32_t rhs_base) {
    bool ok = true;
    const std::uint32_t lhs_words[] = {u32(1), u32(-5), u32(0x7FFF'FFFF), u32(-16)};
    const std::uint32_t rhs_words[] = {u32(2), u32(7), u32(1), u32(2)};
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ok &= expect_eq("lhs host write", host_write(dut, lhs_base + lane * 4, lhs_words[lane]), true);
        ok &= expect_eq("rhs host write", host_write(dut, rhs_base + lane * 4, rhs_words[lane]), true);
    }
    return ok;
}

bool test_vector_load_store_and_add(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    bool ok = load_operands(dut, 0, 16);
    ok &= expect_ok_event("config i32", issue(dut, vector_config_i32(4)));
    ok &= expect_ok_event("load lhs", issue(dut, vector_load(1, 0)));
    ok &= expect_ok_event("load rhs", issue(dut, vector_load(2, 16)));
    ok &= expect_ok_event(
        "add",
        issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD, 3, 1, 2))
    );
    ok &= expect_ok_event("store add", issue(dut, vector_store(3, 32)));

    ok &= expect_memory_word(dut, "add lane 0", 32, u32(3));
    ok &= expect_memory_word(dut, "add lane 1", 36, u32(2));
    ok &= expect_memory_word(dut, "add lane 2", 40, u32(static_cast<std::int32_t>(0x8000'0000U)));
    ok &= expect_memory_word(dut, "add lane 3", 44, u32(-14));
    return ok;
}

bool test_vector_alu_compare_shift(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    bool ok = load_operands(dut, 0, 16);
    ok &= expect_ok_event("config i32", issue(dut, vector_config_i32(4)));
    ok &= expect_ok_event("load lhs", issue(dut, vector_load(1, 0)));
    ok &= expect_ok_event("load rhs", issue(dut, vector_load(2, 16)));

    ok &= expect_ok_event("sub", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SUB, 3, 1, 2)));
    ok &= expect_ok_event("store sub", issue(dut, vector_store(3, 64)));
    ok &= expect_memory_word(dut, "sub lane 0", 64, u32(-1));
    ok &= expect_memory_word(dut, "sub lane 1", 68, u32(-12));

    ok &= expect_ok_event("min", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_MIN, 4, 1, 2)));
    ok &= expect_ok_event("store min", issue(dut, vector_store(4, 80)));
    ok &= expect_memory_word(dut, "min lane 1", 84, u32(-5));

    ok &= expect_ok_event("max", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_MAX, 5, 1, 2)));
    ok &= expect_ok_event("store max", issue(dut, vector_store(5, 96)));
    ok &= expect_memory_word(dut, "max lane 1", 100, u32(7));

    ok &= expect_ok_event("eq", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_EQ, 6, 1, 2)));
    ok &= expect_ok_event("store eq", issue(dut, vector_store(6, 112)));
    ok &= expect_memory_word(dut, "eq lane 0", 112, 0);

    ok &= expect_ok_event("lt", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_LT, 7, 1, 2)));
    ok &= expect_ok_event("store lt", issue(dut, vector_store(7, 128)));
    ok &= expect_memory_word(dut, "lt lane 1", 132, 1);

    ok &= expect_ok_event("shl", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SHL, 8, 1, 2)));
    ok &= expect_ok_event("store shl", issue(dut, vector_store(8, 144)));
    ok &= expect_memory_word(dut, "shl lane 0", 144, u32(4));

    ok &= expect_ok_event("srl", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SRL, 9, 1, 2)));
    ok &= expect_ok_event("store srl", issue(dut, vector_store(9, 160)));
    ok &= expect_memory_word(dut, "srl lane 3", 172, 0x3FFF'FFFCU);

    ok &= expect_ok_event("sra", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SRA, 10, 1, 2)));
    ok &= expect_ok_event("store sra", issue(dut, vector_store(10, 176)));
    ok &= expect_memory_word(dut, "sra lane 3", 188, u32(-4));
    return ok;
}

bool test_narrow_elements(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("i8 lhs write", host_write(dut, 0, 0x10FF'807FU), true);
    ok &= expect_eq("i8 rhs write", host_write(dut, 4, 0xF002'FF01U), true);
    ok &= expect_ok_event(
        "config signed i8",
        issue(dut, vector_config(4, HOLON_NPU_ISA_VTYPE_SEW_8, true))
    );
    ok &= expect_ok_event("load i8 lhs", issue(dut, vector_load(1, 0)));
    ok &= expect_ok_event("load i8 rhs", issue(dut, vector_load(2, 4)));
    ok &= expect_ok_event("add i8", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD, 3, 1, 2)));
    ok &= expect_ok_event("store i8 add", issue(dut, vector_store(3, 8)));
    ok &= expect_memory_word(dut, "i8 add wrap", 8, 0x0001'7F80U);

    ok &= expect_ok_event("min i8", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_MIN, 4, 1, 2)));
    ok &= expect_ok_event("store i8 min", issue(dut, vector_store(4, 12)));
    ok &= expect_memory_word(dut, "i8 signed min", 12, 0xF0FF'8001U);

    ok &= expect_ok_event("srl i8", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SRL, 5, 1, 2)));
    ok &= expect_ok_event("store i8 srl", issue(dut, vector_store(5, 16)));
    ok &= expect_memory_word(dut, "i8 logical shift", 16, 0x103F'013FU);

    ok &= expect_ok_event(
        "config unsigned i8",
        issue(dut, vector_config(4, HOLON_NPU_ISA_VTYPE_SEW_8, false))
    );
    ok &= expect_ok_event("reload u8 lhs", issue(dut, vector_load(1, 0)));
    ok &= expect_ok_event("reload u8 rhs", issue(dut, vector_load(2, 4)));
    ok &= expect_ok_event("lt u8", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_LT, 6, 1, 2)));
    ok &= expect_ok_event("store u8 lt", issue(dut, vector_store(6, 20)));
    ok &= expect_memory_word(dut, "u8 comparison", 20, 0x0100'0100U);

    ok &= expect_eq("i16 lhs write", host_write(dut, 24, 0x8000'7FFFU), true);
    ok &= expect_eq("i16 rhs write", host_write(dut, 28, 0xFFFF'0001U), true);
    ok &= expect_ok_event(
        "config signed i16",
        issue(dut, vector_config(2, HOLON_NPU_ISA_VTYPE_SEW_16, true))
    );
    ok &= expect_ok_event("load i16 lhs", issue(dut, vector_load(1, 24)));
    ok &= expect_ok_event("load i16 rhs", issue(dut, vector_load(2, 28)));
    ok &= expect_ok_event("add i16", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD, 3, 1, 2)));
    ok &= expect_ok_event("store i16 add", issue(dut, vector_store(3, 32)));
    ok &= expect_memory_word(dut, "i16 add wrap", 32, 0x7FFF'8000U);

    ok &= expect_eq("partial destination init", host_write(dut, 36, 0xAABB'CCDDU), true);
    ok &= expect_ok_event(
        "config one i16",
        issue(dut, vector_config(1, HOLON_NPU_ISA_VTYPE_SEW_16, true))
    );
    ok &= expect_ok_event("load one i16", issue(dut, vector_load(1, 24)));
    ok &= expect_ok_event("store upper i16", issue(dut, vector_store(1, 38)));
    ok &= expect_memory_word(dut, "i16 byte strobe", 36, 0x7FFF'CCDDU);

    ok &= expect_eq("u16 lhs write", host_write(dut, 40, 0xFFFF'8000U), true);
    ok &= expect_eq("u16 rhs write", host_write(dut, 44, 0x0001'0001U), true);
    ok &= expect_ok_event(
        "config unsigned i16",
        issue(dut, vector_config(2, HOLON_NPU_ISA_VTYPE_SEW_16, false))
    );
    ok &= expect_ok_event("load u16 lhs", issue(dut, vector_load(1, 40)));
    ok &= expect_ok_event("load u16 rhs", issue(dut, vector_load(2, 44)));
    ok &= expect_ok_event("lt u16", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_LT, 3, 1, 2)));
    ok &= expect_ok_event("store u16 lt", issue(dut, vector_store(3, 48)));
    ok &= expect_memory_word(dut, "u16 comparison", 48, 0x0000'0000U);

    ok &= expect_ok_event(
        "config signed i16 compare",
        issue(dut, vector_config(2, HOLON_NPU_ISA_VTYPE_SEW_16, true))
    );
    ok &= expect_ok_event("reload i16 lhs", issue(dut, vector_load(1, 40)));
    ok &= expect_ok_event("reload i16 rhs", issue(dut, vector_load(2, 44)));
    ok &= expect_ok_event("lt signed i16", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_LT, 3, 1, 2)));
    ok &= expect_ok_event("store signed i16 lt", issue(dut, vector_store(3, 52)));
    ok &= expect_memory_word(dut, "signed i16 comparison", 52, 0x0001'0001U);

    ok &= expect_eq("u32 lhs write", host_write(dut, 56, 0xFFFF'FFFFU), true);
    ok &= expect_eq("u32 rhs write", host_write(dut, 60, 0x0000'0001U), true);
    ok &= expect_ok_event(
        "config unsigned i32",
        issue(dut, vector_config(1, HOLON_NPU_ISA_VTYPE_SEW_32, false))
    );
    ok &= expect_ok_event("load u32 lhs", issue(dut, vector_load(1, 56)));
    ok &= expect_ok_event("load u32 rhs", issue(dut, vector_load(2, 60)));
    ok &= expect_ok_event("lt u32", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_LT, 3, 1, 2)));
    ok &= expect_ok_event("store u32 lt", issue(dut, vector_store(3, 64)));
    ok &= expect_memory_word(dut, "u32 comparison", 64, 0x0000'0000U);

    ok &= expect_ok_event(
        "config signed i32 compare",
        issue(dut, vector_config(1, HOLON_NPU_ISA_VTYPE_SEW_32, true))
    );
    ok &= expect_ok_event("reload i32 lhs", issue(dut, vector_load(1, 56)));
    ok &= expect_ok_event("reload i32 rhs", issue(dut, vector_load(2, 60)));
    ok &= expect_ok_event("lt signed i32", issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_LT, 3, 1, 2)));
    ok &= expect_ok_event("store signed i32 lt", issue(dut, vector_store(3, 68)));
    ok &= expect_memory_word(dut, "signed i32 comparison", 68, 0x0000'0001U);
    return ok;
}

bool test_predicate_execution(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    bool ok = load_operands(dut, 0, 16);
    const std::uint32_t initial[] = {u32(100), u32(200), u32(300), u32(400)};
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ok &= expect_eq("predicate destination write", host_write(dut, 32 + lane * 4, initial[lane]), true);
    }
    ok &= expect_eq("predicate bits write", host_write(dut, 48, 0x0000'0005U), true);
    ok &= expect_ok_event("predicate config", issue(dut, vector_config_i32(4)));
    ok &= expect_ok_event("predicate load lhs", issue(dut, vector_load(1, 0)));
    ok &= expect_ok_event("predicate load rhs", issue(dut, vector_load(2, 16)));
    ok &= expect_ok_event("predicate load destination", issue(dut, vector_load(3, 32)));
    ok &= expect_ok_event("load predicate bits", issue(dut, predicate_load(0, 48)));
    ok &= expect_ok_event(
        "masked add",
        issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD, 3, 1, 2, 0))
    );
    ok &= expect_ok_event("masked store", issue(dut, vector_store(3, 32, 0)));
    ok &= expect_memory_word(dut, "predicate active lane 0", 32, u32(3));
    ok &= expect_memory_word(dut, "predicate inactive lane 1", 36, u32(200));
    ok &= expect_memory_word(
        dut,
        "predicate active lane 2",
        40,
        u32(static_cast<std::int32_t>(0x8000'0000U))
    );
    ok &= expect_memory_word(dut, "predicate inactive lane 3", 44, u32(400));

    ok &= expect_ok_event("predicate ptrue", issue(dut, predicate_ptrue()));
    ok &= expect_ok_event(
        "unmasked add",
        issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD, 4, 1, 2, 0))
    );
    ok &= expect_ok_event("unmasked store", issue(dut, vector_store(4, 64, 0)));
    ok &= expect_memory_word(dut, "ptrue lane 1", 68, u32(2));
    ok &= expect_memory_word(dut, "ptrue lane 3", 76, u32(-14));
    return ok;
}

bool test_saturating_arithmetic(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_eq("signed saturation lhs", host_write(dut, 0, 0x8088'7F78U), true);
    ok &= expect_eq("signed saturation rhs", host_write(dut, 4, 0x01EC'0114U), true);
    ok &= expect_ok_event(
        "signed saturation config",
        issue(dut, vector_config(4, HOLON_NPU_ISA_VTYPE_SEW_8, true, true))
    );
    ok &= expect_ok_event("signed saturation lhs load", issue(dut, vector_load(1, 0)));
    ok &= expect_ok_event("signed saturation rhs load", issue(dut, vector_load(2, 4)));
    ok &= expect_ok_event(
        "signed saturating add",
        issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD, 3, 1, 2))
    );
    ok &= expect_ok_event("signed saturating add store", issue(dut, vector_store(3, 8)));
    ok &= expect_memory_word(dut, "signed saturating add result", 8, 0x8180'7F7FU);
    ok &= expect_ok_event(
        "signed saturating sub",
        issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SUB, 4, 1, 2))
    );
    ok &= expect_ok_event("signed saturating sub store", issue(dut, vector_store(4, 12)));
    ok &= expect_memory_word(dut, "signed saturating sub result", 12, 0x809C'7E64U);

    ok &= expect_eq("unsigned saturation lhs", host_write(dut, 16, 0x0005'FFFAU), true);
    ok &= expect_eq("unsigned saturation rhs", host_write(dut, 20, 0x010A'010AU), true);
    ok &= expect_ok_event(
        "unsigned saturation config",
        issue(dut, vector_config(4, HOLON_NPU_ISA_VTYPE_SEW_8, false, true))
    );
    ok &= expect_ok_event("unsigned saturation lhs load", issue(dut, vector_load(1, 16)));
    ok &= expect_ok_event("unsigned saturation rhs load", issue(dut, vector_load(2, 20)));
    ok &= expect_ok_event(
        "unsigned saturating add",
        issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_ADD, 3, 1, 2))
    );
    ok &= expect_ok_event("unsigned saturating add store", issue(dut, vector_store(3, 24)));
    ok &= expect_memory_word(dut, "unsigned saturating add result", 24, 0x010F'FFFFU);
    ok &= expect_ok_event(
        "unsigned saturating sub",
        issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SUB, 4, 1, 2))
    );
    ok &= expect_ok_event("unsigned saturating sub store", issue(dut, vector_store(4, 28)));
    ok &= expect_memory_word(dut, "unsigned saturating sub result", 28, 0x0000'FEF0U);
    return ok;
}

bool test_pack_unpack_and_transpose(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    constexpr std::uint32_t first_base = 0;
    constexpr std::uint32_t second_base = 64;
    constexpr std::uint32_t zip_lo_base = 128;
    constexpr std::uint32_t zip_hi_base = 192;
    constexpr std::uint32_t first_restored_base = 256;
    constexpr std::uint32_t second_restored_base = 320;
    constexpr std::uint32_t transpose_base = 384;
    bool ok = true;
    for (std::uint32_t lane = 0; lane < 16; ++lane) {
        ok &= expect_eq("permute first write", host_write(dut, first_base + lane * 4, lane), true);
        ok &= expect_eq("permute second write", host_write(dut, second_base + lane * 4, 100U + lane), true);
    }
    ok &= expect_ok_event("permute config", issue(dut, vector_config_i32(16)));
    ok &= expect_ok_event("permute first load", issue(dut, vector_load(1, first_base)));
    ok &= expect_ok_event("permute second load", issue(dut, vector_load(2, second_base)));
    ok &= expect_ok_event(
        "zip lo",
        issue(dut, vector_permute(HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_LO, 3, 1, 2))
    );
    ok &= expect_ok_event(
        "zip hi",
        issue(dut, vector_permute(HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_HI, 4, 1, 2))
    );
    ok &= expect_ok_event("zip lo store", issue(dut, vector_store(3, zip_lo_base)));
    ok &= expect_ok_event("zip hi store", issue(dut, vector_store(4, zip_hi_base)));
    ok &= expect_ok_event(
        "unzip even",
        issue(dut, vector_permute(HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_EVEN, 5, 3, 4))
    );
    ok &= expect_ok_event(
        "unzip odd",
        issue(dut, vector_permute(HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_UNZIP_ODD, 6, 3, 4))
    );
    ok &= expect_ok_event("unzip even store", issue(dut, vector_store(5, first_restored_base)));
    ok &= expect_ok_event("unzip odd store", issue(dut, vector_store(6, second_restored_base)));
    ok &= expect_ok_event(
        "transpose4",
        issue(dut, vector_permute(HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_TRANSPOSE4, 7, 1, 0))
    );
    ok &= expect_ok_event("transpose4 store", issue(dut, vector_store(7, transpose_base)));

    for (std::uint32_t lane = 0; lane < 16; ++lane) {
        const auto zip_source_lane = lane / 2U;
        const auto zip_lo_expected = lane % 2U == 0U ? zip_source_lane : 100U + zip_source_lane;
        const auto zip_hi_expected = lane % 2U == 0U ? 8U + zip_source_lane : 108U + zip_source_lane;
        const auto transpose_expected = (lane % 4U) * 4U + lane / 4U;
        ok &= expect_memory_word(dut, "zip lo result", zip_lo_base + lane * 4, zip_lo_expected);
        ok &= expect_memory_word(dut, "zip hi result", zip_hi_base + lane * 4, zip_hi_expected);
        ok &= expect_memory_word(dut, "unzip first result", first_restored_base + lane * 4, lane);
        ok &= expect_memory_word(dut, "unzip second result", second_restored_base + lane * 4, 100U + lane);
        ok &= expect_memory_word(dut, "transpose4 result", transpose_base + lane * 4, transpose_expected);
    }

    ok &= expect_ok_event("odd permute config", issue(dut, vector_config_i32(3)));
    ok &= expect_fault_event(
        "odd zip fault",
        issue(dut, vector_permute(HOLON_NPU_ISA_OPCODE_VECTOR_PERMUTE_ZIP_LO, 3, 1, 2)),
        HOLON_NPU_V2_FAULT_VECTOR_CONFIG
    );
    return ok;
}

bool test_select_and_gather(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    constexpr std::uint32_t lhs_base = 0;
    constexpr std::uint32_t rhs_base = 16;
    constexpr std::uint32_t predicate_base = 32;
    constexpr std::uint32_t select_base = 48;
    constexpr std::uint32_t index_base = 64;
    constexpr std::uint32_t gather_base = 80;
    constexpr std::uint32_t invalid_index_base = 96;
    constexpr std::uint32_t preserved_base = 112;
    const std::uint32_t lhs[] = {u32(10), u32(20), u32(30), u32(40)};
    const std::uint32_t rhs[] = {u32(-1), u32(-2), u32(-3), u32(-4)};
    const std::uint32_t indices[] = {3, 0, 2, 1};
    const std::uint32_t invalid_indices[] = {3, 4, 2, 1};
    const std::uint32_t preserved[] = {u32(101), u32(102), u32(103), u32(104)};

    bool ok = true;
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ok &= expect_eq("select lhs write", host_write(dut, lhs_base + lane * 4, lhs[lane]), true);
        ok &= expect_eq("select rhs write", host_write(dut, rhs_base + lane * 4, rhs[lane]), true);
        ok &= expect_eq("gather index write", host_write(dut, index_base + lane * 4, indices[lane]), true);
        ok &= expect_eq(
            "invalid gather index write",
            host_write(dut, invalid_index_base + lane * 4, invalid_indices[lane]),
            true
        );
        ok &= expect_eq(
            "preserved destination write",
            host_write(dut, preserved_base + lane * 4, preserved[lane]),
            true
        );
    }
    ok &= expect_eq("select predicate write", host_write(dut, predicate_base, 0x5), true);
    ok &= expect_ok_event("helper config", issue(dut, vector_config_i32(4)));
    ok &= expect_ok_event("helper lhs load", issue(dut, vector_load(1, lhs_base)));
    ok &= expect_ok_event("helper rhs load", issue(dut, vector_load(2, rhs_base)));
    ok &= expect_ok_event("select predicate load", issue(dut, predicate_load(0, predicate_base)));
    ok &= expect_ok_event(
        "select",
        issue(dut, vector_alu(HOLON_NPU_ISA_OPCODE_VECTOR_ALU_SELECT, 3, 1, 2, 0))
    );
    ok &= expect_ok_event("select ptrue", issue(dut, predicate_ptrue()));
    ok &= expect_ok_event("select store", issue(dut, vector_store(3, select_base)));
    const std::uint32_t selected[] = {u32(10), u32(-2), u32(30), u32(-4)};
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ok &= expect_memory_word(dut, "select result", select_base + lane * 4, selected[lane]);
    }

    ok &= expect_ok_event("gather index load", issue(dut, vector_load(4, index_base)));
    ok &= expect_ok_event("gather", issue(dut, vector_gather(5, 1, 4)));
    ok &= expect_ok_event("gather store", issue(dut, vector_store(5, gather_base)));
    const std::uint32_t gathered[] = {u32(40), u32(10), u32(30), u32(20)};
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ok &= expect_memory_word(dut, "gather result", gather_base + lane * 4, gathered[lane]);
    }

    ok &= expect_ok_event("preserved destination load", issue(dut, vector_load(6, preserved_base)));
    ok &= expect_ok_event("invalid index load", issue(dut, vector_load(4, invalid_index_base)));
    ok &= expect_fault_event(
        "invalid gather",
        issue(dut, vector_gather(6, 1, 4)),
        HOLON_NPU_V2_FAULT_VECTOR_CONFIG
    );
    ok &= expect_ok_event("preserved destination store", issue(dut, vector_store(6, preserved_base)));
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ok &= expect_memory_word(dut, "gather fault atomicity", preserved_base + lane * 4, preserved[lane]);
    }
    return ok;
}

bool test_reduction_identities(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    constexpr std::uint32_t source_base = 0;
    constexpr std::uint32_t empty_predicate_base = 16;
    constexpr std::uint32_t result_base = 32;
    const std::uint32_t source[] = {u32(10), u32(-2), u32(30), u32(-4)};
    bool ok = true;
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ok &= expect_eq("reduction source write", host_write(dut, source_base + lane * 4, source[lane]), true);
    }
    ok &= expect_eq("empty predicate write", host_write(dut, empty_predicate_base, 0), true);
    ok &= expect_ok_event("reduction config", issue(dut, vector_config_i32(4)));
    ok &= expect_ok_event("reduction source load", issue(dut, vector_load(1, source_base)));
    ok &= expect_ok_event("reduction ptrue", issue(dut, predicate_ptrue()));
    ok &= expect_ok_event(
        "reduce sum",
        issue(dut, vector_reduce(HOLON_NPU_ISA_OPCODE_VECTOR_REDUCTION_SUM, 2, 1))
    );
    ok &= expect_ok_event(
        "reduce min",
        issue(dut, vector_reduce(HOLON_NPU_ISA_OPCODE_VECTOR_REDUCTION_MIN, 3, 1))
    );
    ok &= expect_ok_event(
        "reduce max",
        issue(dut, vector_reduce(HOLON_NPU_ISA_OPCODE_VECTOR_REDUCTION_MAX, 4, 1))
    );
    ok &= expect_ok_event("reduction scalar config", issue(dut, vector_config_i32(1)));
    ok &= expect_ok_event("sum store", issue(dut, vector_store(2, result_base)));
    ok &= expect_ok_event("min store", issue(dut, vector_store(3, result_base + 4)));
    ok &= expect_ok_event("max store", issue(dut, vector_store(4, result_base + 8)));
    ok &= expect_memory_word(dut, "sum result", result_base, u32(34));
    ok &= expect_memory_word(dut, "min result", result_base + 4, u32(-4));
    ok &= expect_memory_word(dut, "max result", result_base + 8, u32(30));

    ok &= expect_ok_event("empty identity config", issue(dut, vector_config_i32(4)));
    ok &= expect_ok_event("empty predicate load", issue(dut, predicate_load(0, empty_predicate_base)));
    ok &= expect_ok_event(
        "empty reduce sum",
        issue(dut, vector_reduce(HOLON_NPU_ISA_OPCODE_VECTOR_REDUCTION_SUM, 5, 1))
    );
    ok &= expect_ok_event(
        "empty reduce min",
        issue(dut, vector_reduce(HOLON_NPU_ISA_OPCODE_VECTOR_REDUCTION_MIN, 6, 1))
    );
    ok &= expect_ok_event(
        "empty reduce max",
        issue(dut, vector_reduce(HOLON_NPU_ISA_OPCODE_VECTOR_REDUCTION_MAX, 7, 1))
    );
    ok &= expect_ok_event("identity ptrue", issue(dut, predicate_ptrue()));
    ok &= expect_ok_event("identity scalar config", issue(dut, vector_config_i32(1)));
    ok &= expect_ok_event("sum identity store", issue(dut, vector_store(5, result_base + 12)));
    ok &= expect_ok_event("min identity store", issue(dut, vector_store(6, result_base + 16)));
    ok &= expect_ok_event("max identity store", issue(dut, vector_store(7, result_base + 20)));
    ok &= expect_memory_word(dut, "sum identity", result_base + 12, 0);
    ok &= expect_memory_word(dut, "min identity", result_base + 16, 0x7FFF'FFFFU);
    ok &= expect_memory_word(dut, "max identity", result_base + 20, 0x8000'0000U);
    return ok;
}

bool test_requantize(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    constexpr std::uint32_t source_base = 0;
    constexpr std::uint32_t command_base = 32;
    constexpr std::uint32_t result_base = 64;
    constexpr std::uint32_t bad_command_base = 96;
    const std::uint32_t source[] = {u32(3), u32(5), u32(-3), u32(100)};
    const std::uint32_t command[] = {u32(1), 1, u32(0), u32(-2), u32(3), 0};
    const std::uint32_t bad_command[] = {u32(1), 32, u32(0), u32(-2), u32(3), 0};
    bool ok = true;
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ok &= expect_eq("requant source write", host_write(dut, source_base + lane * 4, source[lane]), true);
    }
    for (std::uint32_t word = 0; word < 6; ++word) {
        ok &= expect_eq("requant command write", host_write(dut, command_base + word * 4, command[word]), true);
        ok &= expect_eq(
            "bad requant command write",
            host_write(dut, bad_command_base + word * 4, bad_command[word]),
            true
        );
    }
    ok &= expect_ok_event("requant config", issue(dut, vector_config_i32(4)));
    ok &= expect_ok_event("requant ptrue", issue(dut, predicate_ptrue()));
    ok &= expect_ok_event("requant source load", issue(dut, vector_load(1, source_base)));
    ok &= expect_ok_event(
        "requant",
        issue(dut, vector_requantize(2, 1, 0, command_base))
    );
    ok &= expect_ok_event("requant store", issue(dut, vector_store(2, result_base)));
    const std::uint32_t expected[] = {u32(2), u32(2), u32(-2), u32(3)};
    for (std::uint32_t lane = 0; lane < 4; ++lane) {
        ok &= expect_memory_word(dut, "requant result", result_base + lane * 4, expected[lane]);
    }
    ok &= expect_fault_event(
        "requant malformed command",
        issue(dut, vector_requantize(3, 1, 0, bad_command_base)),
        HOLON_NPU_V2_FAULT_VECTOR_CONFIG
    );
    return ok;
}

bool test_vector_faults(Vnpu_v2_vector_engine& dut) {
    reset(dut);

    bool ok = true;
    ok &= expect_fault_event(
        "zero vl fault",
        issue(dut, vector_config_i32(0)),
        HOLON_NPU_V2_FAULT_VECTOR_CONFIG
    );
    ok &= expect_ok_event("config i32", issue(dut, vector_config_i32(4)));
    ok &= expect_fault_event(
        "bounds fault",
        issue(dut, vector_load(1, kLocalMemBytes - 4)),
        HOLON_NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS
    );
    ok &= expect_fault_event(
        "predicate alignment fault",
        issue(dut, predicate_load(0, 2)),
        HOLON_NPU_V2_FAULT_LOCAL_MEMORY_BOUNDS
    );
    ok &= expect_fault_event(
        "illegal fault",
        issue(dut, encode(HOLON_NPU_ISA_CLASS_VECTOR_ALU, 0xFU, 1, 1, 2, 0)),
        HOLON_NPU_V2_FAULT_ILLEGAL_INSTRUCTION
    );
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    holon_npu_tb::test_run test{"npu_v2_vector_engine", argc, argv};
    Verilated::traceEverOn(false);

    Vnpu_v2_vector_engine dut;
    bool ok = true;
    ok &= test_vector_load_store_and_add(dut);
    ok &= test_vector_alu_compare_shift(dut);
    ok &= test_narrow_elements(dut);
    ok &= test_predicate_execution(dut);
    ok &= test_saturating_arithmetic(dut);
    ok &= test_select_and_gather(dut);
    ok &= test_pack_unpack_and_transpose(dut);
    ok &= test_reduction_identities(dut);
    ok &= test_requantize(dut);
    ok &= test_vector_faults(dut);
    dut.final();

    if (ok) {
        test.cover({
            holon_npu_tb::coverage_point::v2_vector_config,
            holon_npu_tb::coverage_point::v2_vector_load_store,
            holon_npu_tb::coverage_point::v2_vector_alu,
            holon_npu_tb::coverage_point::v2_vector_compare_shift,
            holon_npu_tb::coverage_point::v2_vector_narrow_elements,
            holon_npu_tb::coverage_point::v2_vector_predicate,
            holon_npu_tb::coverage_point::v2_vector_saturating_arithmetic,
            holon_npu_tb::coverage_point::v2_vector_select,
            holon_npu_tb::coverage_point::v2_vector_permute,
            holon_npu_tb::coverage_point::v2_vector_pack_unpack,
            holon_npu_tb::coverage_point::v2_vector_transpose,
            holon_npu_tb::coverage_point::v2_vector_reduction,
            holon_npu_tb::coverage_point::v2_vector_requant,
            holon_npu_tb::coverage_point::v2_vector_faults,
            holon_npu_tb::coverage_point::isa_class_vector_config,
            holon_npu_tb::coverage_point::isa_class_vector_alu,
            holon_npu_tb::coverage_point::isa_class_vector_memory,
            holon_npu_tb::coverage_point::isa_class_vector_permute,
            holon_npu_tb::coverage_point::isa_class_vector_reduction,
            holon_npu_tb::coverage_point::isa_class_quantization,
            holon_npu_tb::coverage_point::v2_vector_config_set,
            holon_npu_tb::coverage_point::v2_vector_load,
            holon_npu_tb::coverage_point::v2_vector_store,
            holon_npu_tb::coverage_point::v2_vector_alu_add,
            holon_npu_tb::coverage_point::v2_vector_alu_sub,
            holon_npu_tb::coverage_point::v2_vector_alu_min,
            holon_npu_tb::coverage_point::v2_vector_alu_max,
            holon_npu_tb::coverage_point::v2_vector_alu_eq,
            holon_npu_tb::coverage_point::v2_vector_alu_lt,
            holon_npu_tb::coverage_point::v2_vector_alu_shl,
            holon_npu_tb::coverage_point::v2_vector_alu_srl,
            holon_npu_tb::coverage_point::v2_vector_alu_sra,
            holon_npu_tb::coverage_point::v2_vector_alu_select,
            holon_npu_tb::coverage_point::v2_vector_permute_gather,
            holon_npu_tb::coverage_point::v2_vector_permute_zip_lo,
            holon_npu_tb::coverage_point::v2_vector_permute_zip_hi,
            holon_npu_tb::coverage_point::v2_vector_permute_unzip_even,
            holon_npu_tb::coverage_point::v2_vector_permute_unzip_odd,
            holon_npu_tb::coverage_point::v2_vector_permute_transpose4,
            holon_npu_tb::coverage_point::v2_vector_reduce_sum,
            holon_npu_tb::coverage_point::v2_vector_reduce_min,
            holon_npu_tb::coverage_point::v2_vector_reduce_max,
            holon_npu_tb::coverage_point::v2_quantization_requantize,
        });
    }
    return test.finish(ok);
}
