<!-- Generated from spec/holon_npu_isa.json; do not edit. -->
# Holon NPU Operand Reference

Current executable research operand contract.
State, arithmetic and fault authority: [ISA](ISA.md).

Low bits: vector/predicate=00, matrix=01, DMA/system=10. Opcode is bits 11:2.
Unused fields and unlisted opcodes are illegal. Format fields below are role:lsb:width.

| Format | Fields |
| --- | --- |
| `set_length` | `rd:12:5`, `avl:17:5`, `type:42:4`, `peer_type:47:4` |
| `binary` | `vd:12:5`, `va:17:5`, `vb:22:5`, `pg:32:5`, `vl:37:5`, `type:42:4`, `policy:46:1` |
| `ternary` | `vd:12:5`, `va:17:5`, `vb:22:5`, `vc:27:5`, `pg:32:5`, `vl:37:5`, `type:42:4`, `policy:46:1` |
| `unary` | `vd:12:5`, `va:17:5`, `pg:32:5`, `vl:37:5`, `type:42:4`, `policy:46:1` |
| `convert` | `vd:12:5`, `va:17:5`, `pg:32:5`, `vl:37:5`, `type:42:4`, `policy:46:1`, `result_type:47:4`, `rounding:51:2` |
| `compare` | `pd:12:5`, `va:17:5`, `vb:22:5`, `pg:32:5`, `vl:37:5`, `type:42:4` |
| `select` | `vd:12:5`, `va:17:5`, `vb:22:5`, `select:27:5`, `pg:32:5`, `vl:37:5`, `type:42:4`, `policy:46:1` |
| `broadcast` | `vd:12:5`, `value:17:5`, `pg:32:5`, `vl:37:5`, `type:42:4`, `policy:46:1` |
| `extract` | `rd:12:5`, `va:17:5`, `index:22:5`, `vl:37:5`, `type:42:4` |
| `permute` | `vd:12:5`, `va:17:5`, `indices:22:5`, `pg:32:5`, `vl:37:5`, `type:42:4`, `policy:46:1` |
| `reduce` | `rd:12:5`, `va:17:5`, `seed:22:5`, `pg:32:5`, `vl:37:5`, `type:42:4` |
| `load` | `vd:12:5`, `base:17:5`, `pg:27:5`, `vl:32:5`, `type:37:4`, `policy:41:1`, `offset:44:20` |
| `store` | `va:12:5`, `base:17:5`, `pg:27:5`, `vl:32:5`, `type:37:4`, `offset:44:20` |
| `strided_load` | `vd:12:5`, `base:17:5`, `stride:22:5`, `pg:27:5`, `vl:32:5`, `type:37:4`, `policy:41:1`, `offset:44:20` |
| `strided_store` | `va:12:5`, `base:17:5`, `stride:22:5`, `pg:27:5`, `vl:32:5`, `type:37:4`, `offset:44:20` |
| `gather` | `vd:12:5`, `base:17:5`, `indices:22:5`, `pg:27:5`, `vl:32:5`, `type:37:4`, `policy:41:1`, `scale:42:2`, `offset:44:20` |
| `scatter` | `va:12:5`, `base:17:5`, `indices:22:5`, `pg:27:5`, `vl:32:5`, `type:37:4`, `scale:42:2`, `offset:44:20` |
| `ptrue` | `pd:12:5`, `vl:37:5` |
| `pwhile` | `pd:12:5`, `base:17:5`, `end:22:5`, `vl:37:5` |
| `predicate_binary` | `pd:12:5`, `pa:17:5`, `pb:22:5`, `vl:37:5` |
| `predicate_unary` | `pd:12:5`, `pa:17:5`, `vl:37:5` |
| `predicate_query` | `rd:12:5`, `pa:17:5`, `vl:37:5` |
| `predicate_load` | `pd:12:5`, `base:17:5`, `vl:32:5`, `offset:44:20` |
| `predicate_store` | `pa:12:5`, `base:17:5`, `vl:32:5`, `offset:44:20` |
| `view` | `view:12:4`, `base:16:5`, `rows:21:5`, `cols:26:5`, `row_stride:31:5`, `col_stride:36:5`, `type:41:4` |
| `tile_load` | `td:12:3`, `view:15:4` |
| `tile_store` | `ts:12:3`, `view:15:4` |
| `tile_clear` | `td:12:3`, `rows:17:5`, `cols:22:5`, `type:42:4` |
| `dot` | `td:12:3`, `ta:15:3`, `tb:18:3`, `type:21:4` |
| `dma` | `dst:12:5`, `src:17:5`, `count:22:5` |
| `stop` | `status:12:5` |
| `caps` | `rd:12:5`, `selector:17:2` |

