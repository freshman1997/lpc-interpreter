"use strict";

const vscode = require("vscode");
const path = require("path");
const { LpcDebugAdapter } = require("./debugAdapter");
const { LpcTaskProvider } = require("./taskProvider");
const { activeLpcFile, configFor, compileFile, runVm, applyLpcEditorSettings } = require("./utils");
const { startLsp, stopLsp } = require("./lspClient");

function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection("lpc");
  context.subscriptions.push(diagnostics);

  context.subscriptions.push(vscode.window.onDidChangeActiveTextEditor((editor) => {
    if (editor && editor.document.languageId === "lpc") {
      applyLpcEditorSettings(editor);
    }
  }));
  if (vscode.window.activeTextEditor && vscode.window.activeTextEditor.document.languageId === "lpc") {
    applyLpcEditorSettings(vscode.window.activeTextEditor);
  }

  context.subscriptions.push(vscode.commands.registerCommand("lpc.compileCurrent", async () => {
    const file = activeLpcFile();
    if (!file) {
      vscode.window.showWarningMessage("Please open an LPC file first.");
      return;
    }
    const result = await compileFile(file, diagnostics);
    if (result.code === 0) {
      vscode.window.showInformationMessage("LPC compilation succeeded.");
    }
  }));

  context.subscriptions.push(vscode.commands.registerCommand("lpc.runCurrent", async () => {
    const file = activeLpcFile();
    if (!file) {
      vscode.window.showWarningMessage("Please open an LPC file first.");
      return;
    }
    const compiled = await compileFile(file, diagnostics);
    if (compiled.code !== 0) {
      return;
    }
    runVm(file);
  }));

  context.subscriptions.push(vscode.commands.registerCommand("lpc.debugCurrent", async () => {
    const file = activeLpcFile();
    if (!file) {
      vscode.window.showWarningMessage("Please open an LPC file first.");
      return;
    }
    const compiled = await compileFile(file, diagnostics);
    if (compiled.code !== 0) {
      return;
    }
    vscode.debug.startDebugging(vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file)), {
      type: "lpc",
      request: "launch",
      name: "Debug LPC file",
      program: file,
      stopOnEntry: true
    });
  }));

  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory("lpc", {
    createDebugAdapterDescriptor(session) {
      const cfg = configFor(session.configuration.program);
      const args = ["debug", "--protocol", "dap", "--entry-file", path.join(cfg.outRoot, "entry.txt")];
      if (session.configuration.entryModule) {
        args.splice(1, 0, session.configuration.entryModule);
      }
      return new vscode.DebugAdapterExecutable(cfg.vmPath, args, { cwd: cfg.workspace });
    }
  }));

  context.subscriptions.push(vscode.tasks.registerTaskProvider("lpc", new LpcTaskProvider()));

  startLsp(context);

  context.subscriptions.push(vscode.commands.registerCommand("lpc.restartLsp", async () => {
    await stopLsp();
    startLsp(context);
    vscode.window.showInformationMessage("LPC Language Server restarted.");
  }));
}

function deactivate() {
  return stopLsp();
}

module.exports = {
  activate,
  deactivate
};
