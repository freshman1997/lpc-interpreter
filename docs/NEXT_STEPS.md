# Next Steps

## Step 1: VS Code Developer Loop

- [x] Register `.lpc` language, comments, brackets, folding, snippets, and syntax highlighting.
- [x] Add compile, run, and debug commands.
- [x] Surface compiler diagnostics in VS Code Problems.
- [x] translator VS Code breakpoints into `lpc_vm debug`.
- [x] Add task provider and launch/tasks examples.
- [x] Parse basic locals and args into the Variables panel.
- [ ] Add hover/completion from known types, globals, functions, and efuns.
- [ ] Add document symbols for functions, classes, and globals.

## Step 2: Debugger Protocol

- [x] Add a VM debug protocol mode that emits JSON lines.
- [ ] Emit structured continued, output, and exception events.
- [x] Emit structured stopped and terminated events.
- [x] Emit structured stack frames with object, function, line, and pc.
- [x] Emit structured locals and args variables.
- [x] Move VS Code debug adapter stopped/variables parsing to protocol parsing.
- [x] Move breakpoint acknowledgements and evaluate results to protocol parsing.
- [x] Emit structured runtime errors.
- [ ] Add request IDs for command/response correlation.
- [ ] Add structured continued and output events.

## Step 3: Compiler Hardening

- [ ] Add a non-writing compiler check mode for editor diagnostics.
- [ ] Preserve original source spans through preprocessing includes.
- [ ] Tighten block scope, shadowing, invalid lvalue, and return coverage diagnostics.
- [ ] Add MIR verifier coverage for stack joins, calls, foreach cleanup, and constants.
- [ ] Keep optimizer tests for before/after MIR stable.

## Step 4: VM Stability

- [ ] Make runtime errors deterministic and source-located.
- [x] Add direct frontend -> nextvm `.nb` bytecode output for the supported opcode subset.
- [x] Make `lpc_vm run --vm next` prefer direct `.nb` chunks before using the V1 translator fallback.
- [x] Add nextvm `Pop` and `Dup` opcodes for assignment-expression stack patterns.
- [x] Expand self-check for bytecode verifier, stack overflow, memory limits, and GC barriers.
- [x] Add frame stack offsets and use offset-based GC stack root scanning.
- [x] Cover minor GC remembered-set survival for closure, array, and mapping containers.
- [x] Initialize array slots at allocation time so GC scans deterministic values.
- [ ] Keep debugger script tests in CI-friendly non-interactive form.
- [ ] Add object-field old-to-young minor GC survival self-check.
- [ ] Move debugger frame locals/args inspection to stack offsets.
- [ ] Add V1 bytecode stack-depth verification.
- [ ] Extend nextvm lowering/runtime for efuns, globals, strings/floats, mappings, closures, foreach, switch, catch, indexed store, and virtual calls.
- [ ] Switch `lpc_vm run` default to `next` after golden/debug gates pass.
- [ ] Remove the older VM runtime after nextvm is default and debugger-compatible.

## Step 5: Performance

- [ ] Add compiler and VM benchmark commands.
- [ ] Track instruction counts and optimizer deltas for representative programs.
- [ ] Add profiler schema docs and VS Code command to run profiling.
