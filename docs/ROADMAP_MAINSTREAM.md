# LPC Mainstream Alignment Roadmap

## Goals

This roadmap aligns the LPC implementation with mainstream scripting languages
in the style of Lua, Python, JavaScript (Node), and Ruby.

The target is:

1. Predictable language semantics.
2. Stable runtime with production-grade memory safety.
3. Fast VM execution with measurable performance.
4. Cross-platform behavior consistency.
5. First-class developer tooling (debugger, profiler, diagnostics).

## Alignment Principles

### 1) Language and Semantics (like Python/Lua)

- Single, stable grammar and semantic rules.
- Clear type conversion rules and operator behavior.
- Deterministic closure and lexical scope semantics.
- Standardized module/object loading behavior.

### 2) Runtime and VM (like Lua/CPython VM)

- Bytecode is versioned and validated before execution.
- Fast dispatch loop and compact call frame layout.
- Explicit stack discipline and well-defined call ABI.
- Structured runtime errors (no raw `exit` in hot paths).

### 3) Memory Management (like Lua/PyPy family)

- Incremental tri-color GC baseline.
- Write barrier for old->young references.
- Optional generation split for short-lived objects.
- GC telemetry (pause time, reclaimed bytes, cycle count).

### 4) Standard Library (like Python stdlib / Node core)

- Stable APIs for io, string, array, dict, time, os/platform.
- Cross-platform contract: same behavior on Windows/Linux/macOS.
- Error model based on return status + message object.

### 5) Tooling (like `python -m pdb`, `node --inspect`, `cProfile`)

- CLI with compile/run/disasm/debug/profile subcommands.
- Source-level debugger with breakpoints and stack inspection.
- Built-in profiler outputting JSON and folded stack format.

## Priority Phases

## Phase 0 - Correctness and Safety First

- Fix memory safety and GC double-free risks.
- Remove hardcoded platform paths.
- Replace unconditional `exit/abort` with typed error flow.
- Add smoke tests for parser/compiler/vm boot path.

## Phase 1 - C++ Runtime Foundation

- Introduce RAII ownership model in runtime and allocator paths.
- Isolate platform APIs under `platform/` abstraction.
- Separate compiler IR and VM bytecode validation.

## Phase 2 - Closure and Object Model Completion

- Implement upvalue capture/load/store opcodes end-to-end.
- Add lexical scope checks in semantic analysis.
- Verify closure + GC reachability invariants.

## Phase 3 - VM and GC Performance

- Add dispatch fast path and opcode microbench.
- Add incremental GC scheduler and barriers.
- Add profile counters per opcode/function.

## Phase 4 - Toolchain and Docs

- Deliver debugger CLI and profiler CLI.
- Publish EBNF, bytecode spec, and platform API docs.
- Add architecture and contributor documents.

## Definition of Done (Mainstream Baseline)

- All core language features have parser + semantic + codegen + VM coverage.
- VM passes deterministic cross-platform test set.
- GC stress tests pass with no leaks or double frees.
- Debugger and profiler work on representative scripts.
- Public docs match implementation behavior.
