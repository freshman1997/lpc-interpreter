#include "lsp/lsp_server.h"
#include <algorithm>
#include <sstream>

namespace lpc {
namespace lsp {

static std::vector<std::string> SplitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::string line;
  std::istringstream iss(text);
  while (std::getline(iss, line)) {
    lines.push_back(line);
  }
  if (!text.empty() && text.back() == '\n') {
    lines.push_back("");
  }
  return lines;
}

static Position OffsetToPosition(const std::string &text, std::size_t offset) {
  Position pos;
  std::size_t i = 0;
  int line = 0;
  while (i < offset && i < text.size()) {
    if (text[i] == '\n') {
      ++line;
      pos.character = 0;
    } else {
      ++pos.character;
    }
    ++i;
  }
  pos.line = line;
  return pos;
}

static std::string TrimIdent(const std::string &s) {
  std::size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return "";
  std::size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

static bool IsIdentChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static std::string WordAtPosition(const std::string &text,
                                  const Position &pos) {
  auto lines = SplitLines(text);
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size()))
    return "";
  const std::string &line = lines[pos.line];
  if (pos.character < 0 || pos.character >= static_cast<int>(line.size()))
    return "";
  int start = pos.character;
  while (start > 0 && IsIdentChar(line[start - 1]))
    --start;
  int end = pos.character;
  while (end < static_cast<int>(line.size()) && IsIdentChar(line[end]))
    ++end;
  return line.substr(start, end - start);
}

void SymbolIndex::IndexDocument(const std::string &uri,
                                const std::string &text) {
  RemoveDocument(uri);
  ParseDocument(uri, text);
}

void SymbolIndex::RemoveDocument(const std::string &uri) {
  auto it = uri_to_symbols_.find(uri);
  if (it == uri_to_symbols_.end())
    return;
  for (const auto &sym : it->second) {
    auto &vec = name_to_symbols_[sym.name];
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](const SymbolInfo &s) { return s.uri == uri; }),
              vec.end());
    if (vec.empty())
      name_to_symbols_.erase(sym.name);
  }
  uri_to_symbols_.erase(it);
}

