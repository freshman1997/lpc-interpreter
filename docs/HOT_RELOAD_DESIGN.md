# LPC VM Hot Reload Design

This document defines a production-oriented hot reload design for the LPC compiler + VM stack.

It is intentionally phased. We do not enable full structural live migration on day one.

## 1. Goals

- Reload selected modules without restarting the VM process.
- Keep running sessions stable (no stack corruption, no invalid handles).
- Allow safe rollout with strict compatibility checks.
- Support gradual capability expansion from function-body patching to structural changes.
- Keep debugger (DAP/REPL) and source map behavior correct across versions.

## 2. Non-goals (initial rollout)

- No arbitrary in-place rewrite of active call frames.
- No implicit migration of class/global layout changes without explicit migration rules.
- No cross-process distributed hot reload protocol in phase 1. (Phase 3 now implemented.)
- No persistent object storage in phase 1. (Phase 5 now implemented.)

## 3. Current Implementation State

### What is implemented (Phase A complete)

- **ModuleRegistry** (`vm/include/vm/runtime/hot_reload.h`): Version chain with `Loaded → Active → Deprecated → Retired` state machine. Supports install candidate, activate, pin/unpin (ref counting), rollback, and retirement with bounded window (≤ 2 deprecated versions retained).
- **HotReloadCompatChecker**: L0, L1, and L2 compatibility checking. L0: function name/count/arity, class layout, global names/order must match exactly. L1: allows additive changes (new functions/classes/globals appended) while existing items must remain at same indices with same signatures. L2: allows structural changes (renames, drops, additions) when covered by an explicit MigrationDescriptor.
- **HotReloadManager**: Orchestrates prepare → compat check → activate → rollback flow. Tracks prepared (pending) versions separately from installed versions.
- **Versioned execution**: `Frame::module_version_id`, `LpcObject::module_version_id`, `LpcClosure::module_version_id_` — old frames continue on old code, new calls use new code. Dispatch loop switches `chunk_/iconst_values_/fconst_values_/sconst_values_` via `BindExecutionVersion()` when frame version differs from bound version.
- **VersionRuntimeData**: Per-version chunk + const caches stored per-module in `ModuleRuntimeState::version_runtime_data`. Avoids cache invalidation bugs during dual-version periods.
- **ModuleRuntimeState**: Per-module state in `Vm::module_states_` map. Each module tracks its own `active_version_id`, `version_runtime_data`, and `prepared_runtime_data`. Replaces flat `version_runtime_data_`/`prepared_runtime_data_` maps to enable independent version management across modules.
- **LoadModule API**: `Vm::LoadModule(module_name, chunk)` loads an additional module alongside the main chunk. Rejects if module already loaded (use hot-reload API for updates).
- **Cross-module call_other**: `call_other` resolves to the target object's module active version, not the caller's version. Frame gets `module_name` and `module_version_id` from the target module, enabling correct version pinning and chunk binding.
- **Cross-module GetChunkForVersion**: Searches all module states to find a version by ID, enabling cross-module object field resolution.
- **Explicit trigger enforcement**: `Vm::LoadChunk` rejects if module already loaded (first-load-only). Subsequent reloads must use `PrepareHotReload/ActivateHotReload` API.
- **Frame pin/unpin**: Frame push does `PinVersion` on the executing module version; frame pop does `UnpinVersion`. When ref_count hits 0, `RetireDeprecatedVersions` is triggered to reclaim memory.
- **Object field resolution**: `Vm::GetObjectField` uses `module_version_id` to resolve the correct chunk version for field layout.
- **Smoke test gate**: `PrepareHotReloadModule` (entry API) runs the candidate in an isolated VM before accepting on the live VM.
- **CLI** (`lpc hot-reload <module>`): `--check-only` (compat check on probe VM), `--dry-run` (compat check only), `--require-smoke func` (compat + smoke + activate), `--status` (show version chain), `--allow-level L0|L1|L2`. Default (no flag): full apply (prepare+activate).
- **Entry API** (`vm/include/vm/runtime/entry.h`): `LoadModuleChunkForHotReload`, `CheckHotReloadModule`, `PrepareHotReloadModule`, `ActivatePreparedHotReloadModule`, `GetHotReloadModuleStatus`, `ApplyHotReloadModule`.
- **Debugger/DAP**: `DebugFrame` includes `module_name` and `module_version_id`. DAP `stackTrace` response includes `moduleName` and `moduleVersion` per frame. `stopped` event includes `moduleName` and `moduleVersion` (top frame). Custom `moduleVersionChange` event emitted when version changes between breaks.
- **Versioned backtrace**: `GetBacktrace/PrintBacktrace` accept a chunk resolver lambda to resolve source maps per version.
- **Self-check tests** (76 total):
  - L0 compat reject (arity change)
  - L0 activate success (verify result changes)
  - Debug+hot-reload interleave (hook triggers reload during step)
  - Explicit trigger enforcement (second LoadChunk must fail)
  - Retention stress (30 rapid reloads during active frame)
  - API prepare/activate flow
  - Smoke gate (nonexistent entry function must fail)
  - Smoke pass + activate (valid smoke function passes and module activates)
  - GC stress (50 rapid reloads with GC cycles, version leak detection)
  - L1 add-function accept (new function appended and callable)
  - L1 reject removal (removed function rejected)
  - L1 reject arity change (existing function arity change rejected)
  - L1 add-globals accept (new global appended)
  - L1 add-class accept (new class appended)
  - L1 add-class-field accept (existing class gets new field appended)
  - L1 reject global reorder (swapped global names rejected)
  - L1 reject class layout change (existing class field count change rejected)
  - L2 rename/drop/add global with migration
  - L2 reject uncovered removal
  - L2 reject new field without default
  - L2 object auto-upgrade with migration
  - L2 custom transform callback
  - L2 class field migration (rename, add-with-default)
  - L2 reject class field change without ClassMigration
  - String concat (numeric+string, two strings, float+string)
  - Class instance upgrade via index
  - Rollback restores object state
  - Type error on bad operand
  - Multiple objects with globals
  - Version lookup across modules
  - Version lookup after hot-reload
  - Rollback + GetChunkForVersion
  - Cross-module version lookup after reload
  - Multiple hot-reload cycles + GetChunkForVersion
  - Retired version lookup
  - Rollback preserves version lookup
  - **Phase 2 (LSP)**: JSON-RPC serialize round-trip, JSON-RPC array round-trip, JSON-RPC null/bool round-trip, LSP server initialize, LSP symbol index, LSP completion+hover, LSP documentSymbol+references
  - **Phase 3 (Distributed)**: Reload protocol round-trip, reload protocol all-message-types, coordinator basic
  - **Phase 5 (Persistent)**: Persistence codec value round-trip, nested value round-trip, object round-trip, module-state round-trip, chunk serialization round-trip, chunk deserialize+execute, persistent storage save/load module, save/load all modules
