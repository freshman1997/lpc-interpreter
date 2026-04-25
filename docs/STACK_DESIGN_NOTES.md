# VM Stack Design Notes

## Current Design

The older runtime uses one contiguous `lpc_stack_t` for operands, arguments, locals, and return values.

Each `call_info_t` stores raw pointers into that stack:

- `base`: frame base
- `top`: intended frame top
- `savepc`: return pc
- `pre` / `next`: call chain

The older runtime now also records stack offsets alongside those raw pointers:

- `base_index`: frame base offset in `lpc_stack_t`
- `top_index`: last reserved local/operand slot captured during frame setup

Raw pointers remain for interpreter/debugger compatibility, but GC and stack guard code can start using offsets as the stable identity.

This is simple and fast enough for the current interpreter, but it has stability costs.

## Current Risks

- Frame state is split between `lpc_stack_t::idx`, `call_info_t::base`, `call_info_t::top`, and function metadata.
- `pop()` protects only against popping below current frame base; it does not describe operand stack height explicitly.
- `pop_frame()` reconstructs how many values to remove from function metadata and current top, which is brittle when bytecode is wrong.
- Raw stack pointers make future stack resizing hard.
- GC root scanning depends on frame pointer ranges being correct.
- Debugger locals/args depend on the same raw frame layout.

## Better Direction

Use an explicit frame-window stack model:

- Keep one `ValueStack` storage vector/array.
- Store stack offsets instead of raw pointers in frames:
  - `base_index`
  - `stack_top_index`
  - `return_pc`
  - `function_index`
- Define a strict call ABI:
  - caller pushes args
  - callee frame owns `[base, frame_top)`
  - operand top is always explicit
  - return leaves exactly one value for non-void functions, zero for void
- Verify max stack and local count before execution.

## Recommended Migration

1. Keep the current stack implementation for legacy compatibility. Done.
2. Add frame offset helpers to `lpc_stack_t`: `index_of`, `at_index`, `contains`, and `valid_range`. Done.
3. Store `base_index`/`top_index` in `call_info_t` during frame creation and call-other transitions. Done.
4. Scan GC stack roots through stack offsets and the current operand top instead of trusting stale raw frame-top pointers. Done.
5. Add invariant checks around frame push/pop and debug builds:
   - `base <= idx <= size`
   - frame ranges are inside stack storage
   - return value count matches function return type
6. Move debugger locals/args and frame-info APIs to offsets while preserving public behavior.
7. Avoid reconstructing pop count in `pop_frame()` from ambiguous state; prefer stored frame stack height.

## Near-Term Hardening

- Add self-checks for stack underflow at frame boundaries.
- Add bytecode verifier stack-depth validation for V1 bytecode.
- Add explicit frame stack-height accounting for return-value validation.
- Convert debugger frame inspection to offset ranges.
