param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputDir = "",
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path $Root).Path
$PluginDir = Join-Path $Root "vscode_plugin"
if (-not (Test-Path $PluginDir)) {
    throw "vscode_plugin directory not found: $PluginDir"
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $Root "dist"
}
$OutputDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Push-Location $PluginDir
try {
    if (-not $SkipInstall) {
        if (Test-Path "package-lock.json") {
            npm ci
        } else {
            npm install
        }
    }

    $package = Get-Content "package.json" -Raw | ConvertFrom-Json
    $vsixName = "$($package.name)-$($package.version).vsix"
    $vsixPath = Join-Path $OutputDir $vsixName

    npx --yes @vscode/vsce package --out $vsixPath
    Write-Host "VS Code extension packaged:"
    Write-Host "  $vsixPath"
    Write-Host ""
    Write-Host "Install locally with:"
    Write-Host "  code --install-extension `"$vsixPath`""
} finally {
    Pop-Location
}
