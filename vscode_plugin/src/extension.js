"use strict";

const vscode = require("vscode");
const cp = require("child_process");
const path = require("path");
const fs = require("fs");

function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection("lpc");
  context.subscriptions.push(diagnostics);

  context.subscriptions.push(vscode.commands.registerCommand("lpc.compileCurrent", async () => {
    const file = activeLpcFile();
    if (!file) {
      vscode.window.showWarningMessage("Open an LPC file first.");
      return;
    }
    const result = await compileFile(file, diagnostics);
    if (result.code === 0) {
      vscode.window.showInformationMessage("LPC compile succeeded.");
    }
  }));

  context.subscriptions.push(vscode.commands.registerCommand("lpc.runCurrent", async () => {
    const file = activeLpcFile();
    if (!file) {
      vscode.window.showWarningMessage("Open an LPC file first.");
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
      vscode.window.showWarningMessage("Open an LPC file first.");
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
    createDebugAdapterDescriptor() {
      return new vscode.DebugAdapterInlineImplementation(new LpcDebugAdapter(diagnostics));
    }
  }));

  context.subscriptions.push(vscode.tasks.registerTaskProvider("lpc", new LpcTaskProvider()));
}

function deactivate() { }

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
    vmEngine: cfg.get("vmEngine") || "legacy"
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

function compileFile(file, diagnostics) {
  const cfg = configFor(file);
  diagnostics.clear();
  return runProcess(cfg.compilerPath, buildCompileArgs(file, cfg), cfg.workspace, "LPC Compiler")
    .then((result) => {
      publishDiagnostics(result.stdout + result.stderr, diagnostics);
      if (result.code !== 0) {
        vscode.window.showErrorMessage(`LPC compile failed with exit code ${result.code}.`);
      }
      return result;
    });
}

function runVm(file) {
  const cfg = configFor(file);
  const terminal = vscode.window.createTerminal("LPC VM");
  const entryFile = path.join(cfg.outRoot, "entry.txt");
  const quoted = (value) => `"${String(value).replace(/"/g, '\\"')}"`;
  terminal.show();
  terminal.sendText(`${quoted(cfg.vmPath)} run --entry-file ${quoted(entryFile)} --vm ${cfg.vmEngine}`);
}

