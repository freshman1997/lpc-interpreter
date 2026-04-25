# VM Compare Matrix (Current)

This file tracks modules that are expected to pass VM1 vs NextVM compare in current stage.

## Passing samples

- `compiler/tests/golden/sample_nextvm_translate_min`
- `compiler/tests/golden/sample_nextvm_compare_ok`
- `compiler/tests/golden/sample_nextvm_compare_eq`
- `compiler/tests/golden/sample_nextvm_compare_neq`
- `compiler/tests/golden/sample_nextvm_compare_lte`
- `compiler/tests/golden/sample_nextvm_compare_logic_ops`
- `compiler/tests/golden/sample_nextvm_compare_logic_call`
- `compiler/tests/golden/sample_nextvm_compare_mul_div_mod`
- `compiler/tests/golden/sample_nextvm_compare_class_field`
- `compiler/tests/golden/sample_nextvm_compare_array_index`
- `compiler/tests/golden/sample_nextvm_compare_index_mul`
- `compiler/tests/golden/sample_nextvm_compare_gte_lt`
- `compiler/tests/golden/sample_nextvm_compare_not_or`

## Not in compare scope yet

- full array/index bytecode parity
- switch-table heavy modules
- efun-rich modules (beyond current minimal translator coverage)

## Known note

- (resolved) previous legacy memory pressure note for `sample_nextvm_compare_class_field` should be fully removed once compare matrix runs all-pass without note fallback.
