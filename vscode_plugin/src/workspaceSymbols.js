"use strict";

const vscode = require("vscode");
const path = require("path");
const { isLspActive } = require("./lspClient");

function createWorkspaceSymbolProvider(workspaceIndex) {
  return vscode.languages.registerWorkspaceSymbolProvider({
    provideWorkspaceSymbols(query, token) {
      if (isLspActive()) return [];
      if (!query || query.length === 0) return [];
      const results = workspaceIndex.lookupPrefix(query);
      return results.map(sym => {
        const uri = vscode.Uri.parse(sym.uri);
        const fileName = path.basename(uri.fsPath);
        const kind = sym.kind === "function"
          ? vscode.SymbolKind.Function
          : sym.kind === "class"
            ? vscode.SymbolKind.Class
            : sym.kind === "inherit"
              ? vscode.SymbolKind.Module
              : vscode.SymbolKind.Variable;
        return new vscode.SymbolInformation(
          sym.name,
          kind,
          sym.kind === "function" || sym.kind === "variable"
            ? new vscode.Location(uri, new vscode.Position(sym.line, 0))
            : undefined,
          fileName
        );
      });
    }
  });
}

module.exports = { createWorkspaceSymbolProvider };
