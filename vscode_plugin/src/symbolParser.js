"use strict";

function parseDocumentSymbols(document) {
  const symbols = [];
  const text = document.getText();
  const lines = text.split("\n");

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];

    let m = line.match(/^\s*(?:(void|int|float|string|object|mapping|mixed|function)\s+)?fun\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/);
    if (m) {
      symbols.push({
        name: m[2],
        kind: "function",
        detail: `${m[1] || "void"} ${m[2]}(${m[3].trim()})`,
        line: i,
        params: m[3].trim(),
        retType: m[1] || "void",
      });
      continue;
    }

    m = line.match(/^\s*class\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{?/);
    if (m) {
      symbols.push({
        name: m[1],
        kind: "class",
        detail: `class ${m[1]}`,
        line: i,
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

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];

    let m = line.match(/^\s*(?:(void|int|float|string|object|mapping|mixed|function)\s+)?fun\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/);
    if (m) {
      const retType = m[1] || "void";
      const name = m[2];
      const params = m[3].trim();
      const range = new vscode.Range(i, 0, i, line.length);
      symbols.push(new vscode.DocumentSymbol(
        name,
        `${retType} ${name}(${params})`,
        vscode.SymbolKind.Function,
        range, range
      ));
      continue;
    }

    m = line.match(/^\s*class\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{?/);
    if (m) {
      const name = m[1];
      const startLine = i;
      let endLine = i;
      let depth = 0;
      for (let j = i; j < lines.length; j++) {
        for (const ch of lines[j]) {
          if (ch === "{") depth++;
          else if (ch === "}") depth--;
        }
        if (depth <= 0 && j > i) {
          endLine = j;
          break;
        }
      }
      const range = new vscode.Range(startLine, 0, endLine, lines[endLine].length);
      const selRange = new vscode.Range(startLine, lines[startLine].indexOf("class"), startLine, lines[startLine].indexOf("class") + 5);
      const classSymbol = new vscode.DocumentSymbol(
        name, `class ${name}`, vscode.SymbolKind.Class, range, selRange
      );

      for (let j = startLine + 1; j <= endLine; j++) {
        const fieldMatch = lines[j].match(/^\s*(?:(int|float|string|object|mapping|mixed|function)\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*;/);
        if (fieldMatch) {
          const fType = fieldMatch[1] || "mixed";
          const fName = fieldMatch[2];
          const fRange = new vscode.Range(j, 0, j, lines[j].length);
          classSymbol.children.push(new vscode.DocumentSymbol(
            fName, `${fType} ${fName}`, vscode.SymbolKind.Field, fRange, fRange
          ));
        }
      }

      symbols.push(classSymbol);
      i = endLine;
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
  }

  return symbols;
}

module.exports = { parseDocumentSymbols, parseDocumentSymbolsFull };
