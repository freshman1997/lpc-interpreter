# MIR Optimization Passes

## Overview

The LPC compiler uses a flat (non-SSA) MIR intermediate representation for optimization. All passes operate on `MirFunction` — a linear sequence of `MirInstr` with jump targets encoded as absolute PC offsets.

The optimization pipeline runs up to 4 fixed-point iterations. Each iteration applies passes in order; if any pass reports a change, the iteration loop continues. If no pass changes anything, it stops early.

## Pipeline Order

```
for iter in 0..4:
    1. LocalConstPropagation
    2. ConstantFoldAndSimplify
    3. FloatConstantFold
    4. StringConstantFold
    5. UnaryConstantFold
    6. StrengthReduce
    7. FloatAlgebraicSimplify
    8. DoubleNegationElim
    9. CompareLogicNotMerge
    10. SelfComparisonSimplify
    11. DeadStoreElim
    12. ConditionalJumpMerge
    13. FloatConstBranchFold
    if iter == 0:
        14. LocalCSE
        15. PeepholeAndBranchSimplify
    16. RemoveUnreachable
    if no changes: break
```

Passes 14–15 run only on iteration 0 because they involve more expensive analysis and their results are unlikely to unlock further constant folding.

## Pass Details

### 1. LocalConstPropagation

**File**: `mir_opt.cpp:LocalConstPropagation`

**What**: Tracks which local variables hold known integer constants. Replaces `LoadLocal` with `LoadConst` when the value is known.

**How**: Single forward scan. At `StoreLocal x` preceded by `LoadConst c`, marks `x` as known with value `c`. At `LoadLocal x` where `x` is known, rewrites to `LoadConst`. Knowledge is cleared at branch targets, jumps, returns, and after any non-constant `StoreLocal`.

**Example**:
```
LoadConst 42       LoadConst 42
StoreLocal x       StoreLocal x
LoadLocal x   →    LoadConst 42      ← propagated
Add                Add
```

### 2. ConstantFoldAndSimplify

**File**: `mir_opt.cpp:ConstantFoldAndSimplify`

**What**: Folds integer binary operations on two constants, plus algebraic identity simplification.

**Constant folding**: `LoadConst a; LoadConst b; <binop>` → `LoadConst result; LoadConst 0; Pop`

**Algebraic identities** (operand on stack below the operator):
- `Add 0` → `Pop` (x + 0 = x)
- `Sub 0` → `Pop` (x - 0 = x)
- `Mul 1` → `Pop` (x * 1 = x)
- `Div 1` → `Pop` (x / 1 = x)

**Example**:
```
LoadConst 3        LoadConst 7
LoadConst 4   →    LoadConst 0
Add                Pop
```

### 3. FloatConstantFold

**File**: `mir_opt.cpp:FloatConstantFold`

**What**: Folds binary operations on two float constants.

**Rules**: Same as integer constant folding but for `LoadFConst`. Comparison results are emitted as `LoadConst` (integer 0/1). Division by 0.0 is not folded.

**Example**:
```
LoadFConst 2.5     LoadFConst 5.0
LoadFConst 2.5 →   LoadConst 0
Mul                Pop
```

### 4. StringConstantFold

**File**: `mir_opt.cpp:StringConstantFold`

**What**: Folds operations on two string constants.

**Concatenation**: `LoadSConst a; LoadSConst b; Add` → `LoadSConst (a+b)`

**Comparison**: `LoadSConst a; LoadSConst b; Eq/Neq` → `LoadConst 0/1`

**Example**:
```
LoadSConst "hello"   LoadSConst "hello world"
LoadSConst " world" → LoadConst 0
Add                   Pop
```

### 5. UnaryConstantFold

**File**: `mir_opt.cpp:UnaryConstantFold`

**What**: Folds unary operations on constants.

**Rules**:
- `LoadConst v; Neg` → `LoadConst (-v)`
- `LoadConst v; BitNot` → `LoadConst (~v)`
- `LoadConst v; LogicNot` → `LoadConst (v==0 ? 1 : 0)`
- `LoadFConst v; Neg` → `LoadFConst (-v)`

### 6. StrengthReduce

**File**: `mir_opt.cpp:StrengthReduce`

**What**: Replaces expensive operations with cheaper equivalents.

**Rules**:
- `Mul 0` → `Pop` (result is 0)
- `BitAnd 0` → `Pop` (result is 0)
- `BitOr 0` → `Pop` (result is other operand)
- `Mul N` where N is a power of 2 → `Shl log2(N)`
- `Div N` where N is a power of 2 → `Shr log2(N)`

**Example**:
```
LoadConst 8     LoadConst 3
LoadConst x  →  LoadLocal x
Mul             Shl
```

### 7. FloatAlgebraicSimplify

**File**: `mir_opt.cpp:FloatAlgebraicSimplify`

**What**: Algebraic identity simplification for float operations.

**Rules**:
- `LoadFConst 0.0; Add/Sub` → `Pop` (x ± 0.0 = x)
- `LoadFConst 1.0; Mul/Div` → `Pop` (x * 1.0 = x, x / 1.0 = x)

### 8. DoubleNegationElim

**File**: `mir_opt.cpp:DoubleNegationElim`

**What**: Eliminates double negation patterns where the two cancel out.

**Rules**:
- `Neg; Neg` → `Pop; Pop` (identity)
- `BitNot; BitNot` → `Pop; Pop` (identity)
- `LogicNot; LogicNot` → `Pop; Pop` (identity)

### 9. CompareLogicNotMerge

**File**: `mir_opt.cpp:CompareLogicNotMerge`

**What**: Merges a comparison followed by `LogicNot` into the inverted comparison.

