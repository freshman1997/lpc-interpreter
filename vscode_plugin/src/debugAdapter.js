"use strict";

const vscode = require("vscode");
const path = require("path");
const fs = require("fs");
const cp = require("child_process");
const { publishDiagnostics } = require("./diagnostics");
const { buildCompileArgs, configFor, resolveToolPath, runProcess, workspaceFolderFor } = require("./utils");

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
    this.globalsVariables = [];
    this.currentVariableScope = undefined;
    this.pendingEvaluations = [];
    this.child = undefined;
    this.launchArgs = undefined;
    this.configurationDone = false;
    this.launchConfigured = false;
    this.protocolMode = false;
    this.debugSession = undefined;
  }

  setDebugSession(session) {
    this.debugSession = session;
  }

  handleMessage(message) {
    switch (message.command) {
      case "initialize":
        this.respond(message, {
          supportsConfigurationDoneRequest: true,
          supportsConditionalBreakpoints: true,
          supportsEvaluateForHovers: true,
          supportsBreakpointLocationsRequest: true,
          supportsHitConditionalBreakpoints: true,
          supportsLogMessageBreakpoints: true,
          supportsFunctionBreakpoints: false
        });
        this.event("initialized");
        break;
      case "breakpointLocations":
        this.breakpointLocations(message);
        break;
      case "source":
        this.respond(message, { content: "" });
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
            { name: "Arguments", variablesReference: 2, expensive: false },
            { name: "Globals", variablesReference: 3, expensive: false }
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
      this.event("output", { category: "stderr", output: "LPC compilation failed.\n" });
      this.event("terminated");
      return;
    }

    const args = ["debug", "--protocol", "json", "--entry-file", path.join(cfg.outRoot, "entry.txt"), "--bytecode-root", cfg.outRoot];
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

  breakpointLocations(request) {
    const sourcePath = request.arguments.source.path;
    if (!sourcePath || !sourcePath.endsWith(".lpc")) {
      this.respond(request, { breakpoints: [] });
      return;
    }
    try {
      const content = fs.readFileSync(sourcePath, "utf-8");
      const lines = content.split(/\r?\n/);
      const breakpoints = [];
      for (let i = 0; i < lines.length; i++) {
        if (lines[i].trim().length > 0) {
          breakpoints.push({ line: i + 1, column: 1 });
        }
      }
      this.respond(request, { breakpoints });
    } catch (e) {
      this.respond(request, { breakpoints: [] });
    }
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
      } else if (scope === "globals") {
        this.globalsVariables = variables;
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
    this.globalsVariables = [];
    this.currentVariableScope = undefined;
    this.writeDebugger("locals");
    this.writeDebugger("args");
    this.writeDebugger("globals");
  }

  stackFrames() {
    return this.frames.length > 0
      ? this.frames
      : [{ id: 1, name: "LPC VM", line: 1, column: 1 }];
  }

  variablesFor(request) {
    const ref = request.arguments.variablesReference;
    if (ref === 2) {
      this.respond(request, { variables: this.argsVariables });
    } else if (ref === 3) {
      this.respond(request, { variables: this.globalsVariables });
    } else {
      this.respond(request, { variables: this.localsVariables });
    }
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
    if (/^Globals \(/.test(line)) {
      this.currentVariableScope = "globals";
      this.globalsVariables = [];
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
    } else if (this.currentVariableScope === "globals") {
      this.globalsVariables.push(variable);
    } else {
      this.localsVariables.push(variable);
    }
  }

  evaluate(request) {
    const expr = (request.arguments && request.arguments.expression) || "";
    if (expr.trim()) {
      const timer = setTimeout(() => {
        this.respond(request, { result: "<Evaluation timeout, check debug console>", variablesReference: 0 });
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
      const prefix = body.success === false ? "Evaluation failed" : "Evaluation result";
      this.event("output", { category: body.success === false ? "stderr" : "stdout", output: `${prefix}: ${body.result || ""}\n` });
      return;
    }
    clearTimeout(pending.timer);
    this.respond(pending.request, {
      result: body.success === false ? (body.result || "<Evaluation failed>") : (body.result || ""),
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

module.exports = { LpcDebugAdapter };
