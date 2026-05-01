# VM Benchmarking

Use `scripts/benchmark_vm.ps1` to keep VM and GC optimization work measurable.

## Run The Default Suite

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark_vm.ps1
```

Default cases cover recursion, Fibonacci, mapping operations, allocation churn, and inheritance/`instanceof` stress.

## Write A JSON Baseline

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark_vm.ps1 -Iterations 10 -Out .\log\bench-baseline.json
```

The JSON includes average, median, min, max wall-clock runtime, and the last `--profile` output for each test.

## Compare Optimizations

1. Run a baseline before changing VM code.
2. Make one optimization at a time.
3. Rebuild Release and rerun the same command.
4. Treat regressions in `test_gc_churn` and `test_mapping` as GC/allocation signals first.

Useful focused commands:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark_vm.ps1 -Tests test_gc_churn -Iterations 20 -Out .\log\bench-gc.json
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark_vm.ps1 -Tests test_fib,test_recursion -Iterations 20 -Out .\log\bench-dispatch.json
```

## Compare With Other Runtimes

The repository includes same-shape microbenchmarks under `benchmarks/runtime/` for LPC, Python, JavaScript, and Lua. The comparison script runs runtimes that are installed on the current machine and skips the rest.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\compare_runtime_benchmarks.ps1 -Iterations 5 -Out .\log\runtime-compare.json
```

Use the result as a direction finder rather than an absolute language ranking: startup time, JIT warmup, and each language's native containers all affect small benchmarks.

The latest local analysis is summarized in `docs/PERFORMANCE_ANALYSIS.md`.
