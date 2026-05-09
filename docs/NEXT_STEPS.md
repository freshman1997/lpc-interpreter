# Next Steps

## Step 0: Security and Baseline Gates

- [x] Remove plain token-like strings from docs/config examples.
- [x] Add repository secret scanning config (`.gitleaks.toml`).
- [x] Add CI workflow skeleton with secret scan + build + basic checks.
- [x] Add local `scripts/security_scan.ps1` helper for contributor pre-check.
- [x] Remove hardcoded temp path fallbacks from runtime self-check.
- [x] Replace process-level `exit` usage in server/runtime hot paths with controlled return flow.
- [x] Make `run_lpc_tests.ps1` binary resolution cross-platform (`.exe` only on Windows).
- [x] Run LPC regression suite in CI for both Windows and Linux.
- [x] Move compiler/lsp/vm CMake away from `GLOB_RECURSE` to explicit source lists.
- [x] Tighten non-MSVC compiler warnings by removing `-fpermissive`.
- [x] Introduce shared process exit code model (`include/lpc/runtime/exit_code.h`).
- [x] Apply exit code mapping in `lpc_vm` entry and CLI run/debug/hot-reload/lsp paths.
- [x] Map standalone `lpc_lsp` protocol shutdown behavior to standardized exit code.
- [x] Extract VM runtime error -> process exit code mapping into shared header (`vm/include/vm/runtime/exit_code_map.h`).
- [x] Add shared process context header for cwd access (`vm/include/vm/runtime/process_context.h`).
- [x] Add CI failure artifact upload for secret scan and test logs.
- [x] Add baseline timer efuns: `call_later(ms, fn, ...)` and `cancel_timer(timer_id)`.
- [x] Add VM self-check coverage for timer schedule/cancel flow.
- [x] Add LPC regression case `test_timer.lpc` and include it in `run_lpc_tests.ps1`.
- [x] Add regex efuns: `regexp(text, pattern)` and `regex_replace(text, pattern, replacement)`.
- [x] Add LPC regression case `test_regex.lpc` and include it in `run_lpc_tests.ps1`.
- [x] Add timer observability efuns: `timer_exists(timer_id)` and `pending_timers()`.
- [x] Add timer queue hard limit in `call_later` to avoid unbounded pending growth.
- [x] Add `timer_info(timer_id)` for structured timer introspection.
- [x] Add regex flags support (`i`) for `regexp` and `regex_replace`.
- [x] Extend regex flags support with `n`/`o` parsing (`nosubs`/`optimize`).
- [x] Add timer stress regression case `test_timer_stress.lpc`.
- [x] Extend timer regression output to include `timer_info` detail fields.
- [x] Add per-module timer queue cap alongside global cap in `call_later`.
- [x] Extend `timer_info` with callback name metadata (`callback_name`).
- [x] Add `timer_clear_module([module])` to bulk cancel module timers.
- [x] Add regex `m/s` behavior support (`multiline`, dotall expansion).
- [x] Add timer quota regression case `test_timer_quota.lpc`.
- [x] Add timer runtime stats efun `timer_stats()`.
- [x] Add regex invalid-flag guard behavior.
- [x] Optimize timer scheduler hot-path with cached next due timestamp.
- [x] Track O(1) timer pending/peak counters for cheap stats/quota checks.
- [x] Add O(1) per-module timer pending counters for module quota checks.
- [x] Move timer due scheduling to heap-assisted dispatch path.
- [x] Add regex compiled-pattern LRU cache path.
- [x] Add perf benchmark `regex_intrinsic_cached_10k`.
- [x] Add timer-heap stale-pop/rebuild stats for long-run observability.
- [x] Add regex cache stats efun `regex_stats()` for hit/miss/evict observability.
- [x] Upgrade perf runner to median-of-5 timing to reduce noise.
- [x] Add regex warm/cold split benchmarks (`regex_intrinsic_cached_10k`, `regex_intrinsic_cold_1k`).

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
