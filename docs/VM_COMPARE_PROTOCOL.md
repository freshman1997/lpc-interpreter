# VM Compare Protocol

## Purpose

Define the temporary compare flow while `nextvm` is becoming the primary runtime.
New compiler output now includes direct `.nb` nextvm chunks when the MIR uses
supported operations.

## Protocol v0

1. Compile LPC through the frontend.
2. Prefer direct `nextvm` bytecode (`.nb`) for `--vm next`.
3. Fall back to the V1 bytecode translator only when `.nb` is unavailable.
4. For compare mode, run `nextvm` first, then run the older runtime best-effort.
5. Report both status streams.

## Output markers

- `[compare] protocol: NextVM-first, legacy-inprocess-best-effort`
- `[compare] NextVM_status=ok|error`
- `[compare] legacy_status=ok|error`
- `[compare] done`

## Known limitations

- Direct `.nb` lowering currently covers integer constants, locals, direct calls, branches, arithmetic/comparison/logical ops, arrays, simple classes, `dup`, `pop`, and return.
- Missing nextvm coverage still includes efuns, globals, strings/floats, mappings, closure/upvalue, foreach, switch, catch, indexed store, virtual calls, and full debugger integration.
- Compare currently still uses the older runtime as a second pass until nextvm coverage is broad enough to become the default.

## Next upgrade (v1)

1. Add frontend diagnostics for MIR operations that cannot lower to `.nb`.
2. Extend nextvm opcode coverage for efuns, globals, mappings, closures, and indexed stores.
3. Move VS Code debug protocol onto nextvm frames and line tables.
4. Switch `lpc_vm run` default from the older runtime to `nextvm`.
5. Delete the old runtime after golden, debugger, and compare gates pass.

## Memory limit policy

- VM hard cap: `2GB`.
- Default startup limit: `2GB`.
- Optional env override: `LPC_VM_MAX_MEM` (bytes), but effective value is clamped to `<= 2GB`.
