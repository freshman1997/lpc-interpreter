# LPC Debug JSONL Protocol

The VM debugger can emit a machine-readable JSON-lines stream while preserving the existing text debugger commands.

Enable it with:

```bash
LPC_DEBUG_PROTOCOL=json ./build/vm/lpc_vm debug --entry-file bin/entry.txt
```

On Windows PowerShell:

```powershell
$env:LPC_DEBUG_PROTOCOL='json'
build\vm\lpc_vm.exe debug --entry-file bin\entry.txt
```

## Event Envelope

Every structured line uses this envelope:

```json
{"type":"event","event":"stopped","body":{}}
```

Non-JSON debugger text may still appear on stdout. Protocol clients should parse JSON lines and ignore ordinary text.

## Events

### stopped

Emitted when the debugger reports the current frame.

```json
{
  "type": "event",
  "event": "stopped",
  "body": {
    "reason": "stopped",
    "frames": [
      {
        "id": 1,
        "index": 0,
        "object": "test_bitwise",
        "function": "main",
        "line": 2,
        "pc": 0
      }
    ]
  }
}
```

### variables

Emitted after `locals` or `args`.

```json
{
  "type": "event",
  "event": "variables",
  "body": {
    "scope": "locals",
    "variables": [
      { "index": 0, "name": "x", "value": "1" }
    ]
  }
}
```

### terminated

Emitted when the debugged program exits or the debugger quits.

```json
{"type":"event","event":"terminated","body":{}}
```

### breakpoint

Emitted after breakpoint set or clear commands.

```json
{
  "type": "event",
  "event": "breakpoint",
  "body": {
    "action": "set",
    "file": "test_bitwise",
    "line": 2,
    "verified": true
  }
}
```

### evaluate

Emitted after `p`, `print`, or `x`.

```json
{
  "type": "event",
  "event": "evaluate",
  "body": {
    "expression": "x",
    "success": true,
    "result": "42"
  }
}
```

### runtimeError

Emitted when the VM reports a runtime error while debugging.

```json
{
  "type": "event",
  "event": "runtimeError",
  "body": {
    "message": "stack overflow"
  }
}
```

## Current Scope

This protocol slice covers stopped frames, locals, args, breakpoint command acknowledgements, evaluation results, runtime errors, and termination. Breakpoint management and run-control commands still use the existing text command input (`b`, `clear`, `c`, `n`, `s`, `finish`, `q`).
