# LPC Architecture (Mainstream Script Language Style)

## Layered Architecture

The system is split into five layers:

1. Frontend
2. IR and bytecode pipeline
3. Runtime VM
4. Memory and GC
5. Tooling and platform

## 1) Frontend

Input source -> tokens -> AST -> semantic-checked AST.

- Scanner: UTF-8 aware tokenizer.
- Parser: grammar-driven AST builder.
- Semantic checker:
  - Scope resolution
  - Type consistency checks
  - Closure capture analysis
  - Symbol table generation

Output is a typed AST + symbol metadata.

## 2) IR and Bytecode Pipeline

Typed AST -> IR -> optimized IR -> bytecode.

- IR enables optimizations without tying to parser tree shape.
- Bytecode validator runs before VM execution.
- Bytecode format includes:
  - version
  - constant pool
  - function table
  - class metadata
  - debug line map

## 3) Runtime VM

Core components:

- `VM`: process-level runtime context.
- `CallFrame`: function activation record.
- `ValueStack`: operand + locals backing storage.
- `Dispatcher`: opcode execution loop.
- `ObjectSpace`: loaded modules and object cache.

Execution model:

1. Load module bytecode.
2. Validate bytecode and link constants.
3. Create frame and execute dispatch loop.
4. Handle calls/returns with strict ABI.

## 4) Memory and GC

Object heap managed by GC.

- Object header:
  - type tag
  - mark color
  - size/class info
  - list links
- GC algorithm:
  - incremental tri-color mark-and-sweep
  - write barrier for inter-generation references
  - optional nursery generation

Root set:

- VM stack values
- call frames
- loaded object/module table
- interned strings
- active closures/upvalues

## 5) Tooling and Platform

### CLI

- `lpc compile`
- `lpc run`
- `lpc disasm`
- `lpc debug`
- `lpc profile`

### Debugger

- Source breakpoints
- Step/next/finish/continue
- Stack traces and frame local inspection

### Profiler

- Function-level CPU time
- Opcode histogram
- GC pause and allocation metrics

### Platform Abstraction

`platform/` should provide:

- filesystem
- path
- clock/time
- process/env
- random/entropy

All runtime and stdlib platform interactions go through this layer.
