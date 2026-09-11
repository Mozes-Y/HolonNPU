<!-- Generated from spec/holon_npu_isa.json by tools/gen_isa.py. Do not edit. -->
# HolonNPU Scalar ISA Reference

Current executable research baseline, not a released hardware ABI.
Behavioral authority: [ISA](ISA.md).

- Scalar profile: `rv32im_zicsr`, `ilp32`.
- Environment: `single_hart_machine`.
- Alignment: 4 bytes; byte order: little.
- Low bits `11`: 4-byte scalar word.
- Low bits `00/01/10`: 8-byte Holon frame.
- ELF base: `rv32i2p1`; stack alignment: 16.

## Scalar Instructions

| Instruction | Extension | Format | Match | Mask |
| --- | --- | --- | --- | --- |
| `LUI` | I | `u` | `0x00000037u` | `0x0000007Fu` |
| `AUIPC` | I | `u` | `0x00000017u` | `0x0000007Fu` |
| `JAL` | I | `j` | `0x0000006Fu` | `0x0000007Fu` |
| `JALR` | I | `i` | `0x00000067u` | `0x0000707Fu` |
| `BEQ` | I | `b` | `0x00000063u` | `0x0000707Fu` |
| `BNE` | I | `b` | `0x00001063u` | `0x0000707Fu` |
| `BLT` | I | `b` | `0x00004063u` | `0x0000707Fu` |
| `BGE` | I | `b` | `0x00005063u` | `0x0000707Fu` |
| `BLTU` | I | `b` | `0x00006063u` | `0x0000707Fu` |
| `BGEU` | I | `b` | `0x00007063u` | `0x0000707Fu` |
| `LB` | I | `i` | `0x00000003u` | `0x0000707Fu` |
| `LH` | I | `i` | `0x00001003u` | `0x0000707Fu` |
| `LW` | I | `i` | `0x00002003u` | `0x0000707Fu` |
| `LBU` | I | `i` | `0x00004003u` | `0x0000707Fu` |
| `LHU` | I | `i` | `0x00005003u` | `0x0000707Fu` |
| `SB` | I | `s` | `0x00000023u` | `0x0000707Fu` |
| `SH` | I | `s` | `0x00001023u` | `0x0000707Fu` |
| `SW` | I | `s` | `0x00002023u` | `0x0000707Fu` |
| `ADDI` | I | `i` | `0x00000013u` | `0x0000707Fu` |
| `SLTI` | I | `i` | `0x00002013u` | `0x0000707Fu` |
| `SLTIU` | I | `i` | `0x00003013u` | `0x0000707Fu` |
| `XORI` | I | `i` | `0x00004013u` | `0x0000707Fu` |
| `ORI` | I | `i` | `0x00006013u` | `0x0000707Fu` |
| `ANDI` | I | `i` | `0x00007013u` | `0x0000707Fu` |
| `SLLI` | I | `shift` | `0x00001013u` | `0xFE00707Fu` |
| `SRLI` | I | `shift` | `0x00005013u` | `0xFE00707Fu` |
| `SRAI` | I | `shift` | `0x40005013u` | `0xFE00707Fu` |
| `ADD` | I | `r` | `0x00000033u` | `0xFE00707Fu` |
| `SUB` | I | `r` | `0x40000033u` | `0xFE00707Fu` |
| `SLL` | I | `r` | `0x00001033u` | `0xFE00707Fu` |
| `SLT` | I | `r` | `0x00002033u` | `0xFE00707Fu` |
| `SLTU` | I | `r` | `0x00003033u` | `0xFE00707Fu` |
| `XOR` | I | `r` | `0x00004033u` | `0xFE00707Fu` |
| `SRL` | I | `r` | `0x00005033u` | `0xFE00707Fu` |
| `SRA` | I | `r` | `0x40005033u` | `0xFE00707Fu` |
| `OR` | I | `r` | `0x00006033u` | `0xFE00707Fu` |
| `AND` | I | `r` | `0x00007033u` | `0xFE00707Fu` |
| `FENCE` | I | `fence` | `0x0000000Fu` | `0x0000707Fu` |
| `ECALL` | I | `system` | `0x00000073u` | `0xFFFFFFFFu` |
| `EBREAK` | I | `system` | `0x00100073u` | `0xFFFFFFFFu` |
| `MUL` | M | `r` | `0x02000033u` | `0xFE00707Fu` |
| `MULH` | M | `r` | `0x02001033u` | `0xFE00707Fu` |
| `MULHSU` | M | `r` | `0x02002033u` | `0xFE00707Fu` |
| `MULHU` | M | `r` | `0x02003033u` | `0xFE00707Fu` |
| `DIV` | M | `r` | `0x02004033u` | `0xFE00707Fu` |
| `DIVU` | M | `r` | `0x02005033u` | `0xFE00707Fu` |
| `REM` | M | `r` | `0x02006033u` | `0xFE00707Fu` |
| `REMU` | M | `r` | `0x02007033u` | `0xFE00707Fu` |
| `CSRRW` | Zicsr | `csr` | `0x00001073u` | `0x0000707Fu` |
| `CSRRS` | Zicsr | `csr` | `0x00002073u` | `0x0000707Fu` |
| `CSRRC` | Zicsr | `csr` | `0x00003073u` | `0x0000707Fu` |
| `CSRRWI` | Zicsr | `csr_immediate` | `0x00005073u` | `0x0000707Fu` |
| `CSRRSI` | Zicsr | `csr_immediate` | `0x00006073u` | `0x0000707Fu` |
| `CSRRCI` | Zicsr | `csr_immediate` | `0x00007073u` | `0x0000707Fu` |
| `MRET` | machine | `system` | `0x30200073u` | `0xFFFFFFFFu` |
| `WFI` | machine | `system` | `0x10500073u` | `0xFFFFFFFFu` |

