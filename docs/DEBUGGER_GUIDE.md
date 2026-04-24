# LPC Debugger Guide

This guide describes the VM debugger command set and script mode.

## Start Debugger

- Interactive:

```bash
./build/vm/lpc_vm debug
```

- With explicit entry module:

```bash
./build/vm/lpc_vm debug my/module/path
```

- With command script:

```bash
./build/vm/lpc_vm debug --script vm/tests/debugger_script.txt
```

## Script Mode

- In debugger prompt, load script:

```text
source vm/tests/debugger_script.txt
```

- Script line rules:
  - Empty line: ignored
  - `#` prefix: comment

- Environment options:
  - `LPC_DEBUG_SCRIPT=<path>`: preload script on debugger startup
  - `LPC_DEBUG_SCRIPT_STRICT=1`: abort script execution on unknown command

## Core Commands

- Run control:
  - `s`/`step`, `si`/`stepi`
  - `n`/`next`, `ni`/`nexti`
  - `c`/`continue`, `r`/`run`
  - `finish`/`out`

- Stack and frame:
  - `bt`/`backtrace`
  - `frame <n>`
  - `up [n]`, `down [n]`
  - `where`, `info frame`

- Source:
  - `list [line]`
  - `info source`

- Breakpoints:
  - `b <line>` / `b <file>:<line>`
  - `b <spec> if <expr>` (`hit==N`, `hit%N==K`)
  - `tb <spec>`
  - `until <line>` / `advance <line>`
  - `disable <id>`, `enable <id>`
  - `condition <id> <expr>`
  - `ignore <id> <count>`
  - `commands <id>` ... `end`
  - `info break`, `info break csv`

- Watch and display:
  - `watch <expr>`
  - `condition watch <id> <expr>`
  - `ignore watch <id> <n>`
  - `disable watch <id>`, `enable watch <id>`
  - `unwatch <id>`
  - `info watch`, `info watch csv`
  - `display <expr>`, `undisplay <id>`
  - `disable display <id>`, `enable display <id>`
  - `info display`

- Inspection:
  - `args`, `locals`, `upvalues`
  - `p <name-or-index>` / `x <name-or-index>` / `print <...>`

## Expression Notes

- Watch/display/print expressions support:
  - Local variable name (for example `hp`)
  - Numeric local index (for example `0`)
  - Argument shorthand (`arg0`, `arg1`, ...)
  - Local shorthand (`local0`, `local1`, ...)
  - One-level object field (`obj.field` or `obj.3`)
