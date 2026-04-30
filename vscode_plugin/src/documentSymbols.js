"use strict";

const vscode = require("vscode");
const { parseDocumentSymbolsFull } = require("./symbolParser");

function createDocumentSymbolProvider() {
  return vscode.languages.registerDocumentSymbolProvider(
    { scheme: "file", language: "lpc" },
    {
      provideDocumentSymbols(document, token) {
        return parseDocumentSymbolsFull(document);
      }
    }
  );
}

module.exports = { createDocumentSymbolProvider };