void SymbolIndex::ParseDocument(const std::string &uri,
                                const std::string &text) {
  auto lines = SplitLines(text);
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    const std::string &line = lines[i];
    std::string trimmed = TrimIdent(line);

    if (trimmed.empty() || trimmed[0] == '/' || trimmed[0] == '#')
      continue;

    if (trimmed.compare(0, 9, "inherit ") == 0 ||
        trimmed.compare(0, 9, "inherit\t") == 0) {
      std::string name = TrimIdent(trimmed.substr(8));
      SymbolInfo sym;
      sym.name = name;
      sym.kind = "inherit";
      sym.uri = uri;
      sym.range.start = {i, 0};
      sym.range.end = {i, static_cast<int>(line.size())};
      sym.selection_range.start = {i, static_cast<int>(line.find(name))};
      sym.selection_range.end = {
          i, static_cast<int>(line.find(name) + name.size())};
      name_to_symbols_[name].push_back(sym);
      uri_to_symbols_[uri].push_back(sym);
      continue;
    }

    if (trimmed.compare(0, 6, "class ") == 0 ||
        trimmed.compare(0, 6, "class\t") == 0) {
      std::string rest = trimmed.substr(6);
      auto colon = rest.find('{');
      auto paren = rest.find('(');
      auto end = rest.find_first_of(" \t{(");
      std::string name =
          (end != std::string::npos) ? rest.substr(0, end) : TrimIdent(rest);
      SymbolInfo sym;
      sym.name = name;
      sym.kind = "class";
      sym.uri = uri;
      sym.range.start = {i, 0};
      sym.range.end = {i, static_cast<int>(line.size())};
      sym.selection_range.start = {i, static_cast<int>(line.find(name))};
      sym.selection_range.end = {
          i, static_cast<int>(line.find(name) + name.size())};
      name_to_symbols_[name].push_back(sym);
      uri_to_symbols_[uri].push_back(sym);
      continue;
    }

    if (trimmed.find("void ") != std::string::npos ||
        trimmed.find("int ") != std::string::npos ||
        trimmed.find("float ") != std::string::npos ||
        trimmed.find("string ") != std::string::npos ||
        trimmed.find("object ") != std::string::npos ||
        trimmed.find("mixed ") != std::string::npos ||
        trimmed.find("mapping ") != std::string::npos ||
        trimmed.find("array ") != std::string::npos ||
        trimmed.find("buffer ") != std::string::npos ||
        trimmed.find("closure ") != std::string::npos ||
        (trimmed.find('(') != std::string::npos &&
         trimmed.find(')') != std::string::npos &&
         trimmed.find(';') == std::string::npos &&
         i + 1 < static_cast<int>(lines.size()))) {
      auto paren_pos = trimmed.find('(');
      if (paren_pos != std::string::npos) {
        std::string before_paren = TrimIdent(trimmed.substr(0, paren_pos));
        auto last_space = before_paren.find_last_of(" \t");
        std::string name = (last_space != std::string::npos)
                               ? before_paren.substr(last_space + 1)
                               : before_paren;
        if (!name.empty() && IsIdentChar(name[0])) {
          SymbolInfo sym;
          sym.name = name;
          sym.kind = "function";
          sym.uri = uri;
          sym.range.start = {i, 0};
          sym.range.end = {i, static_cast<int>(line.size())};
          sym.selection_range.start = {i, static_cast<int>(line.find(name))};
          sym.selection_range.end = {
              i, static_cast<int>(line.find(name) + name.size())};
          name_to_symbols_[name].push_back(sym);
          uri_to_symbols_[uri].push_back(sym);
        }
      }
    }

    if (trimmed.compare(0, 4, "var ") == 0 ||
        trimmed.compare(0, 4, "var\t") == 0) {
      std::string rest = TrimIdent(trimmed.substr(4));
      auto eq = rest.find('=');
      auto semi = rest.find(';');
      auto end = rest.find_first_of(" \t=;");
      std::string name =
          (end != std::string::npos) ? rest.substr(0, end) : rest;
      if (!name.empty() && IsIdentChar(name[0])) {
        SymbolInfo sym;
        sym.name = name;
        sym.kind = "variable";
        sym.uri = uri;
        sym.range.start = {i, 0};
        sym.range.end = {i, static_cast<int>(line.size())};
        sym.selection_range.start = {i, static_cast<int>(line.find(name))};
        sym.selection_range.end = {
            i, static_cast<int>(line.find(name) + name.size())};
        name_to_symbols_[name].push_back(sym);
        uri_to_symbols_[uri].push_back(sym);
      }
    }
  }
}

std::vector<SymbolInfo>
SymbolIndex::FindDefinition(const std::string &name) const {
  auto it = name_to_symbols_.find(name);
  if (it == name_to_symbols_.end())
    return {};
  std::vector<SymbolInfo> defs;
  for (const auto &sym : it->second) {
    if (sym.kind == "function" || sym.kind == "class" ||
        sym.kind == "variable") {
      defs.push_back(sym);
    }
  }
  return defs;
}

std::vector<Location>
SymbolIndex::FindReferences(const std::string &name) const {
  auto it = name_to_symbols_.find(name);
  if (it == name_to_symbols_.end())
    return {};
  std::vector<Location> locs;
  for (const auto &sym : it->second) {
    locs.push_back({sym.uri, sym.selection_range});
  }
  return locs;
}

std::vector<Location>
SymbolIndex::FindReferencesInUri(const std::string &name,
                                 const std::string &uri) const {
  auto it = uri_to_symbols_.find(uri);
  if (it == uri_to_symbols_.end())
    return {};
  std::vector<Location> locs;
  for (const auto &sym : it->second) {
    if (sym.name == name) {
      locs.push_back({sym.uri, sym.selection_range});
    }
  }
  return locs;
}

