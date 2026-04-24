# LPC Usage (Mainstream CLI Style)

## Command Layout

```text
lpc run <entry>
lpc debug <entry>
lpc profile <entry>
lpc_compiler [--out-root DIR] [--workspace-root DIR] [--entry-module MOD] <source...>
```

Current status:

- `run`, `debug`, and `profile` commands are scaffolded in CLI.
- Existing VM default startup path is still available when no command is passed.
- `profile` now writes `profile.json` with opcode histogram JSON.

## Debug Workflow (Target)

```text
lpc debug game/main
> b game/main.lpc:42
> n
> s
> bt
> p hp
> c
```

## Profile Workflow (Target)

```text
lpc profile game/main --out profile.json
```

Profile output should include:

- opcode histogram
- function call counts
- gc cycles and pause metrics

Current `profile.json` includes:

- `opcodes`: per-opcode execution counts
- `functions`: per-function call counts