class LpcTaskProvider {
  provideTasks() {
    const file = activeLpcFile();
    if (!file) {
      return [];
    }
    return [
      this.makeTask("compile", "LPC: compile current file", file),
      this.makeTask("run", "LPC: run current file", file)
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

function runProcess(command, args, cwd, channelName) {
  return new Promise((resolve) => {
    const output = vscode.window.createOutputChannel(channelName);
    output.show(true);
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

function publishDiagnostics(text, diagnostics) {
  const byFile = new Map();
  const re = /^(.*?):(error|warning|note)\((\d+):(\d+)\):\s+(.*)$/gm;
  let m;
  while ((m = re.exec(text)) !== null) {
    const file = path.resolve(m[1]);
    const line = Math.max(0, Number(m[3]) - 1);
    const column = Math.max(0, Number(m[4]) - 1);
    const range = new vscode.Range(line, column, line, column + 1);
    const severity = m[2] === "error"
      ? vscode.DiagnosticSeverity.Error
      : m[2] === "warning"
        ? vscode.DiagnosticSeverity.Warning
        : vscode.DiagnosticSeverity.Information;
    const item = new vscode.Diagnostic(range, m[5], severity);
    item.source = "lpc";
    if (!byFile.has(file)) {
      byFile.set(file, []);
    }
    byFile.get(file).push(item);
  }
  for (const [file, items] of byFile) {
    diagnostics.set(vscode.Uri.file(file), items);
  }
}

class LpcDebugAdapter {
  constructor(diagnostics) {
    this.diagnostics = diagnostics;
    this.emitter = new vscode.EventEmitter();
    this.onDidSendMessage = this.emitter.event;
    this.sequence = 1;
    this.breakpoints = new Map();
    this.frames = [];
    this.localsVariables = [];
    this.argsVariables = [];
    this.currentVariableScope = undefined;
    this.pendingEvaluations = [];
    this.child = undefined;
    this.launchArgs = undefined;
    this.configurationDone = false;
    this.launchConfigured = false;
    this.protocolMode = false;
  }

  handleMessage(message) {
    switch (message.command) {
      case "initialize":
        this.respond(message, {
          supportsConfigurationDoneRequest: true,
          supportsConditionalBreakpoints: true,
          supportsEvaluateForHovers: true
        });
        this.event("initialized");
        break;
      case "launch":
        this.launchArgs = message.arguments || {};
        this.respond(message);
        this.startLaunch();
        break;
      case "setBreakpoints":
        this.setBreakpoints(message);
        break;
      case "configurationDone":
        this.respond(message);
        this.configurationDone = true;
        this.finishLaunchConfiguration();
        break;
      case "threads":
        this.respond(message, { threads: [{ id: 1, name: "LPC VM" }] });
        break;
      case "continue":
        this.respond(message, { allThreadsContinued: true });
        this.writeDebugger("c");
        break;
      case "next":
        this.respond(message);
        this.writeDebugger("n");
        break;
      case "stepIn":
        this.respond(message);
        this.writeDebugger("s");
        break;
      case "stepOut":
        this.respond(message);
        this.writeDebugger("finish");
        break;
      case "pause":
        this.respond(message);
        this.stopped("pause");
        break;
      case "stackTrace":
        this.respond(message, { stackFrames: this.stackFrames(), totalFrames: this.stackFrames().length });
        break;
      case "scopes":
        this.respond(message, {
          scopes: [
            { name: "Locals", variablesReference: 1, expensive: false },
            { name: "Arguments", variablesReference: 2, expensive: false }
          ]
        });
        break;
      case "variables":
        this.variablesFor(message);
        break;
      case "evaluate":
        this.evaluate(message);
        break;
      case "disconnect":
        this.respond(message);
        this.shutdown();
        break;
      default:
        this.respond(message);
        break;
    }
  }

  respond(request, body) {
    this.send({
      type: "response",
      seq: this.sequence++,
      request_seq: request.seq,
      command: request.command,
      success: true,
      body: body || {}
    });
  }

  event(name, body) {
    this.send({ type: "event", seq: this.sequence++, event: name, body: body || {} });
  }

  send(message) {
    this.emitter.fire(message);
  }

  dispose() {
    this.shutdown();
    this.emitter.dispose();
  }

  async startLaunch() {
    const program = this.launchArgs.program;
    const cfg = configFor(program);
    if (this.launchArgs.compilerPath) cfg.compilerPath = resolveToolPath(resolveDebugPath(this.launchArgs.compilerPath, cfg.workspace, program), "lpc_compiler");
    if (this.launchArgs.vmPath) cfg.vmPath = resolveToolPath(resolveDebugPath(this.launchArgs.vmPath, cfg.workspace, program), "lpc_vm");
    if (this.launchArgs.outRoot) cfg.outRoot = resolveDebugPath(this.launchArgs.outRoot, cfg.workspace, program);

    const compiled = await runProcess(cfg.compilerPath, buildCompileArgs(program, cfg), cfg.workspace, "LPC Compiler");
    publishDiagnostics(compiled.stdout + compiled.stderr, this.diagnostics);
    if (compiled.code !== 0) {
      this.event("output", { category: "stderr", output: "LPC compile failed.\n" });
      this.event("terminated");
      return;
    }

    const args = ["debug", "--entry-file", path.join(cfg.outRoot, "entry.txt")];
    if (this.launchArgs.entryModule) {
      args.splice(1, 0, this.launchArgs.entryModule);
    }
    this.child = cp.spawn(cfg.vmPath, args, {
      cwd: cfg.workspace,
      shell: false,
      env: { ...process.env, LPC_DEBUG_PROTOCOL: "json" }
    });
    this.child.stdout.on("data", (data) => this.onDebuggerOutput(data.toString()));
    this.child.stderr.on("data", (data) => this.event("output", { category: "stderr", output: data.toString() }));
    this.child.on("error", (err) => {
      this.event("output", { category: "stderr", output: `${err.message}\n` });
      this.event("terminated");
    });
    this.child.on("close", () => this.event("terminated"));
    this.event("thread", { reason: "started", threadId: 1 });
    this.finishLaunchConfiguration();
  }

  finishLaunchConfiguration() {
    if (!this.child || !this.configurationDone || this.launchConfigured) {
      return;
    }
    this.launchConfigured = true;
    this.applyBreakpoints();
    if (this.launchArgs && !this.launchArgs.stopOnEntry) {
      this.writeDebugger("c");
    } else {
      this.stopped("entry");
    }
  }

  setBreakpoints(request) {
    const sourcePath = request.arguments.source.path;
    const points = request.arguments.breakpoints || [];
    this.breakpoints.set(sourcePath, points);
    this.respond(request, {
      breakpoints: points.map((bp) => ({ verified: true, line: bp.line }))
    });
    this.applyBreakpoints();
  }

  applyBreakpoints() {
    if (!this.child || !this.child.stdin.writable) {
      return;
    }
    this.writeDebugger("clear");
    for (const [file, points] of this.breakpoints.entries()) {
      for (const bp of points) {
        const spec = `${this.moduleSpec(file)}:${bp.line}`;
        this.writeDebugger(bp.condition ? `b ${spec} if ${bp.condition}` : `b ${spec}`);
      }
    }
  }

  moduleSpec(file) {
    const root = workspaceFolderFor(file);
    let rel = path.relative(root, file).replace(/\\/g, "/");
    if (rel.endsWith(".lpc")) {
      rel = rel.slice(0, -4);
    }
    return rel;
  }

  writeDebugger(command) {
    if (this.child && this.child.stdin.writable) {
      this.child.stdin.write(`${command}\n`);
    }
  }

  onDebuggerOutput(text) {
    this.event("output", { category: "stdout", output: text });
    for (const line of text.split(/\r?\n/)) {
      if (this.parseProtocolLine(line)) {
        continue;
      }
      this.parseVariableOutput(line);
      const frame = this.protocolMode ? undefined : this.parseFrame(line);
      if (frame) {
        this.frames = [frame];
        this.refreshVariables();
        this.stopped("breakpoint");
      }
      if (/Program exited\.|Program completed immediately\./.test(line)) {
        this.event("terminated");
      }
    }
  }

  parseProtocolLine(line) {
    if (!line.startsWith("{")) {
      return false;
    }
    let message;
    try {
      message = JSON.parse(line);
    } catch (_) {
      return false;
    }
    if (!message || message.type !== "event") {
      return false;
    }
    this.protocolMode = true;

    if (message.event === "stopped") {
      const frames = message.body && Array.isArray(message.body.frames) ? message.body.frames : [];
      this.frames = frames.map((frame, index) => this.protocolFrame(frame, index));
      this.refreshVariables();
      this.stopped((message.body && message.body.reason) || "breakpoint");
      return true;
    }

    if (message.event === "variables") {
      const scope = message.body && message.body.scope;
      const variables = (message.body && Array.isArray(message.body.variables) ? message.body.variables : []).map((item) => ({
        name: item.name || `slot${item.index || 0}`,
        value: item.value || "",
        variablesReference: 0,
        presentationHint: { kind: scope === "args" ? "property" : "data" }
      }));
      if (scope === "args") {
        this.argsVariables = variables;
      } else {
        this.localsVariables = variables;
      }
      return true;
    }

    if (message.event === "evaluate") {
      this.finishPendingEvaluation(message.body || {});
      return true;
    }

    if (message.event === "breakpoint") {
      const bp = message.body || {};
      const status = bp.verified ? "verified" : "rejected";
      this.event("output", { category: "console", output: `Breakpoint ${bp.action || "update"} ${status}: ${bp.file || "<unknown>"}:${bp.line || 0}\n` });
      return true;
    }

    if (message.event === "runtimeError") {
      const body = message.body || {};
      const output = `${body.message || "Runtime error"}\n`;
      this.event("output", { category: "stderr", output });
      this.stopped("exception");
      return true;
    }

    if (message.event === "terminated") {
      this.event("terminated");
      return true;
    }

    return false;
  }

  protocolFrame(frame, index) {
    const objectName = frame.object || "<unknown>";
    const functionName = frame.function || "<unknown>";
    const sourcePath = this.resolveSource(objectName);
    return {
      id: frame.id || index + 1,
      name: `${objectName}::${functionName}`,
      line: frame.line || 1,
      column: 1,
      source: sourcePath ? { name: path.basename(sourcePath), path: sourcePath } : undefined
    };
  }

  parseFrame(line) {
    const m = /^\s*at\s+(.+?)::(.+?)(?::(\d+))?\s+pc=(\d+)/.exec(line);
    if (!m) {
      return undefined;
    }
    const sourcePath = this.resolveSource(m[1]);
    return {
      id: 1,
      name: `${m[1]}::${m[2]}`,
      line: m[3] ? Number(m[3]) : 1,
      column: 1,
      source: sourcePath ? { name: path.basename(sourcePath), path: sourcePath } : undefined
    };
  }

  resolveSource(moduleName) {
    const folders = vscode.workspace.workspaceFolders || [];
    for (const folder of folders) {
      const p1 = path.join(folder.uri.fsPath, `${moduleName}.lpc`);
      const p2 = path.join(folder.uri.fsPath, moduleName);
      if (fs.existsSync(p1)) return p1;
      if (fs.existsSync(p2)) return p2;
    }
    return undefined;
  }

  stopped(reason) {
    this.event("stopped", { reason, threadId: 1, allThreadsStopped: true });
  }

  refreshVariables() {
    this.localsVariables = [];
    this.argsVariables = [];
    this.currentVariableScope = undefined;
    this.writeDebugger("locals");
    this.writeDebugger("args");
  }

  stackFrames() {
    return this.frames.length > 0
      ? this.frames
      : [{ id: 1, name: "LPC VM", line: 1, column: 1 }];
  }

  variablesFor(request) {
    const ref = request.arguments.variablesReference;
    this.respond(request, { variables: ref === 2 ? this.argsVariables : this.localsVariables });
  }

  parseVariableOutput(line) {
    if (/^Locals \(nargs=\d+, nlocal=\d+\):$/.test(line)) {
      this.currentVariableScope = "locals";
      this.localsVariables = [];
      return;
    }
    if (/^Args \(nargs=\d+\):$/.test(line)) {
      this.currentVariableScope = "args";
      this.argsVariables = [];
      return;
    }
    if (/^No arguments\./.test(line)) {
      this.currentVariableScope = undefined;
      this.argsVariables = [];
      return;
    }
    const m = /^\s+\[(\d+)\]\s+(.+?)\s+=\s+(.*)$/.exec(line);
    if (!m || !this.currentVariableScope) {
      if (line.trim() === "" || /^\(lpcdbg/.test(line) || /^\s*at\s+/.test(line)) {
        this.currentVariableScope = undefined;
      }
      return;
    }
    const variable = {
      name: m[2],
      value: m[3],
      variablesReference: 0,
      presentationHint: { kind: this.currentVariableScope === "args" ? "property" : "data" }
    };
    if (this.currentVariableScope === "args") {
      this.argsVariables.push(variable);
    } else {
      this.localsVariables.push(variable);
    }
  }

  evaluate(request) {
    const expr = (request.arguments && request.arguments.expression) || "";
    if (expr.trim()) {
      const timer = setTimeout(() => {
        this.respond(request, { result: "<evaluation timed out; see Debug Console output>", variablesReference: 0 });
        this.pendingEvaluations = this.pendingEvaluations.filter((item) => item.request !== request);
      }, 1000);
      this.pendingEvaluations.push({ request, timer });
      this.writeDebugger(`p ${expr.trim()}`);
      return;
    }
    this.respond(request, { result: "", variablesReference: 0 });
  }

  finishPendingEvaluation(body) {
    const pending = this.pendingEvaluations.shift();
    if (!pending) {
      const prefix = body.success === false ? "Evaluation failed" : "Evaluation";
      this.event("output", { category: body.success === false ? "stderr" : "stdout", output: `${prefix}: ${body.result || ""}\n` });
      return;
    }
    clearTimeout(pending.timer);
    this.respond(pending.request, {
      result: body.success === false ? (body.result || "<evaluation failed>") : (body.result || ""),
      variablesReference: 0
    });
  }

  shutdown() {
    for (const pending of this.pendingEvaluations) {
      clearTimeout(pending.timer);
    }
    this.pendingEvaluations = [];
    if (this.child) {
      this.writeDebugger("q");
      this.child.kill();
      this.child = undefined;
    }
  }
}

function resolveDebugPath(value, workspace, file) {
  return String(value || "")
    .replace(/\$\{workspaceFolder\}/g, workspace)
    .replace(/\$\{file\}/g, file);
}

module.exports = {
  activate,
  deactivate
};
