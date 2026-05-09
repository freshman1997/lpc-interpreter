"use strict";

const vscode = require("vscode");
const path = require("path");
const { EFUN_DB, LPC_KEYWORDS, LPC_TYPES } = require("./efunDb");
const { parseDocumentSymbols } = require("./symbolParser");
const { isLspActive } = require("./lspClient");

function createHoverProvider(workspaceIndex) {
  return vscode.languages.registerHoverProvider(
    { scheme: "file", language: "lpc" },
    {
      provideHover(document, position, token) {
        const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
        if (!range) return null;
        const word = document.getText(range);

        const efunHover = tryEfunHover(word);
        if (efunHover) return new vscode.Hover(efunHover, range);

        if (isLspActive()) return null;

        const typeHover = tryTypeHover(word);
        if (typeHover) return new vscode.Hover(typeHover, range);

        const kwHover = tryKeywordHover(word);
        if (kwHover) return new vscode.Hover(kwHover, range);

        const docHover = tryDocumentSymbolHover(word, document);
        if (docHover) return new vscode.Hover(docHover, range);

        const crossHover = tryCrossFileHover(word, document, workspaceIndex);
        if (crossHover) return new vscode.Hover(crossHover, range);

        return null;
      }
    }
  );
}

function tryEfunHover(word) {
  const info = EFUN_DB[word];
  if (!info) return null;
  const md = new vscode.MarkdownString();
  md.appendCodeblock(`${info.ret} ${word}(${info.args.join(", ")})`, "lpc");
  md.appendMarkdown("\n\n" + info.doc);
  if (info.ret === "void") {
    md.appendMarkdown("\n\n*Returns void — do not use as a value.*");
  }
  return md;
}

function tryTypeHover(word) {
  if (!LPC_TYPES.includes(word)) return null;
  const md = new vscode.MarkdownString();
  md.appendCodeblock(`type: ${word}`, "lpc");
  const typeDocs = {
    void: "No return value.",
    int: "64-bit signed integer.",
    float: "64-bit IEEE 754 double.",
    string: "UTF-8 string.",
    object: "Object reference with version-pinned class layout.",
    mapping: "Key-value associative array.",
    mixed: "Any type accepted.",
    function: "Function reference / closure.",
    buffer: "Binary buffer.",
  };
  if (typeDocs[word]) md.appendMarkdown("\n\n" + typeDocs[word]);
  md.appendMarkdown("\n\n*Append `*` for array type (e.g. `string*`).*");
  return md;
}

function tryKeywordHover(word) {
  if (!LPC_KEYWORDS.includes(word)) return null;
  const md = new vscode.MarkdownString();
  md.appendCodeblock(`keyword: ${word}`, "lpc");
  const kwDocs = {
    fun: "Declare a function.",
    var: "Declare a variable with inferred type.",
    class: "Declare a class type. Use `: BaseName` for inheritance.",
    new: "Create a new class instance.",
    inherit: "Inherit from another module.",
    foreach: "Iterate over elements in an array or mapping.",
    in: "Used with `foreach` to specify the collection.",
    catch: "Catch runtime errors in a block.",
    static: "Modifier: function/variable is static (per-class, not per-instance).",
    private: "Modifier: function/variable is private to the declaring class.",
    public: "Modifier: function/variable is publicly accessible.",
    protected: "Modifier: function/variable is accessible within the class and its subclasses.",
    nomask: "Modifier: function cannot be overridden by inheritance.",
    varargs: "Modifier: function accepts variable number of arguments.",
    nil: "Null/empty value.",
  };
  if (kwDocs[word]) md.appendMarkdown("\n\n" + kwDocs[word]);
  return md;
}

function tryDocumentSymbolHover(word, document) {
  const symbols = parseDocumentSymbols(document);
  const sym = symbols.find(s => s.name === word);
  if (!sym) return null;
  const md = new vscode.MarkdownString();
  md.appendCodeblock(sym.detail || sym.name, "lpc");
  if (sym.kind === "function") {
    md.appendMarkdown("\n\n*Defined in this file.*");
  } else if (sym.kind === "class") {
    if (sym.baseClass) {
      md.appendMarkdown(`\n\n*Inherits from \`${sym.baseClass}\`.*`);
    } else {
      md.appendMarkdown("\n\n*Defined in this file.*");
    }
  } else if (sym.kind === "inherit") {
    md.appendMarkdown("\n\n*Inherited module.*");
  }
  return md;
}

function tryCrossFileHover(word, document, workspaceIndex) {
  if (!workspaceIndex) return null;
  const results = workspaceIndex.lookup(word);
  const localNames = new Set(parseDocumentSymbols(document).map(s => s.name));
  const cross = results.filter(r => !localNames.has(r.name) || r.uri !== document.uri.toString());
  if (cross.length === 0) return null;
  const md = new vscode.MarkdownString();
  const first = cross[0];
  md.appendCodeblock(first.detail || first.name, "lpc");
  const fileName = path.basename(vscode.Uri.parse(first.uri).fsPath);
  md.appendMarkdown(`\n\n*Defined in \`${fileName}\`*`);
  if (cross.length > 1) {
    md.appendMarkdown(`\n\n*${cross.length - 1} more definition(s) in other files.*`);
  }
  return md;
}

module.exports = { createHoverProvider };