- **Cross-module support**:
  - LoadModule for two independent modules (version tracking, duplicate rejection, GetChunkForVersion)
  - Cross-module hot-reload independent (reload module B, verify module A unaffected, both versions accessible)
  - Cross-module GetChunkForVersion (resolve chunks across modules, bogus version returns null)
- **Audit log** (`vm/include/vm/runtime/audit_log.h`):
  - `AuditEntry`: timestamp, module_name, action (Prepare/Activate/Rollback/Retire), version_id, previous_active_version, chunk_hash, level, compat_result, actor
  - `AuditLog`: in-memory ring buffer (default 1024 entries, trims 25% on overflow), optional file append (JSON-lines format)
  - Integrated into `HotReloadManager`: prepare/activate/rollback operations automatically record audit entries
  - Exposed via `Vm::audit_log()` accessor
  - CLI `--audit-log <path>` enables persistent file output (JSON-lines)
  - Entry API: `SetHotReloadAuditLogPath(path)` configures file persistence on live VM
- **Object auto-upgrade**: Objects automatically upgrade to their module's active version on access. `TryUpgradeObject(obj_id)` is called lazily at key touchpoints (call_other, GetObjectField, GetObjectFieldNames). For L0, only `module_version_id` and `blueprint` are updated (layout unchanged). For L1, the `globals` vector is extended with Nil entries for new globals while existing values are preserved. For L2, `MigrateObjectGlobals()` applies the stored `MigrationDescriptor` to remap fields (rename, drop, add-with-default, custom transform).
- **L2 migration engine** (`hot_reload.h/cpp`): `MigrationDescriptor` with `GlobalMigration` (rename/drop/add-with-default entries), `ClassMigration` (per-class field mappings), and optional `custom_transform` callback. `HotReloadCompatChecker::CheckL2()` validates that the descriptor covers all incompatibilities (removed globals/class fields need Drop or Rename, new globals/class fields need AddWithDefault or Rename). `MigrateObjectGlobals()` builds new globals vector from old by name-based mapping. `MigrateClassFields()` applies class field migration similarly. `UpgradeClassInstancesForModule()` migrates class instances on hot-reload activation. Stored per-version in `HotReloadManager::migration_descriptors_`, applied in `TryUpgradeObject` on L2 upgrade (globals) and `UpgradeClassInstancesForModule` on activation (class fields).
- **VSCode extension** (modular, `vscode_plugin/src/`):
  - `efunDb.js`: EFun database + keyword/type constants for IntelliSense
  - `symbolParser.js`: Regex-based document symbol parsing (lightweight + full vscode.DocumentSymbol with class children)
  - `workspaceIndex.js`: `WorkspaceSymbolIndex` class — incremental index across all open/workspace `.lpc` files; supports lookup, prefix search, cross-file queries
  - `completion.js`: Completion provider — efuns + keywords + types + document symbols + cross-file symbols (sorted after local)
  - `hover.js`: Hover provider — efuns + types + keywords + document symbols + cross-file hover (shows source filename)
  - `signatureHelp.js`: Signature help provider — efuns + document functions + cross-file function signatures
  - `documentSymbols.js`: Document symbol provider using `parseDocumentSymbolsFull`
  - `workspaceSymbols.js`: Workspace symbol provider (Ctrl+T) backed by `WorkspaceSymbolIndex`
  - `diagnostics.js`: Compiler output parsing and diagnostic publishing
  - `debugAdapter.js`: Full DAP debug adapter with versioned backtrace display
  - `taskProvider.js`: Task provider for compile/run tasks
  - `utils.js`: Shared utilities (activeLpcFile, configFor, compileFile, runVm, etc.)
  - `extension.js`: Thin entry point — creates `WorkspaceSymbolIndex`, wires document events, registers all providers
  - `lspClient.js`: LSP client — connects to standalone `lpc_lsp` binary via `vscode-languageclient`, supports restart command

### What is not yet implemented

- LSP: incremental sync (currently full sync on change), signature help, code actions
- Distributed hot reload: production hardening and fault tolerance testing
- Persistent storage: auto-save timer and crash recovery

## 4. Design Principles

- Versioned execution: old frames continue on old code; new calls use new code.
- Compatibility first: reject unsafe updates early.
- Explicit migration: structural changes require migration plan, never best-effort guessing.
- Fast rollback: failed activation restores last stable version atomically.
- Observability: every reload has structured status, reason, and version metadata.

## 5. Hot Reload Levels

### L0: Function Body Reload (implemented)

Allowed:
- Function bytecode body changes.
- Constant pool changes that are internal to function body semantics.
- Debug/source map updates.

Forbidden:
- Function signature change (name/arity).
- Class/global layout change.
- Inheritance graph change.

Guarantee:
- Existing frames complete on previous version.
- New calls enter new version immediately after activation.

### L1: API-compatible Additive Reload (implemented)

Allowed:
- All L0 changes.
- Adding new functions at the end of the function table (new indices).
- Adding new classes at the end of the class table (new indices).
- Adding new globals at the end of the global table (new indices).

Still forbidden:
- Removing or reordering existing functions, classes, or globals.
- Changing existing function names or arity.
- Changing existing class layouts (field names, count, order).
- Changing existing global names or order.

