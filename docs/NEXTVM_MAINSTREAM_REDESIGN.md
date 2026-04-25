# nextvm Mainstream Runtime Redesign

This document tracks the new LPC runtime direction now named `nextvm`.
It treats the new runtime as a staged mainstream implementation with a
temporary bytecode translator for compatibility testing.

## Goals

- Keep VM1 available while `nextvm` reaches feature parity.
- Use explicit bytecode chunks, verified function frames, and deterministic runtime errors.
- Make values, calls, GC, and debug metadata easier to reason about than the older runtime layout.
- Keep comparison tooling available until `nextvm` can become the default runtime.

## Module Layout

- `vm/include/nextvm/value/`
- `vm/include/nextvm/bytecode/`
- `vm/include/nextvm/runtime/`
- `vm/src/runtime/nextvm_vm.cpp`
- `vm/src/runtime/nextvm_verifier.cpp`

The legacy V1 bytecode translator lives under:

- `vm/include/runtime/bytecode_translator.h`
- `vm/src/runtime/bytecode_translator.cpp`

## Runtime Model

- `Chunk` owns constants, functions, bytecode, and line-table data.
- `FunctionProto` records arity, locals, stack limits, and code ranges.
- `Frame` stores function index, instruction pointer, and base stack slot.
- `RuntimeError` is returned through APIs instead of terminating the process.

## Migration Plan

1. Keep compiler output compatible with the older VM during migration.
2. Emit direct `.nb` nextvm chunks from frontend MIR for the supported opcode subset. Done.
3. Use the V1 bytecode translator only as a compatibility fallback. In progress.
4. Expand `nextvm` opcode and intrinsic coverage.
5. Move debug metadata and stack inspection onto explicit frame offsets.
6. Switch CLI defaults only after the golden/debug gates are stable.

## Direct `.nb` Format

- Magic: `LPCNVM1\0`
- Module name
- Integer constants
- Floating constants
- String constants
- Class field counts
- Function table
- Line table
- Bytecode payload

The compiler writes `.nb` beside the existing `.b` output when all MIR operations
in the module are supported by the current nextvm backend.

## Current Gate

- `cmake --build build --config Debug`
- `build\vm\lpc_vm.exe --self-check`
- `build\compiler\lpc_compiler.exe --test`
- `build\compiler\lpc_compiler.exe compiler\tests\golden\sample_nextvm_compare_logic_call.lpc --workspace-root . --out-root bin`
- `build\vm\lpc_vm.exe run compiler/tests/golden/sample_nextvm_compare_logic_call --vm next`
