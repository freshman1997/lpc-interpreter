"use strict";

const vscode = require("vscode");
const path = require("path");
const { buildCompileArgs, configFor, workspaceFolderFor, activeLpcFile } = require("./utils");

class LpcTaskProvider {
  provideTasks() {
    const file = activeLpcFile();
    if (!file) {
      return [];
    }
    return [
      this.makeTask("compile", "LPC: Compile Current File", file),
      this.makeTask("run", "LPC: Run Current File", file)
    ];
  }

  resolveTask(task) {
    const program = task.definition.program || activeLpcFile();
    if (!program) {
      return undefined;
    }
    return this.makeTask(task.definition.command || "compile", task.name, program);
  }

  makeTask(command, name, file) {
    const cfg = configFor(file);
    const definition = { type: "lpc", command, program: file };
    const commandLine = command === "run"
      ? `${quoteShell(cfg.vmPath)} run --entry-file ${quoteShell(path.join(cfg.outRoot, "entry.txt"))} --vm ${quoteShell(cfg.vmEngine)}`
      : `${quoteShell(cfg.compilerPath)} ${buildCompileArgs(file, cfg).map(quoteShell).join(" ")}`;
    const execution = new vscode.ShellExecution(commandLine, { cwd: cfg.workspace });
    const task = new vscode.Task(definition, vscode.TaskScope.Workspace, name, "lpc", execution, command === "compile" ? "$lpc-compiler" : []);
    task.group = command === "compile" ? vscode.TaskGroup.Build : vscode.TaskGroup.Test;
    return task;
  }
}

function quoteShell(value) {
  const text = String(value);
  if (!/[\s"]/u.test(text)) {
    return text;
  }
  return `"${text.replace(/"/g, '\\"')}"`;
}

module.exports = { LpcTaskProvider };
