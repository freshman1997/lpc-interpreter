"use strict";

const vscode = require("vscode");
const { LpcTaskProvider } = require("./taskProvider");
const { activeLpcFile, buildCompileArgs, buildEnvArgs, compileFile, configFor, moduleNameForFile, runProcess, runVm, applyLpcEditorSettings } = require("./utils");
const { publishDiagnostics } = require("./diagnostics");
const { startLsp, stopLsp } = require("./lspClient");

async function saveDocumentForPath(file) {
  const doc = vscode.workspace.textDocuments.find((item) => item.uri.fsPath === file);
  if (doc && doc.isDirty) {
    await doc.save();
  }
}

function currentDebugProgram(program) {
  if (!program || program === "${file}") {
    return activeLpcFile();
  }
  return program;
}

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

  vscode.debug.registerDebugAdapterTrackerFactory("*", {
    createDebugAdapterTracker(session) {
      if (session.type === "lpc") {
        return {
          onWillStartSession() {
            console.log("[LPC Debug] Session starting");
          },
          onWillStopSession() {
            console.log("[LPC Debug] Session stopping");
          },
          onError(error) {
            console.log("[LPC Debug] Error:", error);
          }
        };
      }
      return undefined;
    }
  });

  context.subscriptions.push(vscode.debug.onDidStartDebugSession((session) => {
    if (session.type === "lpc") {
      vscode.commands.executeCommand("workbench.view.debug");
    }
  }));

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
    await saveDocumentForPath(file);
    await vscode.commands.executeCommand("workbench.view.debug");
    const started = await vscode.debug.startDebugging(vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file)), {
      type: "lpc",
      request: "launch",
      name: "Debug LPC file",
      program: file,
      stopOnEntry: false
    });
    if (!started) {
      vscode.window.showErrorMessage("LPC debug session did not start.");
    }
  }));

  context.subscriptions.push(vscode.commands.registerCommand("lpc.attach", async () => {
    const portText = await vscode.window.showInputBox({
      title: "Attach to LPC VM",
      prompt: "Port from lpc_vm run --dap-listen",
      value: "4711",
      validateInput(value) {
        const port = Number(value);
        return Number.isInteger(port) && port > 0 && port <= 65535 ? undefined : "Enter a TCP port from 1 to 65535.";
      }
    });
    if (!portText) {
      return;
    }
    const folder = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0];
    const started = await vscode.debug.startDebugging(folder, {
      type: "lpc",
      request: "attach",
      name: "Attach to LPC VM",
      host: "127.0.0.1",
      port: Number(portText),
      stopOnAttach: false
    });
    if (!started) {
      vscode.window.showErrorMessage("LPC attach session did not start.");
    }
  }));

  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory("lpc", {
    async createDebugAdapterDescriptor(session) {
      if (session.configuration.request === "attach") {
        const port = Number(session.configuration.port || 4711);
        const host = session.configuration.host || "127.0.0.1";
        return new vscode.DebugAdapterServer(port, host);
      }

      const program = currentDebugProgram(session.configuration.program);
      if (!program) {
        vscode.window.showWarningMessage("Please open an LPC file first.");
        return undefined;
      }
      session.configuration.program = program;
      await saveDocumentForPath(program);

      const cfg = configFor(program);
      const compiled = await runProcess(cfg.compilerPath, buildCompileArgs(program, cfg), cfg.workspace, "LPC Compiler", { show: false });
      publishDiagnostics(compiled.stdout + compiled.stderr, diagnostics);
      if (compiled.code !== 0) {
        vscode.commands.executeCommand("workbench.action.output.toggleOutput");
        vscode.window.showErrorMessage(`LPC compilation failed (exit code ${compiled.code}).`);
        return undefined;
      }

      const entryModule = session.configuration.entryModule || moduleNameForFile(program, cfg);
      const entryFunction = session.configuration.entryFunction || "main";
      const env = session.configuration.env || cfg.env;
      const args = ["debug", "--protocol", "dap", "--module", entryModule, "--function", entryFunction, "--bytecode-root", cfg.outRoot];
      args.splice(args.length - 2, 0, ...buildEnvArgs(env));
      return new vscode.DebugAdapterExecutable(cfg.vmPath, args, { cwd: cfg.workspace });
    }
  }));

  context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider("lpc", {
    provideDebugConfigurations(folder) {
      return [{
        type: "lpc",
        request: "launch",
        name: "Debug LPC file",
        program: "${file}",
        stopOnEntry: false
      }, {
        type: "lpc",
        request: "attach",
        name: "Attach to LPC VM",
        host: "127.0.0.1",
        port: 4711,
        stopOnAttach: false
      }];
    },
    resolveDebugConfiguration(folder, config) {
      if (config.request === "attach") {
        config.host = config.host || "127.0.0.1";
        config.port = Number(config.port || 4711);
        return config;
      }

      if (!config.type && !config.request && !config.name) {
        return {
          type: "lpc",
          request: "launch",
          name: "Debug LPC file",
          program: currentDebugProgram("${file}") || "${file}",
          stopOnEntry: false
        };
      }
      const program = currentDebugProgram(config.program);
      if (program) {
        config.program = program;
      }
      return config;
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