std::vector<std::pair<std::string, std::string>>
SymbolIndex::Rename(const std::string &old_name,
                    const std::string &new_name) const {
  auto it = name_to_symbols_.find(old_name);
  if (it == name_to_symbols_.end())
    return {};
  std::vector<std::pair<std::string, std::string>> result;
  for (const auto &sym : it->second) {
    result.push_back({sym.uri, sym.kind});
  }
  return result;
}

std::vector<SymbolInfo> SymbolIndex::AllSymbols() const {
  std::vector<SymbolInfo> all;
  for (auto &[name, syms] : name_to_symbols_) {
    for (auto &sym : syms) {
      all.push_back(sym);
    }
  }
  return all;
}

std::optional<SymbolInfo>
SymbolIndex::SymbolAtPosition(const std::string &uri,
                              const Position &pos) const {
  auto it = uri_to_symbols_.find(uri);
  if (it == uri_to_symbols_.end())
    return std::nullopt;
  for (const auto &sym : it->second) {
    if (sym.selection_range.start.line == pos.line) {
      int start_char = sym.selection_range.start.character;
      int end_char = sym.selection_range.end.character;
      if (pos.character >= start_char && pos.character <= end_char) {
        return sym;
      }
    }
  }
  return std::nullopt;
}

LspServer::LspServer(SendMessageFn send_fn) : send_fn_(std::move(send_fn)) {}

void LspServer::HandleMessage(const JsonNode &message) {
  if (!message.IsObject())
    return;
  bool has_id = message.Has("id");
  std::string method =
      message["method"].IsString() ? message["method"].AsString() : "";

  if (has_id) {
    HandleRequest(message["id"], method, message["params"]);
  } else if (!method.empty()) {
    HandleNotification(method, message["params"]);
  }
}

void LspServer::HandleRequest(const JsonNode &id, const std::string &method,
                              const JsonNode &params) {
  if (method == "initialize") {
    auto result = Initialize(params);
    SendResponse(id, result);
  } else if (method == "shutdown") {
    auto result = Shutdown();
    shutdown_ = true;
    SendResponse(id, result);
  } else if (method == "textDocument/definition") {
    auto result = TextDocumentDefinition(params);
    SendResponse(id, result);
  } else if (method == "textDocument/references") {
    auto result = TextDocumentReferences(params);
    SendResponse(id, result);
  } else if (method == "textDocument/rename") {
    auto result = TextDocumentRename(params);
    SendResponse(id, result);
  } else if (method == "textDocument/hover") {
    auto result = TextDocumentHover(params);
    SendResponse(id, result);
  } else if (method == "textDocument/completion") {
    auto result = TextDocumentCompletion(params);
    SendResponse(id, result);
  } else if (method == "textDocument/documentSymbol") {
    auto result = TextDocumentDocumentSymbol(params);
    SendResponse(id, result);
  } else if (method == "workspace/symbol") {
    auto result = WorkspaceSymbol(params);
    SendResponse(id, result);
  } else {
    SendError(id, -32601, "Method not found: " + method);
  }
}

void LspServer::HandleNotification(const std::string &method,
                                   const JsonNode &params) {
  if (method == "initialized") {
    initialized_ = true;
  } else if (method == "textDocument/didOpen") {
    DidOpen(params);
  } else if (method == "textDocument/didChange") {
    DidChange(params);
  } else if (method == "textDocument/didClose") {
    DidClose(params);
  } else if (method == "exit") {
    std::exit(shutdown_ ? 0 : 1);
  }
}