| Instruction | Family | Opcode | Format | Type domain | Semantic contract |
| --- | --- | --- | --- | --- | --- |
| `VSETL` | vector | 31 | `set_length` | i8, u8, i16, u16, i32, u32, f32 | `bounded_explicit_length` |
| `VADD` | vector | 1 | `binary` | i8, u8, i16, u16, i32, u32, f32 | `lane_add` |
| `VSUB` | vector | 2 | `binary` | i8, u8, i16, u16, i32, u32, f32 | `lane_subtract` |
| `VMUL` | vector | 3 | `binary` | i8, u8, i16, u16, i32, u32, f32 | `lane_multiply` |
| `VMULH` | vector | 27 | `binary` | i8, u8, i16, u16, i32, u32 | `lane_product_high_half` |
| `VMIN` | vector | 4 | `binary` | i8, u8, i16, u16, i32, u32, f32 | `lane_minimum` |
| `VMAX` | vector | 5 | `binary` | i8, u8, i16, u16, i32, u32, f32 | `lane_maximum` |
| `VDIV` | vector | 6 | `binary` | f32 | `binary32_divide` |
| `VSQRT` | vector | 7 | `unary` | f32 | `binary32_square_root` |
| `VFMA` | vector | 8 | `ternary` | f32 | `binary32_fused_multiply_add` |
| `VAND` | vector | 9 | `binary` | i8, u8, i16, u16, i32, u32 | `lane_bitwise_and` |
| `VOR` | vector | 10 | `binary` | i8, u8, i16, u16, i32, u32 | `lane_bitwise_or` |
| `VXOR` | vector | 11 | `binary` | i8, u8, i16, u16, i32, u32 | `lane_bitwise_xor` |
| `VSHL` | vector | 12 | `binary` | i8, u8, i16, u16, i32, u32 | `lane_shift_left` |
| `VSHR` | vector | 13 | `binary` | i8, u8, i16, u16, i32, u32 | `lane_shift_logical_right` |
| `VASHR` | vector | 14 | `binary` | i8, i16, i32 | `lane_shift_arithmetic_right` |
| `VCONVERT` | vector | 15 | `convert` | i8, u8, i16, u16, i32, u32, f32 | `rounded_saturating_conversion` |
| `VCMPEQ` | vector | 16 | `compare` | i8, u8, i16, u16, i32, u32, f32 | `compare_equal` |
| `VCMPLT` | vector | 17 | `compare` | i8, u8, i16, u16, i32, u32, f32 | `compare_less` |
| `VCMPLE` | vector | 18 | `compare` | i8, u8, i16, u16, i32, u32, f32 | `compare_less_equal` |
| `VCMPNE` | vector | 19 | `compare` | i8, u8, i16, u16, i32, u32, f32 | `compare_not_equal` |
| `VSELECT` | vector | 20 | `select` | i8, u8, i16, u16, i32, u32, f32 | `predicated_select` |
| `VBROADCAST` | vector | 21 | `broadcast` | i8, u8, i16, u16, i32, u32, f32 | `scalar_bit_broadcast` |
| `VEXTRACT` | vector | 22 | `extract` | i8, u8, i16, u16, i32, u32, f32 | `lane_to_scalar` |
| `VPERMUTE` | vector | 23 | `permute` | i8, u8, i16, u16, i32, u32, f32 | `indexed_lane_permutation` |
| `VREDSUM` | vector | 24 | `reduce` | i8, u8, i16, u16, i32, u32, f32 | `ordered_seeded_sum` |
| `VREDMIN` | vector | 25 | `reduce` | i8, u8, i16, u16, i32, u32, f32 | `seeded_minimum` |
| `VREDMAX` | vector | 26 | `reduce` | i8, u8, i16, u16, i32, u32, f32 | `seeded_maximum` |
| `VLD` | vector | 32 | `load` | i8, u8, i16, u16, i32, u32, f32 | `local_contiguous_load` |
| `VST` | vector | 33 | `store` | i8, u8, i16, u16, i32, u32, f32 | `local_contiguous_store` |
| `VLDS` | vector | 34 | `strided_load` | i8, u8, i16, u16, i32, u32, f32 | `local_strided_load` |
| `VSTS` | vector | 35 | `strided_store` | i8, u8, i16, u16, i32, u32, f32 | `local_strided_store` |
| `VGATHER` | vector | 36 | `gather` | i8, u8, i16, u16, i32, u32, f32 | `local_indexed_load` |
| `VSCATTER` | vector | 37 | `scatter` | i8, u8, i16, u16, i32, u32, f32 | `local_indexed_store` |
| `PTRUE` | vector | 64 | `ptrue` | - | `predicate_all_active` |
| `PWHILELT` | vector | 65 | `pwhile` | - | `predicate_unsigned_range` |
| `PAND` | vector | 66 | `predicate_binary` | - | `predicate_and` |
| `POR` | vector | 67 | `predicate_binary` | - | `predicate_or` |
| `PXOR` | vector | 68 | `predicate_binary` | - | `predicate_xor` |
| `PNOT` | vector | 69 | `predicate_unary` | - | `predicate_not` |
| `PCOUNT` | vector | 70 | `predicate_query` | - | `predicate_count` |
| `PFIRST` | vector | 71 | `predicate_query` | - | `predicate_first_or_minus_one` |
| `PLD` | vector | 72 | `predicate_load` | - | `packed_predicate_load` |
| `PST` | vector | 73 | `predicate_store` | - | `packed_predicate_store` |
| `MVIEW` | matrix | 0 | `view` | i8, u8, i16, u16, i32, u32, f32 | `capture_tile_view` |
| `MLOAD` | matrix | 1 | `tile_load` | - | `load_matrix_register` |
| `MCLEAR` | matrix | 2 | `tile_clear` | i8, u8, i16, u16, i32, u32, f32 | `clear_matrix_register` |
| `MDOT` | matrix | 3 | `dot` | i32, u32, f32 | `matrix_product` |
| `MMACC` | matrix | 4 | `dot` | i32, u32, f32 | `matrix_accumulation` |
| `MSTORE` | matrix | 5 | `tile_store` | - | `store_matrix_register` |
| `DLOAD` | system | 0 | `dma` | - | `system_to_local` |
| `DSTORE` | system | 1 | `dma` | - | `local_to_system` |
| `STOP` | system | 3 | `stop` | - | `retire_program_stop` |
| `CAPS` | system | 4 | `caps` | - | `query_resource_capacity` |

