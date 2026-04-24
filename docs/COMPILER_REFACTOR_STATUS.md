# Compiler Refactor Status

## Completed in this wave

1. Legacy compiler pipeline was removed.
2. Frontend2 is default compiler path.
3. Parser supports function/lambda, var, return, call, if/else, while, for.
4. Sema covers symbol resolution and base capture tracking.
5. MIR generation supports arithmetic, assignment, control flow, calls, locals/upvalues.
6. MIR to V1 bytecode lowering bridge was added.
7. Compiler now emits bytecode files into `build/compiler` directly.
8. VM can load generated `1.b` entry alias and execute.
9. Output layout now follows source-relative module path under workspace `bin/`.
10. Added multi-module golden coverage and path-mirroring validation.
11. Added initial capture source classification fields in semantic model.
12. Capture source metadata is now propagated into MIR and bytecode emission path.
13. VM closure creation now consumes capture source metadata for upvalue binding.

## Remaining to complete

1. Full LPC grammar parity (class/object runtime behavior details).
2. Rich type system checks and stronger diagnostics.
3. Harden nested-closure capture source correctness across deep chain cases.
4. Tighten verifier with stronger function termination and inter-procedural call contract checks.
5. Replace V1 bridge with V2 opcode backend.

## Grammar parity progress

- `foreach` syntax and lowering path are in place.
- `class` declaration and `new` expression baseline are in place.
- Unknown class in `new` now fails at compile time.

## Practical state

- End-to-end path now exists:
  - source -> frontend2 -> MIR -> V1 bytecode -> VM load/run
- This is a functional baseline, not final completeness.

## Output path rule

- Bytecode output root: `<workspace>/bin`
- Relative path mirrors source location under workspace.
- Output filename uses source stem: `<source_stem>.b`
- Module name inside bytecode matches source-relative module path.
- Example:
  - source: `build/sample_front2.lpc`
  - output file: `bin/build/sample_front2.b`
  - module name: `build/sample_front2`

## Entry execution

- Entry resolution precedence:
  1. explicit CLI module argument
  2. `LPC_ENTRY` environment variable
  3. `--entry-file` content (default `bin/entry.txt`)
  4. fallback module `1`
- Default runtime entry resolves from `bin/entry.txt` when present.
- If `bin/entry.txt` is absent, runtime falls back to module `1`.
- Environment variable override is supported:
  - `LPC_ENTRY=<module>`
- You can run explicit module entry with CLI:
  - `lpc_vm run build/sample_front2`

## Compiler CLI options

- `--out-root <dir>`: set bytecode output root (default: `./bin`)
- `--workspace-root <dir>`: set workspace root used for module-relative path
- `--entry-module <module>`: override content written to `entry.txt`
