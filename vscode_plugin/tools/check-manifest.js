"use strict";

const fs = require("fs");
const path = require("path");

function fail(message) {
  console.error(`[manifest-check] ${message}`);
  process.exit(1);
}

function readJson(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf8"));
  } catch (err) {
    fail(`Failed to read JSON at ${filePath}: ${err.message}`);
  }
}

function hasCommand(commands, id) {
  return Array.isArray(commands) && commands.some((item) => item && item.command === id);
}

function hasConfig(configs, request) {
  return Array.isArray(configs)
    && configs.some((item) => item && item.type === "lpc" && item.request === request);
}

function assert(condition, message) {
  if (!condition) {
    fail(message);
  }
}

const root = path.resolve(__dirname, "..");
const packagePath = path.join(root, "package.json");
const manifest = readJson(packagePath);
const contributes = manifest.contributes || {};

assert(manifest.name === "lpc-language-tools", "Unexpected extension name.");
assert(typeof manifest.version === "string" && manifest.version.length > 0, "Missing extension version.");
assert(typeof manifest.main === "string" && fs.existsSync(path.join(root, manifest.main)), "Extension main entry is missing.");

const expectedCommands = [
  "lpc.compileCurrent",
  "lpc.runCurrent",
  "lpc.debugCurrent",
  "lpc.attach",
  "lpc.restartLsp"
];

for (const id of expectedCommands) {
  assert(hasCommand(contributes.commands, id), `Missing command contribution: ${id}`);
}

assert(Array.isArray(contributes.debuggers) && contributes.debuggers.length > 0, "Missing debugger contribution.");
const debuggerConfig = contributes.debuggers[0] || {};
assert(debuggerConfig.type === "lpc", "Debugger type must be lpc.");
assert(hasConfig(debuggerConfig.initialConfigurations, "launch"), "Missing launch debug configuration.");
assert(hasConfig(debuggerConfig.initialConfigurations, "attach"), "Missing attach debug configuration.");

assert(Array.isArray(contributes.languages) && contributes.languages.some((item) => item.id === "lpc"), "Missing LPC language registration.");
assert(Array.isArray(contributes.grammars) && contributes.grammars.length > 0, "Missing grammar contribution.");

console.log("[manifest-check] OK");
