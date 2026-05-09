param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string[]]$Benchmarks = @("int_loop", "fib_rec", "array_churn", "mapping_churn", "float_math", "string_ops", "class_field", "closure_call", "regex_match", "foreach_iter", "bitwise_ops", "mapping_ops", "object_lifecycle", "mixed_workload", "branchy_control_flow"),
    [int]$Iterations = 5,
    [switch]$SkipBuild,
    [switch]$VmRelease,
    [string]$LuaPath = "",
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path $Root).Path
$BenchDir = Join-Path $Root "benchmarks\runtime"
$OutRoot = Join-Path $Root "bin"
$Compiler = Join-Path $Root "build\compiler\lpc_compiler.exe"
$Vm = Join-Path $Root "build\vm\lpc_vm.exe"

if (-not $SkipBuild) {
    cmake --build (Join-Path $Root "build") --config Release
}

function Find-Tool([string[]]$Names) {
    foreach ($name in $Names) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($cmd) {
            return $cmd.Source
        }
    }
    return $null
}

function Measure-Native([string]$Exe, [string[]]$ToolArgs, [string]$Cwd, [int]$Count) {
    $times = @()
    $lastOutput = @()
    for ($i = 0; $i -lt $Count; ++$i) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $output = & $Exe @ToolArgs 2>&1
        $sw.Stop()
        if ($LASTEXITCODE -ne 0) {
            $output | Out-Host
            throw "command failed: $Exe $($ToolArgs -join ' ')"
        }
        $times += $sw.Elapsed.TotalMilliseconds
        $lastOutput = $output
    }
    $sorted = $times | Sort-Object
    [PSCustomObject]@{
        avg_ms = [Math]::Round(($times | Measure-Object -Average).Average, 3)
        median_ms = [Math]::Round($sorted[[int][Math]::Floor($sorted.Count / 2)], 3)
        min_ms = [Math]::Round(($times | Measure-Object -Minimum).Minimum, 3)
        max_ms = [Math]::Round(($times | Measure-Object -Maximum).Maximum, 3)
        output = $lastOutput
    }
}

$python = Find-Tool @("python", "python3")
$node = Find-Tool @("node")
$lua = if ($LuaPath -and (Test-Path $LuaPath)) { $LuaPath } else { Find-Tool @("luajit", "lua") }

$rows = @()
foreach ($bench in $Benchmarks) {
    $lpc = Join-Path $BenchDir ($bench + ".lpc")
    if (-not (Test-Path $lpc)) {
        Write-Warning "missing LPC benchmark: $lpc"
        continue
    }

    & $Compiler $lpc --workspace-root $Root --out-root $OutRoot | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "LPC compile failed: $bench"
    }

    $vmArgs = @("run", "benchmarks/runtime/$bench", "--bytecode-root", $OutRoot)
    if ($VmRelease) {
        $vmArgs += "--release"
    }
    $lpcResult = Measure-Native -Exe $Vm -ToolArgs $vmArgs -Cwd $Root -Count $Iterations
    $profileArgs = $vmArgs + @("--profile")
    $profileOutput = & $Vm @profileArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        $profileOutput | Out-Host
        throw "LPC profile run failed: $bench"
    }
    $rows += [PSCustomObject]@{
        benchmark = $bench
        runtime = "lpc_vm"
        iterations = $Iterations
        avg_ms = $lpcResult.avg_ms
        median_ms = $lpcResult.median_ms
        min_ms = $lpcResult.min_ms
        max_ms = $lpcResult.max_ms
        slowdown_vs_lpc = 1.0
        profile = @($profileOutput | Where-Object { $_.ToString().StartsWith("[profile]") })
    }

    $targets = @()
    if ($python) { $targets += [PSCustomObject]@{ runtime = "python"; exe = $python; args = @((Join-Path $BenchDir ($bench + ".py"))) } }
    if ($node) { $targets += [PSCustomObject]@{ runtime = "node"; exe = $node; args = @((Join-Path $BenchDir ($bench + ".js"))) } }
    if ($lua) { $targets += [PSCustomObject]@{ runtime = (Split-Path -Leaf $lua); exe = $lua; args = @((Join-Path $BenchDir ($bench + ".lua"))) } }

    foreach ($target in $targets) {
        $result = Measure-Native -Exe $target.exe -ToolArgs $target.args -Cwd $Root -Count $Iterations
        $rows += [PSCustomObject]@{
            benchmark = $bench
            runtime = $target.runtime
            iterations = $Iterations
            avg_ms = $result.avg_ms
            median_ms = $result.median_ms
            min_ms = $result.min_ms
            max_ms = $result.max_ms
            slowdown_vs_lpc = [Math]::Round($result.median_ms / $lpcResult.median_ms, 3)
            profile = @()
        }
    }
}

$rows | Format-Table benchmark, runtime, iterations, avg_ms, median_ms, min_ms, max_ms, slowdown_vs_lpc -AutoSize

if (-not [string]::IsNullOrWhiteSpace($Out)) {
    $outPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
    $rows | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $outPath
    Write-Host "Runtime comparison JSON written:"
    Write-Host "  $outPath"
}
