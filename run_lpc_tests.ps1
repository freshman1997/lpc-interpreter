param(
    [string]$Root = $PSScriptRoot
)

$Root = (Resolve-Path $Root).Path
$compiler = Join-Path $Root "build\compiler\lpc_compiler.exe"
$vm = Join-Path $Root "build\vm\lpc_vm.exe"
$outRoot = Join-Path $Root "bin"

$tests = @(
    "test_string",
    "test_array",
    "test_mapping",
    "test_efuns",
    "test_inherit",
    "test_inherit_chain",
    "test_instanceof",
    "test_closure",
    "test_fib",
    "test_arithmetic",
    "test_class_incr",
    "test_globals",
    "test_globals2",
    "test_globals_init",
    "test_catch",
    "test_foreach",
    "test_foreach2",
    "test_foreach3",
    "test_foreach_break",
    "test_efun",
    "test_efuns2",
    "test_bitwise",
    "test_sc_and",
    "test_sc_or",
    "test_sc_if",
    "test_sc_call",
    "test_sc_func",
    "test_shortcircuit",
    "test_shortcircuit2",
    "test_simple_call",
    "test_compare_logic",
    "test_recursion",
    "test_closure_object",
    "test_multi_object",
    "test_upset",
    "test_incr",
    "test_stack_fixes",
    "test_stack_fixes2",
    "test_features",
    "test_gc_churn",
    "test_inherit_instanceof_stress",
    "test_inherit_module",
    "test_inherit_multi",
    "test_call_other",
    "test_call_other2"
)

$pass = 0
$fail = 0

foreach ($t in $tests) {
    $lpc = Join-Path $Root ($t + ".lpc")
    if (-not (Test-Path $lpc)) {
        continue
    }

    $compileOutput = & $compiler $lpc --workspace-root $Root --out-root $outRoot 2>&1
    if ($LASTEXITCODE -ne 0) {
        $fail++
        Write-Host "FAIL: $t"
        Write-Host "compile failed:"
        Write-Host $compileOutput
        continue
    }

    $runOutput = & $vm run $t --bytecode-root $outRoot 2>&1
    if ($LASTEXITCODE -eq 0) {
        $pass++
    } else {
        $fail++
        Write-Host "FAIL: $t"
        Write-Host $runOutput
    }
}

Write-Host "Pass: $pass / $($pass + $fail)"
if ($fail -gt 0) { exit 1 }
