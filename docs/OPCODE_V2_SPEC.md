# Opcode V2 Specification Draft

This document defines a cleaner opcode model for long-term VM evolution.

## Design goals

1. Stable instruction semantics with explicit stack effect.
2. Separation between control flow, data operations, and runtime intrinsics.
3. Verifiable bytecode (bounds, operand width, stack behavior).
4. Easy lowering path from MIR and compatibility translator from current v1 bytecode.

## Core groups

### A. Data movement

- `LoadConst idx`      stack: +1
- `LoadLocal idx`      stack: +1
- `StoreLocal idx`     stack: -1
- `LoadGlobal idx`     stack: +1
- `StoreGlobal idx`    stack: -1
- `LoadUpvalue idx`    stack: +1
- `StoreUpvalue idx`   stack: -1

### B. Arithmetic and logic

- `Add/Sub/Mul/Div/Mod` stack: -1
- `Neg/Not/BitNot`       stack:  0
- `BitAnd/BitOr/BitXor/Shl/Shr` stack: -1

### C. Compare and branching

- `Eq/Ne/Lt/Le/Gt/Ge` stack: -1
- `Jump target` stack: 0
- `JumpIfFalse target` stack: -1

### D. Calls

- `CallDirect func_id argc` stack: -argc + ret
- `CallValue argc`          stack: -argc -1 + ret
- `CallIntrinsic id argc`   stack: -argc + ret
- `Return`                  stack: -ret

### E. Aggregates and object model

- `MakeArray n`
- `MakeMap n`
- `GetIndex`
- `SetIndex`
- `Slice`
- `MakeObject class_id`
- `GetField field_id`
- `SetField field_id`

## Operand encoding

- fixed-width `u8 opcode`
- instruction-specific operand layout in little-endian
- verifier must reject malformed trailing bytes

## Stack effect table

Each opcode must define:

- min required stack depth before execution
- delta after execution
- may-throw flag
- may-allocate flag

This table is required for bytecode verifier.

## v1 to v2 mapping (draft)

- `op_load_global` -> `LoadGlobal`
- `op_store_global` -> `StoreGlobal`
- `op_load_local` -> `LoadLocal`
- `op_store_local` -> `StoreLocal`
- `op_load_iconst/op_load_fconst/op_load_sconst/op_load_0/op_load_1` -> `LoadConst`
- `op_add/op_sub/op_mul/op_div/op_mod` -> `Add/Sub/Mul/Div/Mod`
- `op_binary_*` -> `Bit*` / `Shl` / `Shr`
- `op_cmp_*` -> compare ops
- `op_test` -> `JumpIfFalse`
- `op_goto` -> `Jump`
- `op_call(type=3)` -> `CallDirect`
- `op_call(type=0)` -> `CallValue`
- `op_call(type=1|2)` -> `CallIntrinsic` (translator layer)
- `op_set_upvalue/op_get_upvalue` -> `StoreUpvalue/LoadUpvalue`
- `op_new_array/op_new_mapping/op_index/op_sub_arr/op_upset` -> aggregate group
- class ops -> object group

## Notes

- v2 is not enabled yet; this is a migration contract.
- v1 verifier should be added first to improve safety before full migration.
