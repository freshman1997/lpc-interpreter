param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$Root = (Resolve-Path $Root).Path
$config = Join-Path $Root ".gitleaks.toml"
$reportDir = Join-Path $Root ".cache"
$report = Join-Path $reportDir "gitleaks-report.json"

if (-not (Test-Path $config)) {
    Write-Error "Missing gitleaks config: $config"
    exit 2
}

$gitleaks = Get-Command gitleaks -ErrorAction SilentlyContinue
if (-not $gitleaks) {
    Write-Host "gitleaks not found in PATH."
    Write-Host "Install: https://github.com/gitleaks/gitleaks"
    exit 2
}

if (-not (Test-Path $reportDir)) {
    New-Item -ItemType Directory -Path $reportDir | Out-Null
}

Push-Location $Root
try {
    & gitleaks detect --source . --config $config --report-path $report --report-format json --redact
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Security scan passed."
        Write-Host "Report: $report"
        exit 0
    }

    Write-Host "Security scan failed."
    Write-Host "Report: $report"
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