## Traps

| Cause | Value |
| --- | --- |
| `instruction_address_misaligned` | 0 |
| `instruction_access_fault` | 1 |
| `illegal_instruction` | 2 |
| `breakpoint` | 3 |
| `load_address_misaligned` | 4 |
| `load_access_fault` | 5 |
| `store_address_misaligned` | 6 |
| `store_access_fault` | 7 |
| `machine_environment_call` | 11 |

## Machine CSRs

| CSR | Address | Reset | Write mask |
| --- | --- | --- | --- |
| `mstatus` | `0x300` | `0x1800` | `0x88` |
| `misa` | `0x301` | `0x40001100` | `0x0` |
| `mie` | `0x304` | `0x0` | `0x888` |
| `mtvec` | `0x305` | `0x0` | `0xffffffff` |
| `mstatush` | `0x310` | `0x0` | `0x0` |
| `mcountinhibit` | `0x320` | `0x0` | `0x5` |
| `mscratch` | `0x340` | `0x0` | `0xffffffff` |
| `mepc` | `0x341` | `0x0` | `0xfffffffc` |
| `mcause` | `0x342` | `0x0` | `0xffffffff` |
| `mtval` | `0x343` | `0x0` | `0xffffffff` |
| `mip` | `0x344` | `0x0` | `0x0` |
| `mcycle` | `0xb00` | `0x0` | `0xffffffff` |
| `minstret` | `0xb02` | `0x0` | `0xffffffff` |
| `mcycleh` | `0xb80` | `0x0` | `0xffffffff` |
| `minstreth` | `0xb82` | `0x0` | `0xffffffff` |
| `mvendorid` | `0xf11` | `0x0` | `0x0` |
| `marchid` | `0xf12` | `0x0` | `0x0` |
| `mimpid` | `0xf13` | `0x0` | `0x0` |
| `mhartid` | `0xf14` | `0x0` | `0x0` |
| `mconfigptr` | `0xf15` | `0x0` | `0x0` |

HPM counter/selector zero ranges:
- `0x323..0x33f`.
- `0xb03..0xb1f`.
- `0xb83..0xb9f`.
