# Refactor Wave 2 (Started)

This wave focuses on mainstream language capabilities and runtime observability.

## Completed in this wave

1. Closure execution opcodes are no longer hard-fail placeholders.
   - `op_set_upvalue` and `op_get_upvalue` now operate on closure objects.
2. Parser lambda body with `-> expr` now normalizes to implicit return.
3. Codegen now supports function-expression path in expression generation.
4. Function prototype serialization now persists `nupvalue` field.
5. VM loader now reads `nupvalue` from bytecode function metadata.
6. Interpreter now emits opcode counts when profiler mode is enabled.
7. Function expression/lambda path now emits function object load and closure bind sequence.
8. Closure objects are created for functions with non-zero upvalues.
9. GC mark phase now traverses closure owner and captured upvalues.
10. CLI `profile` command now outputs `profile.json` (opcode histogram JSON).
11. Lambda capture is refined from broad parent-scope copy to used-identifier-based capture.
12. Upvalue read/write codegen path now emits `op_get_upvalue` and `op_set_upvalue` for captured names.
13. Nested lambda capture propagation is now considered during parent capture analysis.
14. Assignment opcode mapping bug was corrected (`=` no longer maps to float-const load opcode).
15. Compound assignment opcode mappings were corrected (`+=`, `-=`, `*=`, `/=`, `%=` now map to arithmetic opcodes).
16. Frame teardown now clears stale `base_ci` after last frame pop, reducing dangling call-chain risk.
17. Profiler now records function call counts in addition to opcode counts.
18. Profiler JSON output is now sorted by descending hotness and escapes function names.
19. Closure regression test plan drafted for nested/escape/compound-assignment scenarios.

## Notes

- Current closure support is execution plumbing + metadata pipeline.
- Lexical capture analysis is now minimally wired; strict escape/capture correctness still needs hardening.
- Profiler is currently opcode-count level; timing and call graph are next.

## Next tasks

1. Semantic closure capture table generation.
2. Upvalue bind op emission at lambda creation sites.
3. GC root expansion for closure-retained values under stress tests.
4. CLI `profile` output format and dump command.
