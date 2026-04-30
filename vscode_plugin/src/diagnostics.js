"use strict";

const vscode = require("vscode");
const path = require("path");

function publishDiagnostics(text, diagnostics) {
  const byFile = new Map();
  const re = /^(.*?):(error|warning|note)\((\d+):(\d+)\):\s+(.*)$/gm;
  let m;
  while ((m = re.exec(text)) !== null) {
    const file = path.resolve(m[1]);
    const line = Math.max(0, Number(m[3]) - 1);
    const column = Math.max(0, Number(m[4]) - 1);
    const range = new vscode.Range(line, column, line, column + 1);
    const severity = m[2] === "error"
      ? vscode.DiagnosticSeverity.Error
      : m[2] === "warning"
        ? vscode.DiagnosticSeverity.Warning
        : vscode.DiagnosticSeverity.Information;
    const item = new vscode.Diagnostic(range, m[5], severity);
    item.source = "lpc";
    if (!byFile.has(file)) {
      byFile.set(file, []);
    }
    byFile.get(file).push(item);
  }
  for (const [file, items] of byFile) {
    diagnostics.set(vscode.Uri.file(file), items);
  }
}

module.exports = { publishDiagnostics };
