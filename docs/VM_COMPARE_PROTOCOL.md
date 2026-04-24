# VM Compare Protocol (Current)

## Purpose

Define a practical compare flow between legacy VM and VM2 while legacy runtime still has process-exit failure paths.

## Protocol v0

1. Run VM2 first in-process.
2. If VM2 fails, report compare failure immediately.
3. Run legacy VM best-effort (current implementation uses in-process non-fatal mode).
4. Report legacy subprocess exit status.

## Output markers

- `[compare] protocol: vm2-first, legacy-inprocess-best-effort`
- `[compare] vm2_status=ok|error`
- `[compare] legacy_status=ok|error`
- `[compare] done`

## Known limitations

- Legacy VM still contains many direct panic/exit paths in interpreter/efun; compare only hardens bootstrap/load/main-dispatch level.
- Compare currently does not assert semantic equivalence of return value, only path-level completion/error signals.
- For unsupported bytecode/modules in legacy, compare may report legacy error even if VM2 succeeds.

## Next upgrade (v1)

1. Refactor legacy run path to status-return model (no direct `exit` in hot path).
2. Capture both VMs' observable result object and normalized error object.
3. Add strict compare policy (`result + error type + location`).

## Memory limit policy

- VM hard cap: `2GB`.
- Default startup limit: `2GB`.
- Optional env override: `LPC_VM_MAX_MEM` (bytes), but effective value is clamped to `<= 2GB`.
