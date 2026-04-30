"use strict";

const vscode = require("vscode");
const path = require("path");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");
const { resolveToolPath } = require("./utils");

let client = null;

function startLsp(context) {
  const cfg = vscode.workspace.getConfiguration("lpc");
  const lspPathConfig = cfg.get("lspPath") || "${workspaceFolder}/build/lsp/lpc_lsp";
  const workspace = vscode.workspace.workspaceFolders
    ? vscode.workspace.workspaceFolders[0].uri.fsPath
    : "";
  const expand = (value) => String(value || "")
    .replace(/\$\{workspaceFolder\}/g, workspace);
  const lspPath = resolveToolPath(expand(lspPathConfig), "lpc_lsp");

  const serverOptions = {
    run: { command: lspPath, transport: TransportKind.stdio },
    debug: { command: lspPath, transport: TransportKind.stdio }
  };

  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "lpc" }],
    synchronize: {
      configurationSection: "lpc"
    }
  };

  client = new LanguageClient(
    "lpcLsp",
    "LPC Language Server",
    serverOptions,
    clientOptions
  );

  context.subscriptions.push(client.start());

  client.onReady().then(() => {
    vscode.window.setStatusBarMessage("LPC LSP ready", 3000);
  }).catch((err) => {
    vscode.window.showWarningMessage(`LPC LSP failed to start: ${err.message}`);
  });
}

function stopLsp() {
  if (client) {
    return client.stop();
  }
  return Promise.resolve();
}

module.exports = {
  startLsp,
  stopLsp
};
