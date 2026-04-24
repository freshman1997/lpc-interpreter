# VM V1 Verifier

A first-pass bytecode verifier has been added for existing v1 instructions.

## Scope

- Validates opcode value range.
- Validates operand byte width for each instruction form.
- Detects truncated instruction tails.
- Validates embedded sub-opcode/value forms (`op_upset`, `op_call`, `op_switch`, `op_foreach_step2`).

## Integration point

- Verifier runs in object loading path after bytecode read and before runtime use.
- On failure, VM aborts loading with offset and reason.

## Current limitations

- Does not yet validate stack effect correctness.
- Does not yet validate jump targets against instruction boundaries.
- Does not yet validate const/local/upvalue index bounds per function.

## Next verifier upgrades

1. Add stack-effect simulation per function.
2. Validate branch target alignment.
3. Validate function-range local/upvalue operand limits.
