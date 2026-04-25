# Breaking Migration: Legacy Compiler Removed

The legacy compiler pipeline was removed and replaced by Frontend as default.

## Removed components

- legacy scanner/parser/codegen implementation
- old compiler headers tied to macro-based generation
- old `compiler/main.cpp` entry

## New default compiler

- binary: `lpc_compiler`
- command:

```text
lpc_compiler <source-file> [source-file2 ...]
```

## Current output behavior

- parses input and runs semantic analysis
- prints diagnostics with file and location
- prints function summary (locals/upvalues)

## Follow-up required

- MIR to bytecode lowering is still in-progress and not yet replacing VM bytecode emitter.
- Full grammar parity with legacy parser is in-progress.