How it works:
- The compat checker uses prefix matching: all items in the old chunk must appear at the same indices in the new chunk with identical signatures. New items may be appended after the old prefix.
- New globals are initialized by the new version's `init_code` when `RunEntry` is called. Existing globals retain their values at their original indices.
- `LoadGlobal` returns `Nil()` for out-of-bounds indices; `StoreGlobal` auto-resizes. Old-version code only accesses old indices; new-version code accesses both old and new indices. No migration needed.
- The `HotReloadCompatReport` includes `added_functions`, `added_classes`, `added_globals` counts and name lists for observability.

### L2: Structural Reload + Migration (implemented)

Allowed with `MigrationDescriptor`:
- Global field rename, drop, add-with-default.
- Class field add/remove/rename/reorder.
- Custom transform callback for computed migrations.

Migration descriptor (`MigrationDescriptor`):
- `globals`: `GlobalMigration` with vector of `FieldMigration` entries.
  - `Keep`: field at same index, unchanged.
  - `Rename`: `old_name` → `new_name`, value preserved.
  - `Drop`: `old_name` removed, value discarded.
  - `AddWithDefault`: `new_name` added with `default_value`.
- `classes`: vector of `ClassMigration` (per-class field mappings).
- `custom_transform`: optional `std::function<void(std::vector<Value>&)>` for arbitrary post-migration transforms.

Validation (`CheckL2`):
- Every removed global must be covered by a `Drop` or `Rename` entry.
- Every new global must be a `Rename` target or have `AddWithDefault`.
- Otherwise rejected as `Incompatible`.

Migration execution (`MigrateObjectGlobals`):
- Builds new globals vector by name-based lookup from old globals.
- Applies rename mappings first, then AddWithDefault for new fields.
- Drops are implicit (old values not carried forward).
- Custom transform runs last on the fully constructed new globals vector.

Integration:
- `PrepareHotReload(module, chunk, L2, ..., &migration)` stores the descriptor keyed by version ID.
- `TryUpgradeObject()` looks up the descriptor for the target version and calls `MigrateObjectGlobals()`.
- Old global names are resolved from the version runtime data (not from `obj.blueprint`, which may have been rebound).

## 6. Runtime Architecture

### 6.1 Module Registry

`ModuleRegistry` in `vm/include/vm/runtime/hot_reload.h`:

- Key: `module_name`
- Value: version chain stored in `ModuleChain`
  - `version_id` (monotonic, starting from 1)
  - `shared_ptr<const Chunk>` (immutable)
  - `state`: `Loaded → Active → Deprecated → Retired`
  - `ref_count`: tracks active frame references
  - `chunk_hash`: for diagnostics/audit

Registry responsibilities:
- Install candidate version (compat check is done by `HotReloadManager` before calling)
- Activate version atomically (mark old active as Deprecated)
- Pin/unpin versions (ref counting for frame safety)
- Retire deprecated versions when ref_count == 0 (bounded window ≤ 2 deprecated)
- Rollback to specific version
- Provide status query

### 6.2 Versioned References

Runtime entities with module-version awareness:

- **Frame** (`vm/include/vm/runtime/frame.h`):
  - `module_version_id`: pinned at frame creation
  - `func_id/ip/base` interpreted against pinned version's chunk
- **LpcObject** (`vm/include/vm/runtime/vm.h`):
  - `module_version_id` + `module_name`: identifies which version's class layout to use
  - Policy: auto-upgrade on access (see §6.5)
- **LpcClosure** (`vm/include/vm/value/lpc_closure.h`):
  - `module_version_id_`: stored alongside function identity

### 6.3 Constant Cache Ownership

Constant caches are per-version:

- `VersionRuntimeData` holds `chunk` + `iconst_values` + `fconst_values` + `sconst_values`
- Stored per-module in `ModuleRuntimeState::version_runtime_data` (active/installed versions) and `ModuleRuntimeState::prepared_runtime_data` (pending activation).
- Dispatch loop reads cache via `BindExecutionVersion()` when frame version ≠ bound version
- Old caches stay valid while old frames exist (version is pinned)

### 6.4 Call Resolution

- Direct calls (`CallDirect`, `CallVirtual`) inside a frame resolve within frame's pinned module version (dispatch loop switches chunk/constants via `BindExecutionVersion`).
- `call_other` resolves to the **target object's module active version** — not the caller's version, not the object's pinned version. This matches LPC semantics where `call_other(obj, "func")` dispatches to the object's current code.
- Cross-module frame inherits `module_name` and `module_version_id` from the target module, ensuring correct `PinVersion/UnpinVersion` and `BindExecutionVersion` behavior.
- `BindExecutionVersion(module_name, version_id)` overload added for cross-module version switching; old single-arg version delegates via `bound_module_name_`.
- `GetChunkForVersion(version_id)` searches all module states to find the chunk, enabling cross-module object field resolution without requiring module name lookup.

### 6.5 Object Auto-Upgrade

When a module is hot-reloaded, existing objects created from the old version may still reference the old `module_version_id` and `blueprint`. Auto-upgrade ensures objects transition to the new version lazily on access.

**`TryUpgradeObject(obj_id)`** (`vm/src/runtime/vm_chunk_lifecycle.cpp`):
- Checks if the object's module has a newer active version than the object's `module_version_id`
- If so, updates `module_version_id` and `blueprint` to the active version
- For L1 (additive) changes: extends the `globals` vector with Nil entries for newly added globals, preserving existing values at their original indices
- Returns `true` if an upgrade occurred, `false` if already at latest or not upgradable
- Safe to call multiple times; idempotent after upgrade

**Upgrade touchpoints** (lazy, on-access):
- `call_other` dispatch: object is upgraded before resolving the target, ensuring the frame uses the object's current module version
- `GetObjectField` / `GetObjectFieldNames`: object is upgraded before field lookup, ensuring new L1 fields are visible
- `TryUpgradeObject` can also be called explicitly via the public API

