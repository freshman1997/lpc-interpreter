"use strict";

const vscode = require("vscode");
const path = require("path");
const fs = require("fs");
const cp = require("child_process");
const { publishDiagnostics } = require("./diagnostics");

function activeLpcFile() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "lpc") {
    return undefined;
  }
  return editor.document.uri.fsPath;
}

function workspaceFolderFor(file) {
  const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file));
  return folder ? folder.uri.fsPath : path.dirname(file);
}

function configFor(file) {
  const cfg = vscode.workspace.getConfiguration("lpc", vscode.Uri.file(file));
  const workspace = workspaceFolderFor(file);
  const expand = (value) => String(value || "")
    .replace(/\$\{workspaceFolder\}/g, workspace)
    .replace(/\$\{file\}/g, file);
  return {
    workspace,
    compilerPath: resolveToolPath(expand(cfg.get("compilerPath")), "lpc_compiler"),
    vmPath: resolveToolPath(expand(cfg.get("vmPath")), "lpc_vm"),
    outRoot: expand(cfg.get("outRoot")),
    includeDirs: (cfg.get("includeDirs") || []).map(expand),
    env: cfg.get("env") || {}
  };
}

function buildCompileArgs(file, cfg) {
  const args = [
    file,
    "--workspace-root", cfg.workspace,
    "--out-root", cfg.outRoot
  ];
  for (const dir of cfg.includeDirs) {
    args.push("-I", dir);
  }
  return args;
}

function moduleNameForFile(file, cfg) {
  const rel = path.relative(cfg.workspace, file);
  return rel.replace(path.extname(rel), "").split(path.sep).join("/");
}

function buildEnvArgs(env) {
  const args = [];
  for (const [key, value] of Object.entries(env || {})) {
    args.push("--env", `${key}=${String(value)}`);
  }
  return args;
}

function compileFile(file, diagnostics) {
  const cfg = configFor(file);
  diagnostics.clear();
  return runProcess(cfg.compilerPath, buildCompileArgs(file, cfg), cfg.workspace, "LPC Compiler")
    .then((result) => {
      publishDiagnostics(result.stdout + result.stderr, diagnostics);
      if (result.code !== 0) {
        vscode.window.showErrorMessage(`LPC compilation failed (exit code ${result.code}).`);
      }
      return result;
    });
}

function runVm(file) {
  const cfg = configFor(file);
  const terminal = vscode.window.createTerminal("LPC VM");
  const moduleName = moduleNameForFile(file, cfg);
  const quoted = (value) => `"${String(value).replace(/"/g, '\\"')}"`;
  const envArgs = buildEnvArgs(cfg.env).map(quoted).join(" ");
  terminal.show();
  terminal.sendText(`${quoted(cfg.vmPath)} run --module ${quoted(moduleName)} --function main ${envArgs} --bytecode-root ${quoted(cfg.outRoot)}`);
}

function runProcess(command, args, cwd, channelName, options) {
  return new Promise((resolve) => {
    const showOutput = !options || options.show !== false;
    const output = vscode.window.createOutputChannel(channelName);
    if (showOutput) {
      output.show(true);
    }
    output.appendLine(`> ${command} ${args.join(" ")}`);

    const child = cp.spawn(command, args, { cwd, shell: false });
    let stdout = "";
    let stderr = "";

    child.stdout.on("data", (data) => {
      const text = data.toString();
      stdout += text;
      output.append(text);
    });
    child.stderr.on("data", (data) => {
      const text = data.toString();
      stderr += text;
      output.append(text);
    });
    child.on("error", (err) => {
      stderr += err.message;
      output.appendLine(err.message);
      resolve({ code: 127, stdout, stderr });
    });
    child.on("close", (code) => resolve({ code: code || 0, stdout, stderr }));
  });
}

function resolveToolPath(configured, toolName) {
  const candidates = [configured];
  if (process.platform === "win32" && !configured.toLowerCase().endsWith(".exe")) {
    candidates.push(`${configured}.exe`);
  }
  const dir = path.dirname(configured);
  const base = path.basename(configured);
  const exeBase = process.platform === "win32" && !base.toLowerCase().endsWith(".exe") ? `${base}.exe` : base;
  candidates.push(path.join(dir, "Debug", exeBase));
  candidates.push(path.join(dir, "Release", exeBase));
  candidates.push(path.join(dir, "RelWithDebInfo", exeBase));
  for (const candidate of candidates) {
    if (candidate && fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return candidates[0];
}

function applyLpcEditorSettings(editor) {
  const cfg = vscode.workspace.getConfiguration("lpc", editor.document.uri);
  const tabSize = cfg.get("format.tabSize");
  const insertSpaces = cfg.get("format.insertSpaces");
  editor.options = {
    tabSize: tabSize,
    insertSpaces: insertSpaces
  };
}

module.exports = {
  activeLpcFile,
  workspaceFolderFor,
  configFor,
  moduleNameForFile,
  buildEnvArgs,
  buildCompileArgs,
  compileFile,
  runVm,
  runProcess,
  resolveToolPath,
  applyLpcEditorSettings,
};
