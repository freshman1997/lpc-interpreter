"use strict";

const TYPE_PATTERN = "(?:void|int|float|string|object|mapping|mixed|function|buffer)\\s*\\*?";
const MODIFIER_PATTERN = "(?:(?:static|public|private|protected|nomask|varargs)\\s+)*";

function parseDocumentSymbols(document) {
  const symbols = [];
  const text = document.getText();
  const lines = text.split("\n");

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];

    let m = line.match(new RegExp(
      "^\\s*" + MODIFIER_PATTERN + "(?:" + TYPE_PATTERN + ")?\\s+fun\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)"
    ));
    if (m) {
      const typeMatch = line.match(new RegExp(
        "^\\s*" + MODIFIER_PATTERN + "((" + TYPE_PATTERN + "))\\s+fun\\s+"
      ));
      symbols.push({
        name: m[1],
        kind: "function",
        detail: `${typeMatch ? typeMatch[1].trim() : "void"} ${m[1]}(${m[2].trim()})`,
        line: i,
        params: m[2].trim(),
        retType: typeMatch ? typeMatch[1].trim() : "void",
      });
      continue;
    }

    m = line.match(new RegExp(
      "^\\s*" + MODIFIER_PATTERN + "((" + TYPE_PATTERN + "))\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\("
    ));
    if (m && !/^\s*(if|else|for|foreach|while|do|switch|catch|return)\b/.test(line)) {
      symbols.push({
        name: m[3],
        kind: "function",
        detail: `${m[1].trim()} ${m[3]}(...)`,
        line: i,
        params: "",
        retType: m[1].trim(),
      });
      continue;
    }

    m = line.match(/^\s*class\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*:\s*([A-Za-z_][A-Za-z0-9_]*))?\s*\{?/);
    if (m) {
      const entry = {
        name: m[1],
        kind: "class",
        detail: m[2] ? `class ${m[1]} : ${m[2]}` : `class ${m[1]}`,
        line: i,
        baseClass: m[2] || undefined,
      };
      symbols.push(entry);
      continue;
    }

    m = line.match(/^\s*inherit\s+([A-Za-z_][A-Za-z0-9_\/]*|"[^"]*")\s*;/);
    if (m) {
      symbols.push({
        name: m[1].replace(/"/g, ""),
        kind: "inherit",
        detail: `inherit ${m[1]}`,
        line: i,
      });
      continue;
    }

    m = line.match(new RegExp("^\\s*" + MODIFIER_PATTERN + "((" + TYPE_PATTERN + "))\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*(?:=[^;]*)?;"));
    if (m) {
      symbols.push({
        name: m[3],
        kind: "variable",
        detail: `${m[1].trim()} ${m[3]}`,
        line: i,
        varType: m[1].trim(),
      });
      continue;
    }

    m = line.match(/^\s*var\s+([A-Za-z_][A-Za-z0-9_]*)/);
    if (m) {
      symbols.push({
        name: m[1],
        kind: "variable",
        detail: `var ${m[1]}`,
        line: i,
      });
    }
  }

  return symbols;
}

