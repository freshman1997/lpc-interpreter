# LPC Compiler, VM, and VS Code Productionization Plan

This plan tracks the work needed to move the LPC implementation from experimental to project-usable.

## Phase 1: Developer Loop

- Add a VS Code extension for `.lpc` editing, compiler diagnostics, run commands, and VM debugger launch.
- Keep compiler diagnostics stable: `file:level(line:column): message`.
- Keep `lpc_compiler --test` as the fast frontend regression gate.
- Keep `lpc_vm --self-check` as the fast runtime regression gate.

## Phase 2: Compiler Correctness

- Tighten semantic diagnostics for block scope, shadowing, invalid lvalues, return coverage, and type mismatches.
- Extend MIR verification for call arity, branch stack joins, foreach cleanup, class field access, and constant pool bounds.
- Preserve source locations through preprocessing, parsing, MIR, bytecode writing, and VM frames.
- Expand golden tests to cover invalid programs, optimizer output, and bytecode execution.

## Phase 3: Runtime Stability

- Finish the NextVM compatibility matrix and document the remaining legacy-only opcodes.
- Add deterministic error reporting for stack overflow, bad bytecode, bad object paths, memory limits, and uncaught runtime errors.
- Keep GC barriers and remembered-set behavior covered by self-check and stress tests.
- Add non-interactive debugger script tests for breakpoints, watchpoints, frame selection, and source listing.

## Phase 4: Performance

- Track compiler instruction counts and optimizer deltas in tests for known samples.
- Add VM microbenchmarks for calls, loops, arrays, mappings, closures, and object/class access.
- Make profiler output part of the run CLI and document the JSON schema.
- Avoid broad rewrites until correctness gates are stable.

## Phase 5: Debug Protocol

- Add a machine-readable VM debug protocol alongside the current text translator.
- Emit structured events for stopped, exception, output, continued, and terminated.
- Emit structured stack frames, scopes, variables, and source locations.
- Keep the VS Code extension API stable while swapping the backend transport.
