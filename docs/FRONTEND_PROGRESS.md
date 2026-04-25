# Frontend Progress

## What was advanced in this round

1. Parser now supports `if/else` and `while` statements.
2. Parser now supports `%` and `%=` operators.
3. Lambda parsing was fixed to avoid recursive parse deadloop.
4. Parser top-level loop now has forward-progress guard.
5. MIR builder now lowers:
   - variable declarations
   - expression statements
   - return statements
   - if/else control flow (`JumpIfFalse`, `Jump`)
   - while loop control flow
   - identifier/local/upvalue loads
   - assignment and compound assignment to locals/upvalues
6. Sema now visits `if/while` nodes for symbol and capture analysis.
7. Parser + sema + MIR now support `for` loops.
8. Parser + sema + MIR now support `switch/case/default` (via branch lowering).
9. `break` and `continue` are now supported in parser/sema/MIR for loops and switch.
10. Semantic rule refined: `continue` is only valid inside loops (not switch-only scope).
11. Compiler-side MIR verifier added (jump target + basic stack underflow checks).
12. Added Frontend golden test runner (`lpc_compiler --test`).

## Current sample result

On `build/sample_frontend.lpc`:

- compile succeeds
- functions: 1 (`main`)
- locals: 2
- upvalues: 1
- mir instructions: 27

On `build/sample_for_frontend.lpc`:

- compile succeeds
- functions: 1 (`main`)
- locals: 2
- upvalues: 0
- mir instructions: 18

On `build/sample_switch_frontend.lpc`:

- compile succeeds
- functions: 1 (`main`)
- locals: 1
- upvalues: 0
- mir instructions: 23
- VM startup path can execute generated entry and exit normally

On `build/sample_break_continue_frontend.lpc`:

- compile succeeds
- functions: 1 (`main`)
- locals: 2
- upvalues: 0
- mir instructions: 31
- VM executes and exits normally

On `build/invalid_continue_switch.lpc`:

- compile fails with semantic diagnostic:
  - `continue only valid inside loop`

Golden tests:

- command: `lpc_compiler --test`
- current status: PASS (10/10)
- current status: PASS (17/17)
- test sources now live in repository path: `compiler/tests/golden/*.lpc`

Added multi-module path cases:

- `compiler/tests/golden/modules/room/start.lpc`
- `compiler/tests/golden/modules/npc/vendor.lpc`

Added call-contract coverage:

- `compiler/tests/golden/invalid_call_arity.lpc` (warning path)

Grammar parity progress:

- `foreach (<id>[, <id>] in <expr>) <stmt>` parsing and sema/lowering path added.
- `class <Name> { ... }` and `new <Name>(...)` baseline parsing/sema/lowering path added.
- `inherit <id|string>;` parsing and sema path added.

Preprocessor progress:

- Added Frontend preprocess pass with support for:
  - `#include "..."`
  - `#define NAME value`
  - `#if / #elif / #else / #endif`
  - `#undef NAME`
  - condition expression subset: `defined(NAME)`, `!`, `&&`, `||`, `==`, `!=`

Semantic tightening:

- `new` on unknown class now reports compile error.

Added invalid semantic cases:

- `invalid_break_top.lpc`
- `invalid_continue_top.lpc`
- `invalid_undef_ident.lpc`

## Next

1. Tighten block-scoped symbol and shadowing diagnostics.
2. Distinguish capture source kind (parent-local vs parent-upvalue).
3. Introduce MIR to VM bytecode adapter.