| Role | Domain |
| --- | --- |
| `rd` | scalar |
| `avl` | scalar |
| `vl` | scalar |
| `base` | scalar |
| `end` | scalar |
| `stride` | scalar |
| `index` | scalar |
| `seed` | scalar |
| `value` | scalar |
| `rows` | scalar |
| `cols` | scalar |
| `row_stride` | scalar |
| `col_stride` | scalar |
| `dst` | scalar |
| `src` | scalar |
| `count` | scalar |
| `status` | scalar |
| `vd` | vector |
| `va` | vector |
| `vb` | vector |
| `vc` | vector |
| `indices` | vector |
| `pd` | predicate |
| `pa` | predicate |
| `pb` | predicate |
| `pg` | predicate |
| `select` | predicate |
| `td` | tile |
| `ta` | tile |
| `tb` | tile |
| `ts` | tile |
| `view` | view |
| `type` | element |
| `peer_type` | element |
| `result_type` | element |
| `policy` | policy |
| `rounding` | rounding |
| `offset` | displacement |
| `scale` | scale |
| `selector` | capability |

| Domain | Values |
| --- | --- |
| types | `i8`=0, `u8`=1, `i16`=2, `u16`=3, `i32`=4, `u32`=5, `f32`=6 |
| policies | `merge`=0, `zero`=1 |
| roundings | `rne`=0, `rtz`=1, `rdn`=2, `rup`=3 |
| capacities | `vector_bytes`=0, `matrix_rows`=1, `matrix_cols`=2, `tile_bytes`=3 |
| register_counts | `scalar`=32, `vector`=32, `predicate`=32, `tile`=8, `view`=16 |
| traps | `invalid_operand`=24 |
