param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string[]]$Tests = @(
        "test_fib",
        "test_recursion",
        "test_gc_churn",
        "test_mapping",
        "test_inherit_instanceof_stress"
    ),
    [int]$Iterations = 5,
    [switch]$SkipBuild,
    [switch]$NoProfile,
    [switch]$VmRelease,
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path $Root).Path
$Compiler = Join-Path $Root "build\compiler\lpc_compiler.exe"
$Vm = Join-Path $Root "build\vm\lpc_vm.exe"
$OutRoot = Join-Path $Root "bin"
$BenchTests = @()
foreach ($item in $Tests) {
    if ([string]::IsNullOrWhiteSpace($item)) {
        continue
    }
    $BenchTests += ($item -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

if (-not $SkipBuild) {
    cmake --build (Join-Path $Root "build") --config Release
}

if (-not (Test-Path $Compiler)) {
    throw "compiler not found: $Compiler"
}
if (-not (Test-Path $Vm)) {
    throw "vm not found: $Vm"
}

$results = @()
foreach ($test in $BenchTests) {
    $source = Join-Path $Root ($test + ".lpc")
    if (-not (Test-Path $source)) {
        Write-Warning "skip missing benchmark source: $source"
        continue
    }

    & $Compiler $source --workspace-root $Root --out-root $OutRoot | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "compile failed for $test"
    }

    $times = @()
    $lastProfile = @()
    for ($i = 0; $i -lt $Iterations; ++$i) {
        $runArgs = @("run", $test, "--bytecode-root", $OutRoot)
        if ($VmRelease) {
            $runArgs += "--release"
        }
        if (-not $NoProfile) {
            $runArgs += "--profile"
        }

        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $runOutput = & $Vm @runArgs 2>&1
        $sw.Stop()
        if ($LASTEXITCODE -ne 0) {
            $runOutput | Out-Host
            throw "vm run failed for $test"
        }
        $times += $sw.Elapsed.TotalMilliseconds
        if (-not $NoProfile) {
            $lastProfile = $runOutput | Where-Object { $_.ToString().StartsWith("[profile]") }
        }
    }

    $sorted = $times | Sort-Object
    $avg = ($times | Measure-Object -Average).Average
    $min = ($times | Measure-Object -Minimum).Minimum
    $max = ($times | Measure-Object -Maximum).Maximum
    $median = $sorted[[int][Math]::Floor($sorted.Count / 2)]

    $row = [PSCustomObject]@{
        test = $test
        iterations = $Iterations
        avg_ms = [Math]::Round($avg, 3)
        median_ms = [Math]::Round($median, 3)
        min_ms = [Math]::Round($min, 3)
        max_ms = [Math]::Round($max, 3)
        profile = $lastProfile
    }
    $results += $row
}

$results | Format-Table test, iterations, avg_ms, median_ms, min_ms, max_ms -AutoSize

if (-not [string]::IsNullOrWhiteSpace($Out)) {
    $outPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
    $results | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $outPath
    Write-Host "Benchmark JSON written:"
    Write-Host "  $outPath"
}