**Rules**:
- `Eq; LogicNot` → `Neq`
- `Neq; LogicNot` → `Eq`
- `Lt; LogicNot` → `Gte`
- `Lte; LogicNot` → `Gt`
- `Gt; LogicNot` → `Lte`
- `Gte; LogicNot` → `Lt`

**Example**:
```
Lt           Gte
LogicNot  →  Pop
```

### 10. SelfComparisonSimplify

**File**: `mir_opt.cpp:SelfComparisonSimplify`

**What**: Simplifies comparisons of a local variable with itself.

**Rules**: `LoadLocal x; LoadLocal x; <cmp>` → `LoadConst result`

| Comparison | Result |
|-----------|--------|
| `Eq`, `Lte`, `Gte` | 1 |
| `Neq`, `Lt`, `Gt` | 0 |

### 11. DeadStoreElim

**File**: `mir_opt.cpp:DeadStoreElim`

**What**: Removes `StoreLocal` instructions that are overwritten before the value is read.

**How**: For each `StoreLocal x`, scans forward within the same basic block. If another `StoreLocal x` is found before any `LoadLocal x`, the first store is dead and removed.

**Example**:
```
LoadConst 1       (removed)
StoreLocal x      (removed)
LoadConst 2   →   LoadConst 2
StoreLocal x      StoreLocal x
```

### 12. ConditionalJumpMerge

**File**: `mir_opt.cpp:ConditionalJumpMerge`

**What**: Merges a conditional jump followed by an unconditional jump into a single conditional jump with inverted condition.

**Pattern**: `JumpIfFalse L1; Jump L2; L1:` → `JumpIfTrue L2`

Also handles the `JumpIfTrue` variant (inverted to `JumpIfFalse`).

### 13. FloatConstBranchFold

**File**: `mir_opt.cpp:FloatConstBranchFold`

**What**: Folds conditional branches on known float constants.

**Rules**:
- `LoadFConst 0.0; JumpIfFalse L` → `Jump L` (always false → always jump)
- `LoadFConst 0.0; JumpIfTrue L` → removed (never jumps)
- `LoadFConst nonzero; JumpIfTrue L` → `Jump L` (always true → always jump)
- `LoadFConst nonzero; JumpIfFalse L` → removed (never jumps)

### 14. LocalCSE (iteration 0 only)

**File**: `mir_opt.cpp:LocalCSE`

**What**: Eliminates redundant local variable loads when the same local is loaded twice in sequence.

**Rule**: `LoadLocal x; LoadLocal x` → `LoadLocal x; Dup`

**Why iteration 0 only**: The `Dup` replacement is a peephole pattern; further iterations are unlikely to produce new opportunities.

### 15. PeepholeAndBranchSimplify (iteration 0 only)

**File**: `mir_opt.cpp:PeepholeAndBranchSimplify`

**What**: Collection of peephole optimizations and branch simplifications.

**Jump threading**: Chains of `Jump` instructions are collapsed to a single jump to the final target.

**Jump-to-next**: `Jump PC+1` is removed.

**LoadLocal+StoreLocal elimination**: `LoadLocal x; StoreLocal x` (no-op copy) is removed.

**LoadConst+Pop elimination**: `LoadConst c; Pop` is removed (dead constant).

**Branch on constant**:
- `LoadConst 0; JumpIfFalse L` → `Jump L`
- `LoadConst nonzero; JumpIfFalse L` → removed
- `LoadConst nonzero; JumpIfTrue L` → `Jump L`
- `LoadConst 0; JumpIfTrue L` → removed

### 16. RemoveUnreachable

**File**: `mir_opt.cpp:RemoveUnreachable`

**What**: Removes code that cannot be reached from the function entry point.

**How**: BFS from PC 0 following control flow. Any instruction not visited is removed. Jump targets are remapped to reflect the compacted code.

## Infrastructure

### BuildTargetMap

Shared helper that computes `is_target[]` — a bitmask indicating which PCs are jump targets. Used by most passes to avoid optimizing across basic block boundaries.

### AddIConst / AddFConst / AddSConst

Constant pool deduplication. Before adding a new constant, checks if an identical value already exists in the pool. Returns the index of the existing or new entry.

### CompactWithKeepMask

Instruction compaction utility. Given a `keep[]` mask, removes masked-out instructions and remaps all jump targets. Used by `PeepholeAndBranchSimplify` and `DeadStoreElim`.

### InvertCmp

Maps a comparison operator to its logical inverse:
`Eq↔Neq`, `Lt↔Gte`, `Lte↔Gt`.

## Statistics

`MirOptStats` tracks optimization pass effectiveness:

| Field | Meaning |
|-------|---------|
| `function_count` | Number of functions optimized |
| `before_instr` / `after_instr` | Total instruction count before/after |
| `constprop_changed` | How many times const propagation made changes |
| `constprop_instr_delta` | Instructions removed by const propagation |
| `constfold_changed` | How many times any constant folding pass made changes |
| `constfold_instr_delta` | Instructions removed by all folding passes combined |
| `peephole_changed` | How many times peephole made changes |
| `peephole_instr_delta` | Instructions removed by peephole |
| `cse_changed` | How many times CSE made changes |
| `cse_instr_delta` | Instructions removed by CSE |
| `unreachable_changed` | How many times unreachable code was removed |
| `unreachable_instr_delta` | Instructions removed by unreachable pass |

Accessed via `OptimizeMirModuleWithStats()`.

## Source Files

| File | Purpose |
|------|---------|
| `compiler/include/frontend/mir.h` | `MirOp` enum, `MirInstr`, `MirFunction`, `MirModule` |
| `compiler/include/frontend/mir_opt.h` | `MirOptStats`, `OptimizeMirModule()`, `OptimizeMirModuleWithStats()` |
| `compiler/src/frontend/mir_opt.cpp` | All 16 optimization passes + pipeline |
