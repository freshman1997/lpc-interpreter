# LPC Language Tools

This VS Code extension is the first project-local integration layer for the LPC compiler and VM.

## Features

- `.lpc` language registration
- Syntax highlighting, brackets, comments, folding, and snippets
- `LPC: Compile Current File`
- `LPC: Run Current File`
- `LPC: Debug Current File`
- `LPC: Attach to VM`
- `lpc` debug type with VS Code breakpoints translatord into `lpc_vm debug`
- Compiler diagnostics surfaced as VS Code problems

## Use From This Repository

1. Build the project with CMake so `build/compiler/lpc_compiler.exe` and `build/vm/lpc_vm.exe` exist.
2. Open `vscode_plugin` in VS Code's Extension Development Host, or package it with `scripts/package_vscode_plugin.ps1`.
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

`LPC: Debug Current File` compiles the active file, derives the VM module name from its workspace-relative path, and starts the VM with `entryFunction` defaulting to `main`. For example, `base/module/hero/proto.lpc` launches module `base/module/hero/proto`.

If the file has no zero-argument `main`, the VM reports `entry function not found`. Add a small zero-argument debug entry that calls the business function you want to inspect:

```c
int proto_cs_activate_hero(int hero_id)
{
    return "module/hero/main"->activate_hero(hero_id);
}

int main()
{
    return proto_cs_activate_hero(1);
}
```

Or set `entryModule` and `entryFunction` in `launch.json`:

```json
{
  "type": "lpc",
  "request": "launch",
  "name": "Debug hero proto",
  "program": "${workspaceFolder}/base/module/hero/proto.lpc",
  "entryModule": "base/module/hero/proto",
  "entryFunction": "debug"
}
```

The VM launch entry is currently called without arguments, so parameterized business functions should be wrapped by a zero-argument `main`, `debug`, or similar helper.

To attach to an already running VM, start the VM with a DAP attach port:

```powershell
build\vm\lpc_vm.exe run benchmarks/runtime/int_loop --bytecode-root bin --dap-listen 4711
```

Then use `LPC: Attach to VM`, or add this launch configuration:

```json
{
  "type": "lpc",
  "request": "attach",
  "name": "Attach to LPC VM",
  "host": "127.0.0.1",
  "port": 4711,
  "stopOnAttach": false
}
```

The current VM debugger still accepts text commands for run control. The JSONL protocol is documented in `docs/DEBUG_PROTOCOL_JSONL.md`.

Example `settings.json`, `launch.json`, and `tasks.json` files are available in `examples/`.

## Package And Install

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_vscode_plugin.ps1
code --install-extension .\dist\lpc-language-tools-0.1.0.vsix
```

More details are in `docs/VSCODE_EXTENSION_PACKAGING.md`.