JsonNode LspServer::Initialize(const JsonNode &params) {
  if (params.Has("rootUri") && params["rootUri"].IsString()) {
    root_uri_ = params["rootUri"].AsString();
  }

  std::unordered_map<std::string, JsonNode> caps;

  std::unordered_map<std::string, JsonNode> completion;
  std::unordered_map<std::string, JsonNode> comp_item;
  comp_item["snippetSupport"] = JsonNode(false);
  completion["completionItem"] = JsonNode(std::move(comp_item));
  caps["completionProvider"] = JsonNode(std::move(completion));

  std::unordered_map<std::string, JsonNode> hover;
  hover["contentFormat"] =
      JsonNode(std::vector<JsonNode>{JsonNode(std::string("plaintext"))});
  caps["hoverProvider"] = JsonNode(std::move(hover));

  caps["definitionProvider"] = JsonNode(true);
  caps["referencesProvider"] = JsonNode(true);
  caps["renameProvider"] = JsonNode(true);
  caps["documentSymbolProvider"] = JsonNode(true);
  caps["workspaceSymbolProvider"] = JsonNode(true);

  std::unordered_map<std::string, JsonNode> sync;
  sync["openClose"] = JsonNode(true);
  sync["change"] = JsonNode(1);
  std::unordered_map<std::string, JsonNode> text_doc;
  text_doc["synchronization"] = JsonNode(std::move(sync));
  caps["textDocumentSync"] = JsonNode(std::move(text_doc));

  std::unordered_map<std::string, JsonNode> result;
  result["capabilities"] = JsonNode(std::move(caps));
  result["serverInfo"] = JsonNode(std::unordered_map<std::string, JsonNode>{
      {"name", JsonNode(std::string("lpc-lsp"))},
      {"version", JsonNode(std::string("1.0.0"))}});
  return JsonNode(std::move(result));
}

JsonNode LspServer::Shutdown() { return JsonNode(); }

JsonNode LspServer::TextDocumentDefinition(const JsonNode &params) {
  std::string uri = params["textDocument"]["uri"].IsString()
                        ? params["textDocument"]["uri"].AsString()
                        : "";
  int line = params["position"]["line"].IsNumber()
                 ? params["position"]["line"].AsInt()
                 : 0;
  int character = params["position"]["character"].IsNumber()
                      ? params["position"]["character"].AsInt()
                      : 0;

  auto doc_it = open_docs_.find(uri);
  if (doc_it == open_docs_.end())
    return JsonNode();

  std::string word = WordAtPosition(doc_it->second.text, {line, character});
  if (word.empty())
    return JsonNode();

  auto defs = symbol_index_.FindDefinition(word);
  if (defs.empty())
    return JsonNode();

  std::vector<JsonNode> locations;
  for (const auto &def : defs) {
    std::unordered_map<std::string, JsonNode> loc;
    loc["uri"] = JsonNode(def.uri);
    std::unordered_map<std::string, JsonNode> range;
    std::unordered_map<std::string, JsonNode> start;
    start["line"] = JsonNode(def.selection_range.start.line);
    start["character"] = JsonNode(def.selection_range.start.character);
    std::unordered_map<std::string, JsonNode> end;
    end["line"] = JsonNode(def.selection_range.end.line);
    end["character"] = JsonNode(def.selection_range.end.character);
    range["start"] = JsonNode(std::move(start));
    range["end"] = JsonNode(std::move(end));
    loc["range"] = JsonNode(std::move(range));
    locations.push_back(JsonNode(std::move(loc)));
  }
  return JsonNode(std::move(locations));
}

JsonNode LspServer::TextDocumentReferences(const JsonNode &params) {
  std::string uri = params["textDocument"]["uri"].IsString()
                        ? params["textDocument"]["uri"].AsString()
                        : "";
  int line = params["position"]["line"].IsNumber()
                 ? params["position"]["line"].AsInt()
                 : 0;
  int character = params["position"]["character"].IsNumber()
                      ? params["position"]["character"].AsInt()
                      : 0;

  auto doc_it = open_docs_.find(uri);
  if (doc_it == open_docs_.end())
    return JsonNode(std::vector<JsonNode>{});

  std::string word = WordAtPosition(doc_it->second.text, {line, character});
  if (word.empty())
    return JsonNode(std::vector<JsonNode>{});

  auto refs = symbol_index_.FindReferences(word);
  std::vector<JsonNode> locations;
  for (const auto &ref : refs) {
    std::unordered_map<std::string, JsonNode> loc;
    loc["uri"] = JsonNode(ref.uri);
    std::unordered_map<std::string, JsonNode> range;
    std::unordered_map<std::string, JsonNode> start;
    start["line"] = JsonNode(ref.range.start.line);
    start["character"] = JsonNode(ref.range.start.character);
    std::unordered_map<std::string, JsonNode> end;
    end["line"] = JsonNode(ref.range.end.line);
    end["character"] = JsonNode(ref.range.end.character);
    range["start"] = JsonNode(std::move(start));
    range["end"] = JsonNode(std::move(end));
    loc["range"] = JsonNode(std::move(range));
    locations.push_back(JsonNode(std::move(loc)));
  }
  return JsonNode(std::move(locations));
}

