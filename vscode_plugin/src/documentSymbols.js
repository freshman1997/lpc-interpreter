"use strict";

const vscode = require("vscode");
const { parseDocumentSymbolsFull } = require("./symbolParser");
const { isLspActive } = require("./lspClient");

function createDocumentSymbolProvider() {
  return vscode.languages.registerDocumentSymbolProvider(
    { scheme: "file", language: "lpc" },
    {
      provideDocumentSymbols(document, token) {
        if (isLspActive()) return [];
        return parseDocumentSymbolsFull(document);
      }
    }
  );
}

module.exports = { createDocumentSymbolProvider };