**Safety guarantees**:
- L0: Layout is identical, so upgrading is always safe — only `module_version_id` and `blueprint` are updated
- L1: Existing globals stay at same indices (prefix match), new globals are appended with Nil. `LoadGlobal`/`StoreGlobal` handle out-of-bounds indices by returning Nil or auto-resizing
- Objects currently executing on an old-version frame are safe: the frame is independently version-pinned, and the object's globals vector preserves old values at old indices
- No upgrade occurs for destroyed objects or objects with empty module names

## 7. Compatibility Model

`HotReloadCompatChecker::Check(old_chunk, new_chunk, level)` returns `HotReloadCompatReport`:

- `kind`: `Compatible`, `CompatibleWithMigration`, or `Incompatible`
- `issues`: list of `{field, detail}` pairs
- `migration_required`: flag

### 7.1 L0 Rules (implemented)

Must match exactly:
- Module name
- Function set (same names)
- Arity for existing functions
- Class count and class field layouts (names, order)
- Globals count and order
- Inheritance metadata

Can differ:
- Function bytecode body
- Const pools
- Line table/debug/source map

### 7.2 L1 Rules (implemented)

Must match exactly (prefix):
- Module name
- Function names and arity at indices [0, old_count)
- Class layouts at indices [0, old_count)
- Global names and order at indices [0, old_count)

May differ:
- New functions appended after old prefix
- New classes appended after old prefix
- New globals appended after old prefix
- Function bytecode body changes (same as L0)
- Const pools, line table, debug info (same as L0)

Compat report additions:
- `added_functions` / `added_function_names`
- `added_classes` / `added_class_names`
- `added_globals` / `added_global_names`

### 7.3 L2 Rules

Compat check with `MigrationDescriptor`:
- All removed globals must be covered by `Drop` or `Rename` entry
- All new globals must be `Rename` target or have `AddWithDefault`
- Uncovered removals or additions without defaults → `Incompatible`

Allowed changes beyond L1:
- Global field rename (value preserved at new index)
- Global field drop (value discarded)
- Global field add with explicit default value
- Custom transform function for computed migrations
- Class field structural changes (via `ClassMigration`)

Compat report:
- `kind = CompatibleWithMigration`
- `migration_required = true`
- Issues list details uncovered fields

## 8. Activation and Rollback Flow

### 8.1 Pipeline

1. Compile target file(s) to candidate bytecode.
2. `CheckHotReloadModule`: load candidate in probe VM, run compatibility checker.
3. If incompatible, reject with detailed report.
4. `PrepareHotReloadModule`: install candidate as `Loaded` on live VM, run smoke test on isolated VM (if specified).
5. `ActivatePreparedHotReloadModule`: atomic flip active version pointer in registry.
6. Prior active version marked `Deprecated`.
7. Deprecated version retired when `ref_count` hits 0 (triggered by `UnpinVersion` or next `ActivateVersion`).

### 8.2 Rollback

`RollbackHotReload(module_name, version_id)`:
- Restores specified version as active.
- Clears prepared (pending) runtime data.
- Rebinds execution chunk if no frames are active.

### 8.3 One-shot Apply

`ApplyHotReloadModule(module_name, candidate, level)`: Combined prepare+activate for convenience. Used by CLI default mode.

## 9. Debugger and Source Map Behavior

### 9.1 Versioned Backtrace

- `GetBacktrace(chunk, frames, chunk_resolver)` resolves each frame's source against its pinned version's chunk.
- `PrintBacktrace` shows `(version=N)` tag per frame.
- DAP `stackTrace` response includes `moduleVersion` field per frame.

### 9.2 DAP Integration