JsonNode LspServer::TextDocumentRename(const JsonNode &params) {
  std::string uri = params["textDocument"]["uri"].IsString()
                        ? params["textDocument"]["uri"].AsString()
                        : "";
  int line = params["position"]["line"].IsNumber()
                 ? params["position"]["line"].AsInt()
                 : 0;
  int character = params["position"]["character"].IsNumber()
                      ? params["position"]["character"].AsInt()
                      : 0;
  std::string new_name =
      params["newName"].IsString() ? params["newName"].AsString() : "";

  auto doc_it = open_docs_.find(uri);
  if (doc_it == open_docs_.end())
    return JsonNode();

  std::string word = WordAtPosition(doc_it->second.text, {line, character});
  if (word.empty())
    return JsonNode();

  auto rename_info = symbol_index_.Rename(word, new_name);
  if (rename_info.empty())
    return JsonNode();

  std::unordered_map<std::string, std::vector<JsonNode>> changes_by_uri;
  auto refs = symbol_index_.FindReferences(word);
  for (const auto &ref : refs) {
    std::unordered_map<std::string, JsonNode> text_edit;
    std::unordered_map<std::string, JsonNode> range;
    std::unordered_map<std::string, JsonNode> start;
    start["line"] = JsonNode(ref.range.start.line);
    start["character"] = JsonNode(ref.range.start.character);
    std::unordered_map<std::string, JsonNode> end;
    end["line"] = JsonNode(ref.range.end.line);
    end["character"] = JsonNode(ref.range.end.character);
    range["start"] = JsonNode(std::move(start));
    range["end"] = JsonNode(std::move(end));
    text_edit["range"] = JsonNode(std::move(range));
    text_edit["newText"] = JsonNode(new_name);
    changes_by_uri[ref.uri].push_back(JsonNode(std::move(text_edit)));
  }

  std::unordered_map<std::string, JsonNode> changes;
  for (auto &[u, edits] : changes_by_uri) {
    changes[u] = JsonNode(std::move(edits));
  }

  std::unordered_map<std::string, JsonNode> workspace_edit;
  workspace_edit["changes"] = JsonNode(std::move(changes));
  return JsonNode(std::move(workspace_edit));
}

JsonNode LspServer::TextDocumentHover(const JsonNode &params) {
  std::string uri = params["textDocument"]["uri"].IsString()
                        ? params["textDocument"]["uri"].AsString()
                        : "";
  int line = params["position"]["line"].IsNumber()
                 ? params["position"]["line"].AsInt()
                 : 0;
  int character = params["position"]["character"].IsNumber()
                      ? params["position"]["character"].AsInt()
                      : 0;

  auto doc_it = open_docs_.find(uri);
  if (doc_it == open_docs_.end())
    return JsonNode();

  std::string word = WordAtPosition(doc_it->second.text, {line, character});
  if (word.empty())
    return JsonNode();

  auto defs = symbol_index_.FindDefinition(word);
  if (defs.empty())
    return JsonNode();

  std::string hover_text;
  for (const auto &def : defs) {
    hover_text += def.kind + " " + def.name + " (" + def.uri + ")\n";
  }
  if (!hover_text.empty())
    hover_text.pop_back();

  std::unordered_map<std::string, JsonNode> result;
  result["contents"] = JsonNode(std::unordered_map<std::string, JsonNode>{
      {"kind", JsonNode(std::string("plaintext"))},
      {"value", JsonNode(hover_text)}});
  return JsonNode(std::move(result));
}

