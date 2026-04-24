# Closure Regression Test Plan

This document defines minimal closure behavior checks for the current refactor stage.

## 1) Counter closure

Goal: verify local capture + mutation survives returns.

Expected behavior:

- `make_counter` returns callable closure.
- each call increments and returns captured `n`.

## 2) Nested closure propagation

Goal: verify parent lambda captures vars used only by child lambda.

Expected behavior:

- child closure reads parent-scope variable after parent returns.

## 3) Compound assignment on upvalue

Goal: verify `+=` and similar operations go through upvalue load/store path.

Expected behavior:

- captured numeric variable updates correctly over repeated calls.

## 4) Function symbol should not be captured

Goal: avoid over-capture of declared function names.

Expected behavior:

- function call by name inside lambda does not occupy upvalue slot.

## 5) Profile output smoke

Goal: verify profile output has opcode + function sections.

Expected behavior:

- generated `profile.json` contains keys: `opcodes`, `functions`.
