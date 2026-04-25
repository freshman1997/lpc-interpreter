# VM GC Evolution Plan

## Current Baseline

- Collector: stop-the-world mark-sweep.
- Trigger: allocation threshold and periodic call-frame pop checks.
- Roots: call frames + object cache mapping.

## Recently Fixed P0 Issues

- Root marking no longer depends on having an active call frame.
- Root worklist entries are now recursively scanned during major GC.
- Sweep now terminates survivor linked-list tail explicitly.
- Realloc accounting uses old-size/new-size delta.
- String ownership and free path are aligned.
- GC stats are exposed through VM APIs.
- VM self-check covers major-GC root container children.
- Newly allocated array slots are initialized, so GC never scans random value bits as object pointers.
- GC stack root scanning now uses stack offsets and the active operand top instead of depending on raw frame-top pointers.

## Next Step (Phase-1): Barrier Preparation

- Add no-op write barrier API and count invocations.
- Route barrier calls through hot mutation paths:
  - array element store
  - mapping set/upset
  - class field set
  - closure upvalue set
- Keep behavior unchanged for now (instrumentation only).

Current coverage:

- Array element set/upset routes through the barrier.
- Mapping set/upset routes through the barrier.
- Class field set routes through the barrier.
- Closure upvalue set now routes through the barrier.
- VM self-check covers old closure -> young array upvalue writes and verifies remembered-set insertion.
- Minor GC scans array elements, mapping keys/values, object fields, and closure upvalues from the remembered set.
- VM self-check covers old closure/array/mapping -> young array survival through minor GC.

## Phase-2: Young Generation Prototype

- Add generation metadata in gc header (`young`, `age`).
- Maintain a nursery allocation region/list. Prototype metadata and minor collection entrypoint are in place.
- Minor collection scans:
  - roots
  - remembered set (old->young writes)
- Promote survivors over age threshold.

Remaining GC hardening:

- Add object-field old-to-young minor survival coverage with a real object proto.
- Add V1 bytecode stack-depth verification so malformed bytecode cannot confuse GC roots.
- Split nursery accounting from total allocation accounting before tuning thresholds.

## Phase-3: Mixed Collection Policy

- Keep full mark-sweep for old generation fallback.
- Trigger policy:
  - minor GC on nursery pressure
  - major GC on old-space/high-water pressure

## Success Metrics

- Lower pause time p95 under allocation-heavy scripts.
- Fewer full-heap collections.
- Stable memory usage under same workload.