- `stopped` event includes `moduleVersion` (top frame's version).
- Custom `moduleVersionChange` event emitted when version changes between consecutive breaks. Body: `{"moduleVersion": N, "previousVersion": M}`.
- Breakpoints set by source path/line map against active version by default.
- Existing stopped frame remains mapped to its own versioned source map.

### 9.3 Debug REPL

- `RunDebugReplStep` uses `vm.GetChunkForVersion()` as chunk resolver for versioned backtrace.

## 10. Audit Log

### 10.1 Purpose

Every hot-reload operation (prepare, activate, rollback, retire) is recorded in an audit log for operational visibility, compliance, and post-incident analysis.

### 10.2 Data Model

`AuditEntry` fields:

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | `system_clock::time_point` | When the operation occurred (ms precision) |
| `module_name` | `string` | Target module |
| `action` | `AuditAction` | `Prepare`, `Activate`, `Rollback`, or `Retire` |
| `version_id` | `uint64_t` | Version involved in the operation |
| `previous_active_version` | `uint64_t` | Prior active version (for Activate) |
| `chunk_hash` | `string` | Chunk hash for traceability |
| `level` | `string` | Hot-reload level used (L0/L1) |
| `compat_result` | `string` | Compatibility check result |
| `actor` | `string` | Who/what triggered the operation |

### 10.3 Storage

- **In-memory ring buffer**: Default capacity 1024 entries. When full, trims oldest 25% to make room. Prevents unbounded memory growth.
- **Optional file persistence**: When `SetFilePath()` is called, each entry is appended to the specified file in JSON-lines format (one JSON object per line). Enables `tail -f` monitoring and log aggregation.
- **Format**: `FormatEntryJson()` produces ISO 8601 timestamps with millisecond precision, JSON-escaped strings.

### 10.4 Integration

- `HotReloadManager` owns an `AuditLog` instance and records entries in `PrepareHotReload`, `ActivatePrepared`, and `RollbackHotReload`.
- Exposed via `Vm::audit_log()` for programmatic access.
- CLI `--audit-log <path>` enables file persistence before any hot-reload operation.

### 10.5 Example JSON-lines output

```json
{"ts":"2026-04-29T14:30:00.123Z","module":"combat","action":"prepare","version":3,"prev_active":0,"hash":"a1b2c3","level":"L0","compat":"Compatible","actor":""}
{"ts":"2026-04-29T14:30:00.456Z","module":"combat","action":"activate","version":3,"prev_active":2,"hash":"","level":"","compat":"","actor":""}
```

## 11. Operational Interfaces

### 11.1 CLI

```
lpc hot-reload <module> [options]

Options:
  --check-only              Run compatibility check only (probe VM, no state mutation)
  --dry-run                 Compatibility check only (same as --check-only)
  --require-smoke <func>    Full pipeline: compat check + smoke run + activate
  --status                  Show module version chain status
  --allow-level L0|L1|L2    Compatibility level (default: L0)
  --audit-log <path>        Enable persistent audit log (JSON-lines format)
  (no flags)                Full apply: prepare + activate
```

Note: `--require-smoke` runs the specified entry function in an isolated VM before activating. If the smoke run fails, activation is aborted.

### 11.2 Runtime API

```cpp
RuntimeError LoadModuleChunkForHotReload(module_name, *out_chunk);
RuntimeError CheckHotReloadModule(module_name, candidate, level, *out_report);
RuntimeError PrepareHotReloadModule(module_name, candidate, level, smoke_function, *out_version, *out_status);
RuntimeError ActivatePreparedHotReloadModule(module_name, prepared_version, *out_status);
RuntimeError GetHotReloadModuleStatus(module_name, *out_status);
RuntimeError ApplyHotReloadModule(module_name, candidate, level, *out_status);
void SetHotReloadAuditLogPath(file_path);
```

Split prepare/activate is for programmatic in-process use. CLI uses `ApplyHotReloadModule` for one-shot apply.

### 11.3 Vm Member API

```cpp
Vm::PrepareHotReload(module_name, candidate, level, *out_version, *out_report, migration=nullptr);
Vm::ActivateHotReload(module_name, candidate_version, *out_previous_active);
Vm::RollbackHotReload(module_name, version_id);
Vm::GetHotReloadStatus(module_name);
Vm::GetChunkForVersion(module_version_id);
Vm::TryUpgradeObject(obj_id);
```

## 12. Data Migration (L2, implemented)

Migration descriptor (`MigrationDescriptor`) supports:
- Global variable mapping by name (rename, drop, add-with-default)
- Class field mapping by `old_field -> new_field` (via `ClassMigration`)
- Default value providers for new fields (`AddWithDefault`)
- Tombstone handling for removed fields (`Drop`)
- Custom transform callback for computed migrations

Usage:
```cpp
MigrationDescriptor migration;
migration.globals.entries.push_back({.old_name="x", .new_name="a", .kind=FieldMigrationKind::Rename});
migration.globals.entries.push_back({.old_name="y", .kind=FieldMigrationKind::Drop});
migration.globals.entries.push_back({.new_name="z", .kind=FieldMigrationKind::AddWithDefault, .default_value=Value::FromI64(7)});
migration.custom_transform = [](std::vector<Value>& globals) { /* ... */ };

vm.PrepareHotReload(module_name, candidate, HotReloadLevel::L2, &version, &report, &migration);
```

Validation:
- `CheckL2(old, new, migration)` ensures every removed global/class field is covered and every new global/class field has a default.
- Uncovered changes produce `Incompatible` report.

Execution:
- `MigrateObjectGlobals(globals, old_names, new_names, migration)` builds a new globals vector by name-based lookup.
- `MigrateClassFields(fields, old_field_names, new_field_names, class_migration)` builds new class field vector by name-based lookup.
- `UpgradeClassInstancesForModule(module_name, new_version_id)` migrates class instances on activation.
- Globals migration applied in `TryUpgradeObject()` when L2 migration descriptor exists for the target version.
- Class instances store `class_module_version_ids_` and `class_module_names_` for version tracking; `ResolveClassInfoFromHandle` resolves to the versioned chunk for correct field name resolution.

## 13. Failure Modes and Mitigations

| Failure | Prevention |
|---------|------------|
| Frame/version mismatch | Version-pinned frame metadata + dispatch loop `BindExecutionVersion` |
| Stale closure function id | Closure stores version + function identity |
| Object layout mismatch | L2 migration with explicit descriptor required; objects auto-upgrade on access for L0/L1/L2 |
| Debug source confusion | Module version in debug mapping; per-frame chunk resolution |
| Memory growth from old versions | Reference counting + retirement threshold (≤ 2 deprecated) |
| Cross-process state loss | CLI uses `ApplyHotReloadModule` (one-shot); split prepare/activate is in-process API only |

## 14. Performance Considerations

- Reload path can be slower; execution hot path must stay unchanged for non-reload workloads.
- **OPT1**: Version binding is O(1) pointer indirection via `bound_vrdata_` member, replacing deep-copy of 6 vectors per frame switch. `BoundChunk()/BoundIConst()/BoundFConst()/BoundSConst()` accessors dereference through the pointer.
- **OPT2**: Per-version `func_name_index` maps (`unordered_map<string, uint32_t>`) in `VersionRuntimeData` enable O(1) function lookup by name. `FindBoundFunction()/FindFunctionInModule()` use the pre-built index.
- **OPT3**: Per-version `global_name_index` and `ClassInfo::field_name_index` maps enable O(1) global and class field lookup by name.
- **OPT4**: `version_lookup_` map (`unordered_map<uint64_t, VersionRuntimeData*>`) provides O(1) version→VRData resolution, replacing O(N) scan across all modules. Used by `GetChunkForVersion()`, `FindFunctionInModule()`, `FindGlobalInModule()`.
- **OPT5**: L2 compat checker uses `unordered_set`/`unordered_map` for O(1) name lookups instead of O(N*M) nested loops.
- **OPT6**: `ResolveStringView()` returns `string_view` (zero-copy for string objects, uses caller-provided buffer for numeric conversions). `Op::Add` string concat uses `reserve+append` pattern to eliminate temporary string copies.
- **OPT7**: `module_class_instances_` index (`unordered_map<string, vector<size_t>>`) maps module name → class instance indices. `UpgradeClassInstancesForModule` iterates only relevant instances instead of scanning all `class_fields_`.
- Constant caches are prebuilt per version (`BuildVersionRuntimeData`).
- Old-version retention bounded by frame lifetime and retirement policy.
- Benchmarks: ~113 ns/op (int_add), ~2.7ms (fibonacci_20), ~5.1μs/op (hot_reload_100), ~10.9μs/op (object_upgrade_L1_50).

## 15. Security and Safety

- Reload source must be authenticated/trusted (out of scope for local dev, required for production).
- Audit log: entries are recorded in-memory and optionally persisted to file (JSON-lines format). File path is set via `--audit-log` CLI flag or `SetHotReloadAuditLogPath()` API.
- Bytecode validation: `LoadChunk` checks magic + version; compat checker validates structural integrity.

## 16. Test Matrix

### 16.1 Unit (via self-check)

- L0 compat checker: reject arity change, accept body-only change
- L1 compat checker: accept new function/class/global, reject removal/reorder/layout change
- Module registry state transitions
- Version refcount and retirement
- Explicit trigger enforcement (second LoadChunk rejected)
- Audit log: prepare/activate/rollback entry recording
- Audit log: rollback entry with correct version_id
- Audit log: ring buffer trim + JSON format validation

### 16.2 Integration (via self-check)

- Active call frame continues on old version after reload
- New invocation hits new version
- Debug + hot-reload interleaving (reload during step)
- API prepare/activate flow
- Smoke gate (invalid entry rejected, valid entry accepted + activated)
- Backtrace version correctness across mixed versions
- Cross-module LoadModule (two modules, independent version tracking)
- Cross-module hot-reload independence (reload B, A unaffected)
- Cross-module chunk resolution (GetChunkForVersion across modules)
- Object auto-upgrade L0 (version update, idempotent, field preservation)
- Object auto-upgrade L1 add-globals (new fields visible, existing values preserved, Nil defaults)
- Object auto-upgrade lazy on-access (GetObjectFieldNames/GetObjectField trigger upgrade automatically)
- L2 rename global with migration (x→a rename, y drop, z add-with-default, values preserved/mapped)
- L2 reject uncovered removal (removed global without Drop migration → Incompatible)
- L2 reject new field without default (new global without AddWithDefault → Incompatible)
- L2 object auto-upgrade with migration (score→rating rename, level drop, rank add-with-default)
- L2 custom transform callback (post-migration transform doubles value)
- L2 class field migration (y→z rename, w add-with-default, x preserved)
- L2 reject class field change without ClassMigration (→ Incompatible)

### 16.3 Stress (via self-check)

- 30 rapid reloads with active frame (retention stress)
- 50 rapid reloads with GC cycles (version leak detection)
- Long-lived closures across versions (TBD)

## 17. Key Implementation Decisions

1. **Explicit trigger**: `LoadChunk` is first-load-only. Users must handle cached data before calling hot-reload API. This prevents accidental implicit reload.
2. **Per-version `VersionRuntimeData`**: Stored in Vm rather than sharing single global chunk. Enables true dual-version execution without cache invalidation bugs.
3. **Frame pin/unpin**: At frame granularity. Deprecated versions kept alive while any frame references them. Retirement triggered on `UnpinVersion` when `ref_count` → 0.
4. **Bounded deprecated window**: ≤ 2 deprecated versions retained per module. `RetireDeprecatedVersions` called on `UnpinVersion` and `ActivateVersion`.
5. **CLI is stateless**: Each CLI invocation is a separate process. `--require-smoke` does the full pipeline (prepare + smoke + activate) in a single invocation. Split prepare/activate is a programmatic in-process API only.
6. **Smoke test isolation**: Runs in a separate `Vm` instance (`smoke_vm`), so side effects don't leak to the live VM.
7. **DAP `moduleVersionChange` event**: Custom event emitted when top-frame version changes between consecutive breaks. Allows DAP clients to detect and display hot-reload activity.
8. **Cross-module call_other resolves to target module's active version**: `call_other(obj, "func")` dispatches against the target object's module active version, not the caller's version. This matches LPC semantics where `call_other` calls into the object's current code. Cross-module frames get the target module's `module_name` and `module_version_id` for correct pin/unpin tracking.
9. **Per-module `ModuleRuntimeState` in `module_states_` map**: Replaces flat version-id-keyed maps (`version_runtime_data_`/`prepared_runtime_data_`). Each module has independent version tracking, enabling true multi-module hot-reload without cross-contamination.
10. **`LoadModule()` for multi-module**: First-load-only per module name; subsequent updates use hot-reload API. Enables loading multiple independent modules into a single VM instance.
11. **Object auto-upgrade is lazy**: Objects upgrade to their module's active version on access (call_other, GetObjectField, GetObjectFieldNames). This is transparent to the programmer and avoids the complexity of eager upgrade (which would need to scan all objects on activation). L0 upgrades are layout-safe; L1 upgrades extend globals with Nil defaults.
12. **L2 migration uses name-based field mapping**: `MigrateObjectGlobals()` resolves old→new globals by name (not by index), enabling renames and drops. This is more robust than index-based mapping and naturally handles reordering. A `MigrationDescriptor` must be provided at `PrepareHotReload` time; it is stored per-version and applied in `TryUpgradeObject` when the object upgrades to that version.
13. **Old global names resolved from version runtime data, not from `obj.blueprint`**: After `BindExecutionVersion`, `chunk_` is replaced with the new version's chunk. Since `obj.blueprint` may point to `&chunk_` (set during `LoadChunk`), reading `obj.blueprint->global_names` would give the new names instead of old. `TryUpgradeObject` resolves old names from `ModuleRuntimeState::version_runtime_data[obj.module_version_id]` to avoid this pitfall.
  14. **Class instances track module version**: `class_module_version_ids_` and `class_module_names_` vectors parallel `class_fields_` and `class_template_ids_`. `ResolveClassInfoFromHandle` resolves field names from the versioned chunk (not always `chunk_`). `UpgradeClassInstancesForModule` migrates class field values on activation, applying `MigrateClassFields` for L2 or extending with Nil for L1.
  15. **`version_lookup_` map for O(1) version resolution**: `unordered_map<uint64_t, VersionRuntimeData*>` provides direct lookup from version ID to runtime data, avoiding O(N) scan across modules. Populated in `LoadChunk`, `LoadModule`, `ActivateHotReload`. `unordered_map` element pointers are stable across inserts (C++17 guarantee).
  16. **`module_class_instances_` index for efficient class upgrade**: Maps module name → class instance indices. `UpgradeClassInstancesForModule` iterates only relevant instances instead of scanning all `class_fields_`. May contain stale indices after GC (acceptable — bounds checks skip invalid entries).
  17. **`ResolveStringView` for zero-copy string resolution**: Returns `string_view` into existing string data (sconst/heap); uses caller-provided `std::string &buf` for numeric conversions to avoid dangling views. `Op::Add` string concat path checks ObjRef before Float64 to correctly handle mixed-type concatenation.

## 18. Rollout Status

| Phase | Description | Status |
|-------|-------------|--------|
| A | ModuleRegistry + L0 compat + activation/rollback + CLI + debugger + smoke | **Complete** |
| B | Frame/object/closure version pinning completion + mixed-version debug | **Complete** (folded into Phase A) |
| C | L1 additive support | **Complete** |
| D | Audit log (in-memory ring buffer + file persistence + CLI flag) | **Complete** |
| E | Cross-module support (LoadModule, call_other resolution, GetChunkForVersion, independent hot-reload) | **Complete** |
| F | L2 migration engine (MigrationDescriptor, CheckL2, MigrateObjectGlobals, custom transform) | **Complete** |
| G | L2 class field migration (ClassMigration, MigrateClassFields, class instance upgrade) | **Complete** |
| H | Performance optimizations (OPT1-7: pointer indirection, pre-built indexes, version_lookup map, L2 O(1) checker, ResolveStringView, module_class_instances index) | **Complete** |
| I | Edge-case test coverage (retired version lookup, rollback + version_lookup, string concat float+string, class instance upgrade via index) | **Complete** |
| J | Phase 2: Full LSP server (JSON-RPC protocol, symbol index, document management, completion/hover/definition/references/rename) | **Complete** |
| K | Phase 3: Distributed hot reload protocol (binary wire format, coordinator, TCP transport) | **Complete** |
| L | Phase 5: Persistent object storage (binary codec, Value serialization, file I/O, module save/load, RehashAfterHotReload, chunk serialization) | **Complete** |

## 19. Phase 2: LSP Server

### Architecture

The LSP server is a standalone top-level module at `lsp/`, separate from the VM.

- `lsp/include/lsp/json_rpc.h`: JSON-RPC protocol layer
  - `JsonNode`: variant-based JSON tree (`JsonNull`, `bool`, `double`, `string`, `vector<JsonNode>`, `unordered_map<string, JsonNode>`)
  - `JsonSerialize`/`JsonParse`: full JSON serializer/parser
  - `EncodeRpcMessage`/`ReadRpcMessage`: Content-Length header framing
  - `JsonRpcRequest`/`JsonRpcResponse`/`RequestToJson`/`ResponseToJson`
- `lsp/include/lsp/lsp_server.h`: LSP server
  - `SymbolIndex`: name-to-symbol and URI-to-symbol indices, parse-on-index
  - `LspServer`: request handler with `SendMessageFn` callback for transport independence
  - Supported methods: `initialize`, `shutdown`, `textDocument/definition`, `textDocument/references`, `textDocument/rename`, `textDocument/hover`, `textDocument/completion`, `textDocument/documentSymbol`, `workspace/symbol`
  - Notifications: `initialized`, `textDocument/didOpen`, `textDocument/didChange`, `textDocument/didClose`, `exit`

### Symbol Parsing

The `SymbolIndex::ParseDocument` method performs lightweight regex-free parsing:
- `inherit` lines → `inherit` symbol kind
- `class` lines → `class` symbol kind
- Lines with type keywords (`void`, `int`, `float`, `string`, etc.) and `(` → `function` symbol
- `var` lines → `variable` symbol kind

## 20. Phase 3: Distributed Hot Reload Protocol

### Wire Format

Binary protocol with `"LPCR"` magic + u32 length prefix:
```
[u32 payload_length][LPCR magic][u8 msg_type][u32 request_id]
[str module_name][u64 version_id][str chunk_data]
[str compat_level][str migration_data][str status_json][str error_message]
[u8 success]
```

### Message Types

| Type | Code | Direction |
|------|------|-----------|
| Hello | 0x01 | Any → Any |
| HelloAck | 0x02 | Response |
| PrepareReload | 0x10 | Coordinator → Peer |
| PrepareResult | 0x11 | Peer → Coordinator |
| ActivateReload | 0x12 | Coordinator → Peer |
| ActivateResult | 0x13 | Peer → Coordinator |
| RollbackReload | 0x14 | Coordinator → Peer |
| RollbackResult | 0x15 | Peer → Coordinator |
| StatusQuery | 0x20 | Any → Any |
| StatusResponse | 0x21 | Response |
| SyncState | 0x30 | Coordinator → New Peer |
| SyncAck | 0x31 | New Peer → Coordinator |
| Heartbeat | 0x40 | Any → Any |
| Error | 0xFF | Error response |

### Coordinator

`HotReloadCoordinator` manages a set of peers and broadcasts reload operations:
- `PrepareAndBroadcast`: sends PrepareReload to all peers, collects results
- `ActivateAndBroadcast`: sends ActivateReload to all peers
- `RollbackAndBroadcast`: sends RollbackReload to all peers
- `SyncStateToPeer`: sends full module state to a newly connected peer

### TCP Transport

`HotReloadTcpTransport` wraps WinSock2 (Windows) or POSIX sockets:
- `Listen(port)`: accept incoming peer connections
- `Connect(host, port)`: connect to a coordinator
- `Poll(timeout_ms)`: non-blocking receive with length-prefixed framing
- `Send`/`Broadcast`: send to specific peer or all peers
- Links to `ws2_32.lib` on Windows

## 21. Phase 5: Persistent Object Storage

### Storage Format

Binary serialization of `StoredValue`/`StoredObject`/`PersistedModuleState`:
- `StoredValue`: type tag (u8) + type-specific payload (int/float/string/array/mapping/class/object/closure/bool)
- `StoredObject`: module_name + version_id + globals vector + object_type
- `PersistedModuleState`: module_name + active_version_id + serialized chunk + objects vector

### PersistenceCodec

Static encode/decode methods:
- `SerializeValue`/`DeserializeValue`: recursive value serialization
- `SerializeObject`/`DeserializeObject`: object state serialization
- `SerializeModuleState`/`DeserializeModuleState`: full module state

### PersistentStorage

File-based persistence manager:
- `SaveModuleState(vm, module_name)`: serialize + write to `base_dir/module_name.lpcp`
- `LoadModuleState(vm, module_name)`: read + deserialize + restore
- `SaveAllModules`/`LoadAllModules`: batch save/load
- `RehashAfterHotReload(vm, module_name, new_version_id)`: rehash string/objref-based data structures after a hot-reload changes chunk layout

### Chunk Serialization

`SerializeChunk`/`DeserializeChunk` in `vm/src/bytecode/chunk_serialization.cpp`:
- V2 binary format with `"LPC\0"` magic
- Little-endian encoding for all integers/floats
- Serializes: module_name, iconst, fconst, sconst, code, global_names, functions (name, nlocals, code_start, code_end), classes (name, field_names, field_name_index)

## 22. LSP Module (Standalone)

The LSP server is built as an independent top-level module `lsp/`, separate from the VM.

### Structure

- `lsp/include/lsp/json_rpc.h`: JSON node variant, JSON serializer/parser, JSON-RPC framing (Content-Length)
- `lsp/include/lsp/lsp_server.h`: LspServer class, SymbolIndex, Position/Range/Location types
- `lsp/src/json_rpc.cpp`: Full JSON serializer/parser, Content-Length framing
- `lsp/src/lsp_server.cpp`: Full LSP implementation (initialize, shutdown, completion, hover, definition, references, rename, documentSymbol, workspaceSymbol, didOpen/DidChange/DidClose)
- `lsp/main.cpp`: Standalone `lpc_lsp` binary entry point
- `lsp/CMakeLists.txt`: Builds `lpc_lsp_lib` (static library) + `lpc_lsp` (executable)

### Build Targets

- `lpc_lsp`: Standalone LSP server binary (reads stdin, writes stdout, Content-Length framed JSON-RPC)
- `lpc_lsp_lib`: Static library linked by both `lpc_lsp` and `lpc_vm` (for `lpc lsp` command and self-check tests)

### VSCode Integration

- `vscode_plugin/src/lspClient.js`: Connects to `lpc_lsp` via `vscode-languageclient` (stdio transport)
- `lpc.lspPath` configuration: default `${workspaceFolder}/build/lsp/lpc_lsp`
- `lpc.restartLsp` command: restart the language server
- Extension activates LSP client on startup; deactivation stops the client

### Performance Benchmarks

#### Baseline (Release -O3, before optimizations)

| Benchmark | ops | ns/op (baseline) |
|-----------|-----|-------|
| int_add_loop | 10000 | ~80-100 |
| fibonacci_20 | 1 | ~2,700,000 |
| hot_reload_100_cycles | 100 | ~4,000-6,000 |
| json_rpc_serialize_10k | 10000 | ~630 |
| json_rpc_parse_10k | 10000 | ~960 |
| reload_protocol_encode_10k | 10000 | ~480 |
| persistence_codec_value_10k | 10000 | ~645-928 |
| chunk_serialization_1k | 1000 | ~1,400-1,700 |

#### After optimizations (Release -O3)

| Benchmark | ops | ns/op (optimized) | Speedup |
|-----------|-----|-------|---------|
| int_add_loop | 10000 | ~56 | **1.4-1.8x** |
| fibonacci_20 | 1 | ~2,020,000 | **1.3x** |
| hot_reload_100_cycles | 100 | ~3,800 | **1.1-1.6x** |
| json_rpc_serialize_10k | 10000 | ~215 | **2.9x** |
| json_rpc_parse_10k | 10000 | ~640 | **1.5x** |
| reload_protocol_encode_10k | 10000 | ~450 | ~1.0x |
| persistence_codec_value_10k | 10000 | ~68 | **9.5-13.7x** |
| chunk_serialization_1k | 1000 | ~1,440 | ~1.0x |

### §23 Performance Optimization Details

1. **JSON serialize** (2.9x): `ostringstream` replaced with direct `std::string` builder using `out.append()`/`out.push_back()` + `snprintf` for numeric formatting. Eliminated `<sstream>` overhead.

2. **JSON parse** (1.5x): Custom inline float parser in `ParseNumber()` — scans characters directly without `std::stod` + `substr` allocation. Integer part accumulated via `val = val * 10 + digit`; fractional part via `frac/div` accumulation; exponent via `pow(10, exp)`. No temporary string allocation, no locale-aware conversion.

3. **Persistence codec** (9.5-13.7x): `SerializeValueFlat(val, buf)` — flat buffer serialization. Eliminated per-element sub-buffer allocation + copy. Removed length prefixes before sub-elements. Both serializer and deserializer updated to sequential flat format.

4. **VM dispatch** (1.3x): Removed `bound_module_name_ != cur.module_name` string compare in version binding — only `bound_module_version_id_ != cur.module_version_id` integer compare needed (version IDs are globally unique across modules).

5. **BuildChunkHash** (contributes to hot_reload): `ostringstream` replaced with `snprintf(buf, sizeof(buf), "%zx", seed)` in hot_reload.cpp. Eliminated `<sstream>` overhead.

6. **RetireDeprecatedVersions**: Early exit if `deprecated.size() <= keep_deprecated_limit`; smaller `reserve(std::min(versions.size(), 8))`.

7. **JSON parse fast string path**: `ParseString` fast-path for no-escape strings — scan to end, single `substr` call instead of char-by-char append.
