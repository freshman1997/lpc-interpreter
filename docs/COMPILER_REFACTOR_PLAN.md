# Compiler Refactor Plan (Frontend2)

This plan restructures compiler stages into maintainable layers.

## New pipeline

1. `Lexer` -> `Token[]`
2. `Parser2` -> `AST2`
3. `Sema2` -> `SemanticModel` (scope + capture metadata)
4. `Lowering` -> `MIR`
5. `BytecodeGen` (next step) -> legacy VM bytecode

## Added scaffold

- `compiler/include/frontend2/source.h`
- `compiler/include/frontend2/diagnostic.h`
- `compiler/include/frontend2/token.h`
- `compiler/include/frontend2/lexer.h`
- `compiler/include/frontend2/ast2.h`
- `compiler/include/frontend2/parser2.h`
- `compiler/include/frontend2/sema2.h`
- `compiler/include/frontend2/mir.h`
- `compiler/include/frontend2/pipeline.h`
- `compiler/src/frontend2/lexer.cpp`
- `compiler/src/frontend2/parser2.cpp`
- `compiler/src/frontend2/sema2.cpp`
- `compiler/src/frontend2/pipeline.cpp`
- `compiler/main_frontend2.cpp`

## Current behavior

- `lpc_compiler <source-file> [more files]` now runs frontend2 pipeline directly.
- Diagnostics are reported with file + line/column.
- MIR function list prints function locals/upvalue counts.

## Migration strategy

1. Grow frontend2 coverage until grammar parity with legacy parser.
2. Add MIR-to-bytecode backend adapter.
3. Remove legacy macro-based codegen path.

## Next implementation items

1. Pratt parser completeness (precedence and postfix set).
2. Full symbol table with scope ids and shadowing diagnostics.
3. Capture classification (parent-local vs parent-upvalue).
4. MIR block/control-flow representation.
5. Bytecode verifier before VM handoff.
