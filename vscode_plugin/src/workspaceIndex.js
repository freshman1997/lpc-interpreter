"use strict";

const vscode = require("vscode");
const { parseDocumentSymbols } = require("./symbolParser");

class WorkspaceSymbolIndex {
  constructor() {
    this._symbols = new Map();
  }

  update(uri, document) {
    if (!document || document.languageId !== "lpc") {
      this._symbols.delete(uri.toString());
      return;
    }
    this._symbols.set(uri.toString(), parseDocumentSymbols(document));
  }

  updateFromText(uri, text) {
    const fakeDoc = { getText: () => text, languageId: "lpc", uri };
    this._symbols.set(uri.toString(), parseDocumentSymbols(fakeDoc));
  }

  remove(uri) {
    this._symbols.delete(uri.toString());
  }

  async scanWorkspaceFiles() {
    const folders = vscode.workspace.workspaceFolders;
    if (!folders || folders.length === 0) return;
    const pattern = new vscode.RelativePattern(folders[0], "**/*.lpc");
    const files = await vscode.workspace.findFiles(pattern);
    for (const fileUri of files) {
      if (this._symbols.has(fileUri.toString())) continue;
      try {
        const bytes = await vscode.workspace.fs.readFile(fileUri);
        const text = new TextDecoder().decode(bytes);
        this.updateFromText(fileUri, text);
      } catch (_) { }
    }
  }

  lookup(name) {
    const results = [];
    for (const [uriStr, symbols] of this._symbols) {
      for (const sym of symbols) {
        if (sym.name === name) {
          results.push({ ...sym, uri: uriStr });
        }
      }
    }
    return results;
  }

  lookupPrefix(prefix) {
    const lower = prefix.toLowerCase();
    const results = [];
    for (const [uriStr, symbols] of this._symbols) {
      for (const sym of symbols) {
        if (sym.name.toLowerCase().startsWith(lower)) {
          results.push({ ...sym, uri: uriStr });
        }
      }
    }
    return results;
  }

  allSymbols() {
    const results = [];
    for (const [uriStr, symbols] of this._symbols) {
      for (const sym of symbols) {
        results.push({ ...sym, uri: uriStr });
      }
    }
    return results;
  }

  symbolsForUri(uri) {
    return this._symbols.get(uri.toString()) || [];
  }

  otherFileSymbols(currentUri) {
    const key = currentUri.toString();
    const results = [];
    for (const [uriStr, symbols] of this._symbols) {
      if (uriStr === key) continue;
      for (const sym of symbols) {
        results.push({ ...sym, uri: uriStr });
      }
    }
    return results;
  }
}

module.exports = { WorkspaceSymbolIndex };