JsonNode LspServer::TextDocumentCompletion(const JsonNode &params) {
  std::vector<JsonNode> items;
  auto all = symbol_index_.AllSymbols();
  std::unordered_set<std::string> seen;
  for (const auto &sym : all) {
    if (seen.count(sym.name))
      continue;
    seen.insert(sym.name);
    std::unordered_map<std::string, JsonNode> item;
    item["label"] = JsonNode(sym.name);
    int kind = 6;
    if (sym.kind == "function")
      kind = 3;
    else if (sym.kind == "class")
      kind = 5;
    else if (sym.kind == "variable")
      kind = 6;
    else if (sym.kind == "inherit")
      kind = 8;
    item["kind"] = JsonNode(kind);
    items.push_back(JsonNode(std::move(item)));
  }
  return JsonNode(std::unordered_map<std::string, JsonNode>{
      {"isIncomplete", JsonNode(false)},
      {"items", JsonNode(std::move(items))}});
}

JsonNode LspServer::TextDocumentDocumentSymbol(const JsonNode &params) {
  std::string uri = params["textDocument"]["uri"].IsString()
                        ? params["textDocument"]["uri"].AsString()
                        : "";

  auto all = symbol_index_.AllSymbols();
  std::vector<JsonNode> symbols;
  for (const auto &sym : all) {
    if (sym.uri != uri)
      continue;
    std::unordered_map<std::string, JsonNode> s;
    s["name"] = JsonNode(sym.name);
    int kind = 12;
    if (sym.kind == "function")
      kind = 12;
    else if (sym.kind == "class")
      kind = 5;
    else if (sym.kind == "variable")
      kind = 13;
    else if (sym.kind == "inherit")
      kind = 8;
    s["kind"] = JsonNode(kind);
    std::unordered_map<std::string, JsonNode> range;
    std::unordered_map<std::string, JsonNode> rstart;
    rstart["line"] = JsonNode(sym.range.start.line);
    rstart["character"] = JsonNode(sym.range.start.character);
    std::unordered_map<std::string, JsonNode> rend;
    rend["line"] = JsonNode(sym.range.end.line);
    rend["character"] = JsonNode(sym.range.end.character);
    range["start"] = JsonNode(std::move(rstart));
    range["end"] = JsonNode(std::move(rend));
    s["range"] = JsonNode(std::move(range));
    std::unordered_map<std::string, JsonNode> sel_range;
    std::unordered_map<std::string, JsonNode> sstart;
    sstart["line"] = JsonNode(sym.selection_range.start.line);
    sstart["character"] = JsonNode(sym.selection_range.start.character);
    std::unordered_map<std::string, JsonNode> send;
    send["line"] = JsonNode(sym.selection_range.end.line);
    send["character"] = JsonNode(sym.selection_range.end.character);
    sel_range["start"] = JsonNode(std::move(sstart));
    sel_range["end"] = JsonNode(std::move(send));
    s["selectionRange"] = JsonNode(std::move(sel_range));
    symbols.push_back(JsonNode(std::move(s)));
  }
  return JsonNode(std::move(symbols));
}

JsonNode LspServer::WorkspaceSymbol(const JsonNode &params) {
  std::string query =
      params["query"].IsString() ? params["query"].AsString() : "";
  auto all = symbol_index_.AllSymbols();
  std::vector<JsonNode> symbols;
  for (const auto &sym : all) {
    if (!query.empty() && sym.name.find(query) == std::string::npos)
      continue;
    std::unordered_map<std::string, JsonNode> s;
    s["name"] = JsonNode(sym.name);
    int kind = 12;
    if (sym.kind == "function")
      kind = 12;
    else if (sym.kind == "class")
      kind = 5;
    else if (sym.kind == "variable")
      kind = 13;
    s["kind"] = JsonNode(kind);
    s["location"] = JsonNode(std::unordered_map<std::string, JsonNode>{
        {"uri", JsonNode(sym.uri)},
        {"range",
         JsonNode(std::unordered_map<std::string, JsonNode>{
             {"start", JsonNode(std::unordered_map<std::string, JsonNode>{
                           {"line", JsonNode(sym.selection_range.start.line)},
                           {"character",
                            JsonNode(sym.selection_range.start.character)}})},
             {"end", JsonNode(std::unordered_map<std::string, JsonNode>{
                         {"line", JsonNode(sym.selection_range.end.line)},
                         {"character",
                          JsonNode(sym.selection_range.end.character)}})}})}});
    symbols.push_back(JsonNode(std::move(s)));
  }
  return JsonNode(std::move(symbols));
}

