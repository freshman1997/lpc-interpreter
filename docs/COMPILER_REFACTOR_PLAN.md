# Compiler Refactor Plan (Frontend)

This plan restructures compiler stages into maintainable layers.

## New pipeline

1. `Lexer` -> `Token[]`
2. `Parser` -> `ast`
3. `Sema` -> `SemanticModel` (scope + capture metadata)
4. `Lowering` -> `MIR`
5. `BytecodeGen` -> nextvm bytecode, with older-runtime bytecode kept during migration

## Added scaffold

- `compiler/include/frontend/source.h`
- `compiler/include/frontend/diagnostic.h`
- `compiler/include/frontend/token.h`
- `compiler/include/frontend/lexer.h`
- `compiler/include/frontend/ast.h`
- `compiler/include/frontend/Parser.h`
- `compiler/include/frontend/Sema.h`
- `compiler/include/frontend/mir.h`
- `compiler/include/frontend/pipeline.h`
- `compiler/src/frontend/lexer.cpp`
- `compiler/src/frontend/Parser.cpp`
- `compiler/src/frontend/Sema.cpp`
- `compiler/src/frontend/pipeline.cpp`
- `compiler/main.cpp`

## Current behavior

- `lpc_compiler <source-file> [more files]` now runs frontend pipeline directly.
- Diagnostics are reported with file + line/column.
- MIR function list prints function locals/upvalue counts.

## Migration strategy

1. Grow frontend coverage until grammar parity with legacy parser.
2. Add MIR-to-bytecode backend adapter.
3. Remove legacy macro-based codegen path.

## Next implementation items

1. Pratt parser completeness (precedence and postfix set).
2. Full symbol table with scope ids and shadowing diagnostics.
3. Capture classification (parent-local vs parent-upvalue).
4. MIR block/control-flow representation.
5. Bytecode verifier before VM handoff.
