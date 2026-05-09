"use strict";

const vscode = require("vscode");
const { EFUN_DB } = require("./efunDb");
const { parseDocumentSymbols } = require("./symbolParser");
const { isLspActive } = require("./lspClient");

function createSignatureHelpProvider(workspaceIndex) {
  return vscode.languages.registerSignatureHelpProvider(
    { scheme: "file", language: "lpc" },
    {
      provideSignatureHelp(document, position, token, context) {
        const line = document.lineAt(position.line).text;
        const prefix = line.slice(0, position.character);

        const callInfo = findEnclosingCall(prefix);
        if (!callInfo) return null;

        const efunHelp = tryEfunSignature(callInfo);
        if (efunHelp) return efunHelp;

        if (isLspActive()) return null;

        const docHelp = tryDocumentFunctionSignature(callInfo, document);
        if (docHelp) return docHelp;

        const crossHelp = tryCrossFileFunctionSignature(callInfo, document, workspaceIndex);
        if (crossHelp) return crossHelp;

        return null;
      }
    },
    "(", ","
  );
}

function tryEfunSignature(callInfo) {
  const info = EFUN_DB[callInfo.funcName];
  if (!info) return null;
  return buildSignatureHelp(callInfo, info.args, info.ret, info.doc);
}

function tryDocumentFunctionSignature(callInfo, document) {
  const symbols = parseDocumentSymbols(document);
  const sym = symbols.find(s => s.name === callInfo.funcName && s.kind === "function");
  if (!sym) return null;
  const args = splitParams(sym.params);
  return buildSignatureHelp(callInfo, args, sym.retType, undefined);
}

function tryCrossFileFunctionSignature(callInfo, document, workspaceIndex) {
  if (!workspaceIndex) return null;
  const results = workspaceIndex.lookup(callInfo.funcName);
  const localNames = new Set(parseDocumentSymbols(document).map(s => s.name));
  const cross = results.find(r => r.kind === "function" && (r.uri !== document.uri.toString() || !localNames.has(r.name)));
  if (!cross) return null;
  const args = splitParams(cross.params || "");
  return buildSignatureHelp(callInfo, args, cross.retType || "void", undefined);
}

function buildSignatureHelp(callInfo, args, retType, doc) {
  const sig = new vscode.SignatureInformation(
    `${retType} ${callInfo.funcName}(${args.join(", ")})`,
    doc ? new vscode.MarkdownString(doc) : undefined
  );
  sig.parameters = args.map(a => {
    const parts = a.split(" ");
    const label = a;
    const paramName = parts.length > 1 ? parts[parts.length - 1] : a;
    const paramType = parts.length > 1 ? parts[0] : "mixed";
    return new vscode.ParameterInformation(label, new vscode.MarkdownString(`**${paramType}** — \`${paramName}\``));
  });
  sig.activeParameter = Math.min(callInfo.commaCount, sig.parameters.length - 1);

  const help = new vscode.SignatureHelp();
  help.signatures = [sig];
  help.activeSignature = 0;
  help.activeParameter = sig.activeParameter;
  return help;
}

function splitParams(paramsStr) {
  if (!paramsStr || paramsStr.trim() === "") return [];
  return paramsStr.split(",").map(p => p.trim()).filter(p => p.length > 0);
}

function findEnclosingCall(prefix) {
  let depth = 0;
  let i = prefix.length - 1;
  while (i >= 0) {
    if (prefix[i] === ")") depth++;
    else if (prefix[i] === "(") {
      depth--;
      if (depth < 0) {
        const beforeParen = prefix.slice(0, i);
        const m = beforeParen.match(/([A-Za-z_][A-Za-z0-9_]*)\s*$/);
        if (m) {
          const funcName = m[1];
          const argsText = prefix.slice(i + 1);
          const commaCount = countTopLevelCommas(argsText);
          return { funcName, commaCount };
        }
        return null;
      }
    }
    i--;
  }
  return null;
}

function countTopLevelCommas(text) {
  let count = 0;
  let depth = 0;
  for (let i = 0; i < text.length; i++) {
    if (text[i] === "(" || text[i] === "[" || text[i] === "{") depth++;
    else if (text[i] === ")" || text[i] === "]" || text[i] === "}") depth--;
    else if (text[i] === "," && depth === 0) count++;
  }
  return count;
}

module.exports = { createSignatureHelpProvider };