void LspServer::DidOpen(const JsonNode &params) {
  std::string uri = params["textDocument"]["uri"].IsString()
                        ? params["textDocument"]["uri"].AsString()
                        : "";
  std::string text = params["textDocument"]["text"].IsString()
                         ? params["textDocument"]["text"].AsString()
                         : "";
  int version = params["textDocument"]["version"].IsNumber()
                    ? params["textDocument"]["version"].AsInt()
                    : 0;
  open_docs_[uri] = {version, text};
  symbol_index_.IndexDocument(uri, text);
}

void LspServer::DidChange(const JsonNode &params) {
  std::string uri = params["textDocument"]["uri"].IsString()
                        ? params["textDocument"]["uri"].AsString()
                        : "";
  int version = params["textDocument"]["version"].IsNumber()
                    ? params["textDocument"]["version"].AsInt()
                    : 0;

  auto it = open_docs_.find(uri);
  if (it == open_docs_.end())
    return;

  const auto &changes = params["contentChanges"];
  if (changes.IsArray() && changes.ArraySize() > 0) {
    std::string text =
        changes[0]["text"].IsString() ? changes[0]["text"].AsString() : "";
    it->second.version = version;
    it->second.text = text;
    symbol_index_.IndexDocument(uri, text);
  }
}

void LspServer::DidClose(const JsonNode &params) {
  std::string uri = params["textDocument"]["uri"].IsString()
                        ? params["textDocument"]["uri"].AsString()
                        : "";
  open_docs_.erase(uri);
  symbol_index_.RemoveDocument(uri);
}

void LspServer::SendResponse(const JsonNode &id, const JsonNode &result) {
  JsonRpcResponse resp;
  if (id.IsNumber())
    resp.id = id.AsInt();
  else if (id.IsString())
    resp.id = id.AsString();
  resp.result = result;
  auto msg = ResponseToJson(resp);
  send_fn_(EncodeRpcMessage(msg));
}

void LspServer::SendError(const JsonNode &id, int code,
                          const std::string &message) {
  JsonRpcResponse resp;
  if (id.IsNumber())
    resp.id = id.AsInt();
  else if (id.IsString())
    resp.id = id.AsString();
  resp.has_error = true;
  resp.error = JsonNode(std::unordered_map<std::string, JsonNode>{
      {"code", JsonNode(code)}, {"message", JsonNode(message)}});
  auto msg = ResponseToJson(resp);
  send_fn_(EncodeRpcMessage(msg));
}

void LspServer::SendNotification(const std::string &method,
                                 const JsonNode &params) {
  JsonRpcRequest req;
  req.is_notification = true;
  req.method = method;
  req.params = params;
  auto msg = RequestToJson(req);
  send_fn_(EncodeRpcMessage(msg));
}

std::string LspServer::UriToPath(const std::string &uri) const {
  if (uri.compare(0, 7, "file://") == 0) {
#ifdef _WIN32
    std::string path = uri.substr(8);
    if (path.size() >= 2 && path[0] == '/' && path[2] == ':')
      path = path.substr(1);
    return path;
#else
    return uri.substr(7);
#endif
  }
  return uri;
}

std::string LspServer::PathToUri(const std::string &path) const {
#ifdef _WIN32
  std::string uri = "file:///";
  for (char c : path) {
    if (c == '\\')
      uri += '/';
    else
      uri += c;
  }
  return uri;
#else
  return "file://" + path;
#endif
}

void LspServer::Run(std::istream &in) {
  JsonNode msg;
  while (ReadRpcMessage(in, msg)) {
    HandleMessage(msg);
    msg = JsonNode();
  }
}

} // namespace lsp
} // namespace lpc