function parseDocumentSymbolsFull(document) {
  const vscode = require("vscode");
  const symbols = [];
  const text = document.getText();
  const lines = text.split("\n");

  let i = 0;
  while (i < lines.length) {
    const line = lines[i];

    let m = line.match(new RegExp(
      "^\\s*" + MODIFIER_PATTERN + "(?:" + TYPE_PATTERN + ")?\\s+fun\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\(([^)]*)\\)"
    ));
    if (m) {
      const typeMatch = line.match(new RegExp(
        "^\\s*" + MODIFIER_PATTERN + "((" + TYPE_PATTERN + "))\\s+fun\\s+"
      ));
      const retType = typeMatch ? typeMatch[1].trim() : "void";
      const name = m[1];
      const params = m[2].trim();
      const range = new vscode.Range(i, 0, i, line.length);
      symbols.push(new vscode.DocumentSymbol(
        name,
        `${retType} ${name}(${params})`,
        vscode.SymbolKind.Function,
        range, range
      ));
      i++;
      continue;
    }

    m = line.match(new RegExp(
      "^\\s*" + MODIFIER_PATTERN + "((" + TYPE_PATTERN + "))\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\("
    ));
    if (m && !/^\s*(if|else|for|foreach|while|do|switch|catch|return)\b/.test(line)) {
      const retType = m[1].trim();
      const name = m[3];
      const range = new vscode.Range(i, 0, i, line.length);
      symbols.push(new vscode.DocumentSymbol(
        name,
        `${retType} ${name}(...)`,
        vscode.SymbolKind.Function,
        range, range
      ));
      i++;
      continue;
    }

    m = line.match(/^\s*class\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*:\s*([A-Za-z_][A-Za-z0-9_]*))?\s*\{?/);
    if (m) {
      const name = m[1];
      const baseClass = m[2];
      const startLine = i;
      let endLine = i;
      let depth = 0;
      let started = false;
      for (let j = i; j < lines.length; j++) {
        for (const ch of lines[j]) {
          if (ch === "{") { depth++; started = true; }
          else if (ch === "}") depth--;
        }
        if (started && depth <= 0) {
          endLine = j;
          break;
        }
      }
      const range = new vscode.Range(startLine, 0, endLine, lines[endLine].length);
      const classKwIdx = lines[startLine].indexOf("class");
      const selRange = new vscode.Range(startLine, classKwIdx, startLine, classKwIdx + 5);
      const classSymbol = new vscode.DocumentSymbol(
        name,
        baseClass ? `class ${name} : ${baseClass}` : `class ${name}`,
        vscode.SymbolKind.Class, range, selRange
      );

      for (let j = startLine + 1; j <= endLine; j++) {
        const fieldMatch = lines[j].match(new RegExp(
          "^\\s*(?:" + TYPE_PATTERN + "|var)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*(?:=[^;]*)?\\s*;"
        ));
        if (fieldMatch) {
          const fTypeMatch = lines[j].match(new RegExp("^\\s*((" + TYPE_PATTERN + "))\\s+"));
          const fType = fTypeMatch ? fTypeMatch[1].trim() : "mixed";
          const fName = fieldMatch[1];
          const fRange = new vscode.Range(j, 0, j, lines[j].length);
          classSymbol.children.push(new vscode.DocumentSymbol(
            fName, `${fType} ${fName}`, vscode.SymbolKind.Field, fRange, fRange
          ));
        }
      }

      symbols.push(classSymbol);
      i = endLine + 1;
      continue;
    }

    m = line.match(/^\s*inherit\s+([A-Za-z_][A-Za-z0-9_\/]*|"[^"]*")\s*;/);
    if (m) {
      const path = m[1].replace(/"/g, "");
      const range = new vscode.Range(i, 0, i, line.length);
      symbols.push(new vscode.DocumentSymbol(
        path, `inherit ${m[1]}`, vscode.SymbolKind.Module, range, range
      ));
      i++;
      continue;
    }

    m = line.match(new RegExp("^\\s*" + MODIFIER_PATTERN + "((" + TYPE_PATTERN + "))\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*(?:=[^;]*)?;"));
    if (m) {
      const vType = m[1].trim();
      const name = m[3];
      const range = new vscode.Range(i, 0, i, line.length);
      symbols.push(new vscode.DocumentSymbol(
        name, `${vType} ${name}`, vscode.SymbolKind.Variable, range, range
      ));
      i++;
      continue;
    }

    m = line.match(/^\s*var\s+([A-Za-z_][A-Za-z0-9_]*)/);
    if (m) {
      const name = m[1];
      const range = new vscode.Range(i, 0, i, line.length);
      symbols.push(new vscode.DocumentSymbol(
        name, `var ${name}`, vscode.SymbolKind.Variable, range, range
      ));
    }
    i++;
  }

  return symbols;
}

module.exports = { parseDocumentSymbols, parseDocumentSymbolsFull };
