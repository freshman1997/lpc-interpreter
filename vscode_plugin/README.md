# LPC Language Tools

This VS Code extension is the first project-local integration layer for the LPC compiler and VM.

## Features

- `.lpc` language registration
- Syntax highlighting, brackets, comments, folding, and snippets
- `LPC: Compile Current File`
- `LPC: Run Current File`
- `LPC: Debug Current File`
- `lpc` debug type with VS Code breakpoints translatord into `lpc_vm debug`
- Compiler diagnostics surfaced as VS Code problems

## Use From This Repository

1. Build the project with CMake so `build/compiler/lpc_compiler.exe` and `build/vm/lpc_vm.exe` exist.
2. Open `vscode_plugin` in VS Code's Extension Development Host, or package it with `vsce`.
3. Open an `.lpc` file in this repository.
4. Run `LPC: Compile Current File`, `LPC: Run Current File`, or `LPC: Debug Current File`.

Default settings assume the repository layout:

```json
{
  "lpc.compilerPath": "${workspaceFolder}/build/compiler/lpc_compiler",
  "lpc.vmPath": "${workspaceFolder}/build/vm/lpc_vm",
  "lpc.outRoot": "${workspaceFolder}/bin",
  "lpc.includeDirs": ["${workspaceFolder}"]
}
```

On Windows, the extension also searches `.exe`, `Debug`, and `Release` variants automatically.

## Debugger Notes

The adapter intentionally reuses the current VM debugger command set for run control and breakpoints. It enables `LPC_DEBUG_PROTOCOL=json` and reads structured stopped, terminated, locals, args, breakpoint, evaluate, and runtime-error events when the VM supports them.

The current VM debugger still accepts text commands for run control. The JSONL protocol is documented in `docs/DEBUG_PROTOCOL_JSONL.md`.

Example `launch.json` and `tasks.json` files are available in `examples/`.
