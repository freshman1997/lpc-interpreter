"use strict";

const vscode = require("vscode");
const { EFUN_DB, EFUN_NAMES, LPC_KEYWORDS, LPC_TYPES } = require("./efunDb");
const { parseDocumentSymbols } = require("./symbolParser");

function createCompletionProvider(workspaceIndex) {
  return vscode.languages.registerCompletionItemProvider(
    { scheme: "file", language: "lpc" },
    {
      provideCompletionItems(document, position, token, context) {
        const line = document.lineAt(position.line).text;
        const prefix = line.slice(0, position.character);
        const items = [];

        const triggerKind = context && context.triggerKind;

        if (triggerKind === vscode.CompletionTriggerKind.TriggerCharacter) {
          if (context.triggerCharacter === ".") {
            return items;
          }
        }

        addEfunCompletions(items);
        addKeywordCompletions(items);
        addTypeCompletions(items);
        addDocumentSymbolCompletions(items, document);
        addCrossFileSymbolCompletions(items, document, workspaceIndex);

        return items;
      }
    },
    ".", " "
  );
}

function addEfunCompletions(items) {
  for (const name of EFUN_NAMES) {
    const info = EFUN_DB[name];
    const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Function);
    item.detail = `${info.ret} ${name}(${info.args.join(", ")})`;
    item.documentation = new vscode.MarkdownString(info.doc);
    item.sortText = `1_${name}`;
    const snippetArgs = info.args.map((a, i) => {
      const paramName = a.split(" ").pop().replace("...", "");
      return `\${${i + 1}:${paramName}}`;
    }).join(", ");
    item.insertText = new vscode.SnippetString(`${name}(${snippetArgs})`);
    items.push(item);
  }
}

function addKeywordCompletions(items) {
  for (const kw of LPC_KEYWORDS) {
    const item = new vscode.CompletionItem(kw, vscode.CompletionItemKind.Keyword);
    item.sortText = `2_${kw}`;
    const isControl = ["if", "else", "for", "foreach", "while", "do", "switch", "case", "default", "break", "continue", "return", "catch"].includes(kw);
    if (isControl) {
      item.insertText = keywordSnippet(kw);
    }
    items.push(item);
  }
}

function keywordSnippet(kw) {
  const snippets = {
    if: new vscode.SnippetString("if (${1:condition}) {\n\t$0\n}"),
    else: new vscode.SnippetString("else {\n\t$0\n}"),
    for: new vscode.SnippetString("for (${1:int i} = ${2:0}; ${3:i} < ${4:n}; ${5:i++}) {\n\t$0\n}"),
    foreach: new vscode.SnippetString("foreach (${1:item} in ${2:items}) {\n\t$0\n}"),
    while: new vscode.SnippetString("while (${1:condition}) {\n\t$0\n}"),
    do: new vscode.SnippetString("do {\n\t$0\n} while (${1:condition});"),
    switch: new vscode.SnippetString("switch (${1:value}) {\n\tcase ${2:0}:\n\t\t$0\n\t\tbreak;\n}"),
    catch: new vscode.SnippetString("catch {\n\t$0\n}"),
    return: new vscode.SnippetString("return $0;"),
    fun: new vscode.SnippetString("fun ${1:name}(${2:params}) {\n\t$0\n}"),
    var: new vscode.SnippetString("var ${1:name} = $0;"),
    class: new vscode.SnippetString("class ${1:Name} {\n\t${2:int} ${3:field};\n}\n$0"),
    new: new vscode.SnippetString("new ${1:ClassName}($0)"),
    inherit: new vscode.SnippetString("inherit ${1:path};$0"),
  };
  return snippets[kw] || undefined;
}

function addTypeCompletions(items) {
  for (const t of LPC_TYPES) {
    const item = new vscode.CompletionItem(t, vscode.CompletionItemKind.Class);
    item.detail = `type: ${t}`;
    item.sortText = `3_${t}`;
    items.push(item);
  }
}

function addDocumentSymbolCompletions(items, document) {
  const symbols = parseDocumentSymbols(document);
  for (const sym of symbols) {
    const kind = sym.kind === "function"
      ? vscode.CompletionItemKind.Function
      : sym.kind === "class"
        ? vscode.CompletionItemKind.Class
        : vscode.CompletionItemKind.Variable;
    const item = new vscode.CompletionItem(sym.name, kind);
    item.detail = sym.detail || sym.name;
    item.sortText = `4_${sym.name}`;
    items.push(item);
  }
}

function addCrossFileSymbolCompletions(items, document, workspaceIndex) {
  if (!workspaceIndex) return;
  const localNames = new Set(parseDocumentSymbols(document).map(s => s.name));
  const crossSymbols = workspaceIndex.otherFileSymbols(document.uri);
  const seen = new Set();
  for (const sym of crossSymbols) {
    if (localNames.has(sym.name) || seen.has(sym.name)) continue;
    seen.add(sym.name);
    const kind = sym.kind === "function"
      ? vscode.CompletionItemKind.Function
      : sym.kind === "class"
        ? vscode.CompletionItemKind.Class
        : vscode.CompletionItemKind.Variable;
    const item = new vscode.CompletionItem(sym.name, kind);
    const fileName = require("path").basename(vscode.Uri.parse(sym.uri).fsPath);
    item.detail = `${sym.detail} — ${fileName}`;
    item.sortText = `5_${sym.name}`;
    items.push(item);
  }
}

module.exports = { createCompletionProvider };
