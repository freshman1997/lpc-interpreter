# VM GC Evolution Plan

## Current Baseline

- Collector: stop-the-world mark-sweep.
- Trigger: allocation threshold and periodic call-frame pop checks.
- Roots: call frames + object cache mapping.

## Recently Fixed P0 Issues

- Root marking no longer depends on having an active call frame.
- Sweep now terminates survivor linked-list tail explicitly.
- Realloc accounting uses old-size/new-size delta.
- String ownership and free path are aligned.
- GC stats are exposed through VM APIs.

## Next Step (Phase-1): Barrier Preparation

- Add no-op write barrier API and count invocations.
- Route barrier calls through hot mutation paths:
  - array element store
  - mapping set/upset
  - class field set
- Keep behavior unchanged for now (instrumentation only).

## Phase-2: Young Generation Prototype

- Add generation metadata in gc header (`young`, `age`).
- Maintain a nursery allocation region/list.
- Minor collection scans:
  - roots
  - remembered set (old->young writes)
- Promote survivors over age threshold.

## Phase-3: Mixed Collection Policy

- Keep full mark-sweep for old generation fallback.
- Trigger policy:
  - minor GC on nursery pressure
  - major GC on old-space/high-water pressure

## Success Metrics

- Lower pause time p95 under allocation-heavy scripts.
- Fewer full-heap collections.
- Stable memory usage under same workload.
