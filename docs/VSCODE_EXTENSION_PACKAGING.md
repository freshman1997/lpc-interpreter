# VS Code Extension Packaging

This repository ships the editor integration in `vscode_plugin/`.

## Build Native Tools First

```powershell
cmake --build .\build --config Release
```

The extension resolves the compiler, VM, and LSP from the current workspace by default:

- `build/compiler/lpc_compiler(.exe)`
- `build/vm/lpc_vm(.exe)`
- `build/lsp/lpc_lsp(.exe)`

## Package VSIX

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_vscode_plugin.ps1
```

The script runs `npm ci` in `vscode_plugin/`, invokes `@vscode/vsce`, and writes the package to `dist/`.

To skip dependency install after the first successful run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_vscode_plugin.ps1 -SkipInstall
```

Install the generated VSIX locally:

```powershell
code --install-extension .\dist\lpc-language-tools-0.1.0.vsix
```

## Development Host

Open `vscode_plugin/` in VS Code and press `F5` to launch an Extension Development Host. Open this repository in that host window before running `LPC: Compile Current File`, `LPC: Run Current File`, or `LPC: Debug Current File`.

## Workspace Settings

Use `vscode_plugin/examples/settings.json` as a starting point for `.vscode/settings.json` in an LPC workspace.

If your CMake generator writes executables under `Release/`, use these path values instead:

```json
{
  "lpc.compilerPath": "${workspaceFolder}/build/compiler/Release/lpc_compiler.exe",
  "lpc.vmPath": "${workspaceFolder}/build/vm/Release/lpc_vm.exe",
  "lpc.lspPath": "${workspaceFolder}/build/lsp/Release/lpc_lsp.exe"
}
```
