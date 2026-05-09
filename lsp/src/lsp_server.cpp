#include "lsp/lsp_server.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "frontend/pipeline.h"
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "frontend/sema.h"
#include "frontend/source.h"

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

static bool IsIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static std::string ReadFileAll(const std::string &path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in.is_open()) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static std::unordered_map<std::string, JsonNode> RangeToJson(const Range &r) {
  return {
      {"start", JsonNode(std::unordered_map<std::string, JsonNode>{
                    {"line", JsonNode(r.start.line)},
                    {"character", JsonNode(r.start.character)}})},
      {"end", JsonNode(std::unordered_map<std::string, JsonNode>{
                  {"line", JsonNode(r.end.line)},
                  {"character", JsonNode(r.end.character)}})}};
}

static Range SpanToRange(const lpc::frontend::SourceSpan &span,
                         std::size_t fallback_len) {
  const int line = span.line > 0 ? span.line - 1 : 0;
  const int character = span.column > 0 ? span.column - 1 : 0;
  const int len = span.length > 0 ? span.length : static_cast<int>(fallback_len);
  return {{line, character}, {line, character + std::max(1, len)}};
}

static bool RangeContains(const Range &r, const Position &p) {
  if (p.line < r.start.line || p.line > r.end.line) return false;
  if (p.line == r.start.line && p.character < r.start.character) return false;
  if (p.line == r.end.line && p.character > r.end.character) return false;
  return true;
}

static int CountChar(const std::string &line, char ch) {
  return static_cast<int>(std::count(line.begin(), line.end(), ch));
}

static bool IsKeyword(const std::string &name) {
  static const std::set<std::string> k = {
      "if", "else", "while", "for", "foreach", "in", "switch", "case", "default",
      "break", "continue", "return", "catch", "class", "inherit", "new", "var",
      "fun", "void", "int", "float", "string", "object", "mapping", "mixed",
      "array", "buffer", "closure", "static", "public", "private", "true", "false"};
  return k.count(name) != 0;
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
  if (it != uri_to_symbols_.end()) {
    for (const auto &sym : it->second) {
      auto &vec = name_to_symbols_[sym.name];
      vec.erase(std::remove_if(vec.begin(), vec.end(),
                               [&](const SymbolInfo &s) { return s.uri == uri; }),
                vec.end());
      if (vec.empty())
        name_to_symbols_.erase(sym.name);
      auto &id_vec = id_to_symbols_[sym.symbol_id];
      id_vec.erase(std::remove_if(id_vec.begin(), id_vec.end(),
                                  [&](const SymbolInfo &s) { return s.uri == uri; }),
                   id_vec.end());
      if (id_vec.empty())
        id_to_symbols_.erase(sym.symbol_id);
    }
    uri_to_symbols_.erase(it);
  }

  auto rit = uri_to_refs_.find(uri);
  if (rit == uri_to_refs_.end())
    return;
  for (const auto &ref : rit->second) {
    auto &vec = name_to_refs_[ref.name];
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](const ReferenceInfo &r) { return r.uri == uri; }),
              vec.end());
    if (vec.empty())
      name_to_refs_.erase(ref.name);
    auto &id_vec = id_to_refs_[ref.symbol_id];
    id_vec.erase(std::remove_if(id_vec.begin(), id_vec.end(),
                                [&](const ReferenceInfo &r) { return r.uri == uri; }),
                 id_vec.end());
    if (id_vec.empty())
      id_to_refs_.erase(ref.symbol_id);
  }
  uri_to_refs_.erase(rit);
}

void SymbolIndex::AddSymbol(const SymbolInfo &sym) {
  name_to_symbols_[sym.name].push_back(sym);
  id_to_symbols_[sym.symbol_id].push_back(sym);
  uri_to_symbols_[sym.uri].push_back(sym);
}

void SymbolIndex::AddReference(const ReferenceInfo &ref) {
  if (ref.name.empty() || ref.symbol_id.empty() || IsKeyword(ref.name)) return;
  name_to_refs_[ref.name].push_back(ref);
  id_to_refs_[ref.symbol_id].push_back(ref);
  uri_to_refs_[ref.uri].push_back(ref);
}

void SymbolIndex::ParseDocument(const std::string &uri,
                                const std::string &text) {
  lpc::frontend::DiagnosticSink diagnostics;
  lpc::frontend::SourceFile source;
  source.path = uri;
  source.text = text;

  lpc::frontend::Lexer lexer(&diagnostics);
  std::vector<lpc::frontend::Token> tokens = lexer.Tokenize(source);
  lpc::frontend::Parser parser(&diagnostics);
  std::unique_ptr<lpc::frontend::Module> module = parser.Parse(tokens);
  if (module) {
    lpc::frontend::Sema sema(&diagnostics);
    lpc::frontend::SemanticModel model = sema.Analyze(*module);

    for (const auto &record : model.symbols) {
      if (record.name.empty() || IsKeyword(record.name)) continue;
      SymbolInfo sym;
      sym.name = record.name;
      sym.kind = record.kind;
      sym.symbol_id = uri + "#" + record.symbol_id;
      sym.uri = uri;
      sym.range = SpanToRange(record.span, record.name.size());
      sym.selection_range = sym.range;
      sym.container_name = record.container_name;
      sym.detail = record.detail;
      AddSymbol(sym);

      ReferenceInfo decl_ref;
      decl_ref.name = sym.name;
      decl_ref.symbol_id = sym.symbol_id;
      decl_ref.uri = uri;
      decl_ref.range = sym.selection_range;
      AddReference(decl_ref);
    }

    for (const auto &record : model.references) {
      if (record.name.empty() || record.symbol_id.empty() || IsKeyword(record.name)) continue;
      ReferenceInfo ref;
      ref.name = record.name;
      ref.symbol_id = uri + "#" + record.symbol_id;
      ref.uri = uri;
      ref.range = SpanToRange(record.span, record.name.size());
      AddReference(ref);
    }
    return;
  }

  auto lines = SplitLines(text);
  std::unordered_map<std::string, std::string> globals;
  std::unordered_map<std::string, std::string> functions;
  std::unordered_map<std::string, std::string> classes;
  std::vector<std::unordered_map<std::string, std::string>> locals_by_line(lines.size());
  std::vector<std::string> function_by_line(lines.size());
  std::vector<std::vector<SymbolInfo>> local_symbols_by_line(lines.size());
  std::string current_func;
  int function_depth = 0;

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
      sym.symbol_id = uri + "#inherit:" + name;
      sym.uri = uri;
      sym.range.start = {i, 0};
      sym.range.end = {i, static_cast<int>(line.size())};
      sym.selection_range.start = {i, static_cast<int>(line.find(name))};
      sym.selection_range.end = {
          i, static_cast<int>(line.find(name) + name.size())};
      sym.detail = "inherit " + name;
      AddSymbol(sym);
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
      sym.symbol_id = uri + "#class:" + name;
      sym.uri = uri;
      sym.range.start = {i, 0};
      sym.range.end = {i, static_cast<int>(line.size())};
      sym.selection_range.start = {i, static_cast<int>(line.find(name))};
      sym.selection_range.end = {
          i, static_cast<int>(line.find(name) + name.size())};
      sym.detail = "class " + name;
      classes[name] = sym.symbol_id;
      AddSymbol(sym);
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
          sym.symbol_id = uri + "#function:" + name;
          sym.uri = uri;
          sym.range.start = {i, 0};
          sym.range.end = {i, static_cast<int>(line.size())};
          sym.selection_range.start = {i, static_cast<int>(line.find(name))};
          sym.selection_range.end = {
              i, static_cast<int>(line.find(name) + name.size())};
          sym.detail = before_paren + "()";
          functions[name] = sym.symbol_id;
          current_func = name;
          function_depth = 0;
          function_by_line[i] = current_func;
          std::size_t lp = line.find('(');
          std::size_t rp = line.find(')', lp == std::string::npos ? 0 : lp);
          if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
            std::string params = line.substr(lp + 1, rp - lp - 1);
            std::stringstream ps(params);
            std::string one;
            while (std::getline(ps, one, ',')) {
              one = TrimIdent(one);
              if (one.empty()) continue;
              std::size_t sp = one.find_last_of(" \t*");
              std::string pname = sp == std::string::npos ? one : one.substr(sp + 1);
              if (!pname.empty() && IsIdentStart(pname[0])) {
                std::string sid = uri + "#function:" + current_func + ":local:" + pname;
                locals_by_line[i][pname] = sid;
                std::size_t ppos = line.find(pname, lp);
                SymbolInfo psym;
                psym.name = pname;
                psym.kind = "variable";
                psym.symbol_id = sid;
                psym.uri = uri;
                psym.container_name = current_func;
                psym.detail = "param " + pname;
                psym.range.start = {i, static_cast<int>(ppos == std::string::npos ? lp : ppos)};
                psym.range.end = {i, static_cast<int>((ppos == std::string::npos ? lp : ppos) + pname.size())};
                psym.selection_range = psym.range;
                local_symbols_by_line[i].push_back(psym);
              }
            }
          }
          AddSymbol(sym);
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
        sym.symbol_id = current_func.empty()
            ? uri + "#global:" + name
            : uri + "#function:" + current_func + ":local:" + name;
        sym.uri = uri;
        sym.range.start = {i, 0};
        sym.range.end = {i, static_cast<int>(line.size())};
        sym.selection_range.start = {i, static_cast<int>(line.find(name))};
        sym.selection_range.end = {
            i, static_cast<int>(line.find(name) + name.size())};
        sym.container_name = current_func;
        sym.detail = current_func.empty() ? ("var " + name) : ("local " + name);
        if (current_func.empty()) {
          globals[name] = sym.symbol_id;
        } else {
          locals_by_line[i][name] = sym.symbol_id;
          local_symbols_by_line[i].push_back(sym);
        }
        if (current_func.empty()) {
          AddSymbol(sym);
        }
      }
    }

    if (!current_func.empty()) {
      function_by_line[i] = current_func;
      function_depth += CountChar(line, '{') - CountChar(line, '}');
      if (function_depth <= 0) {
        current_func.clear();
        function_depth = 0;
      }
    }
  }

  std::unordered_map<std::string, std::string> active_locals;
  std::string active_func;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (function_by_line[i] != active_func) {
      active_func = function_by_line[i];
      active_locals.clear();
    }
    for (const auto &p : locals_by_line[i]) {
      active_locals[p.first] = p.second;
    }
    for (const auto &sym : local_symbols_by_line[i]) {
      AddSymbol(sym);
    }
    std::string line = lines[i];
    bool line_comment = false;
    bool block_comment = false;
    bool string_lit = false;
    for (std::size_t j = 0; j < line.size();) {
      char c = line[j];
      char n = j + 1 < line.size() ? line[j + 1] : '\0';
      if (line_comment) break;
      if (block_comment) {
        if (c == '*' && n == '/') {
          j += 2;
          block_comment = false;
        } else {
          ++j;
        }
        continue;
      }
      if (string_lit) {
        if (c == '\\' && n != '\0') {
          j += 2;
        } else {
          if (c == '"') string_lit = false;
          ++j;
        }
        continue;
      }
      if (c == '/' && n == '/') {
        line_comment = true;
        continue;
      }
      if (c == '/' && n == '*') {
        block_comment = true;
        j += 2;
        continue;
      }
      if (c == '"') {
        string_lit = true;
        ++j;
        continue;
      }
      if (!IsIdentStart(line[j])) { ++j; continue; }
      std::size_t start = j;
      while (j < line.size() && IsIdentChar(line[j])) ++j;
      std::string name = line.substr(start, j - start);
      if (IsKeyword(name)) continue;
      std::string sid;
      if (!active_func.empty()) {
        auto lit = active_locals.find(name);
        if (lit != active_locals.end()) sid = lit->second;
      }
      if (sid.empty()) {
        auto git = globals.find(name);
        if (git != globals.end()) sid = git->second;
      }
      if (sid.empty()) {
        auto fit = functions.find(name);
        if (fit != functions.end()) sid = fit->second;
      }
      if (sid.empty()) {
        auto cit = classes.find(name);
        if (cit != classes.end()) sid = cit->second;
      }
      if (sid.empty()) sid = uri + "#unresolved:" + name;
      ReferenceInfo ref;
      ref.name = name;
      ref.symbol_id = sid;
      ref.uri = uri;
      ref.range.start = {i, static_cast<int>(start)};
      ref.range.end = {i, static_cast<int>(j)};
      AddReference(ref);
    }
  }
}

void SymbolIndex::ParseReferences(const std::string &uri,
                                  const std::string &text) {
  int line = 0;
  int character = 0;
  bool line_comment = false;
  bool block_comment = false;
  bool string_lit = false;

  for (std::size_t i = 0; i < text.size();) {
    char c = text[i];
    char n = i + 1 < text.size() ? text[i + 1] : '\0';
    if (c == '\n') {
      ++line;
      character = 0;
      line_comment = false;
      ++i;
      continue;
    }
    if (line_comment) {
      ++character;
      ++i;
      continue;
    }
    if (block_comment) {
      if (c == '*' && n == '/') {
        i += 2;
        character += 2;
        block_comment = false;
      } else {
        ++i;
        ++character;
      }
      continue;
    }
    if (string_lit) {
      if (c == '\\' && n != '\0') {
        i += 2;
        character += 2;
      } else {
        if (c == '"') string_lit = false;
        ++i;
        ++character;
      }
      continue;
    }
    if (c == '/' && n == '/') {
      line_comment = true;
      i += 2;
      character += 2;
      continue;
    }
    if (c == '/' && n == '*') {
      block_comment = true;
      i += 2;
      character += 2;
      continue;
    }
    if (c == '"') {
      string_lit = true;
      ++i;
      ++character;
      continue;
    }
    if (IsIdentStart(c)) {
      int start_char = character;
      std::size_t start = i;
      while (i < text.size() && IsIdentChar(text[i])) {
        ++i;
        ++character;
      }
      std::string name = text.substr(start, i - start);
      ReferenceInfo ref;
      ref.name = name;
      ref.uri = uri;
      ref.range.start = {line, start_char};
      ref.range.end = {line, character};
      ref.symbol_id = uri + "#lex:" + name;
      AddReference(ref);
      continue;
    }
    ++i;
    ++character;
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
  auto rit = name_to_refs_.find(name);
  if (rit == name_to_refs_.end())
    return {};
  std::vector<Location> locs;
  for (const auto &ref : rit->second) {
    locs.push_back({ref.uri, ref.range});
  }
  return locs;
}

std::vector<SymbolInfo>
SymbolIndex::FindDefinitionById(const std::string &symbol_id) const {
  auto it = id_to_symbols_.find(symbol_id);
  return it == id_to_symbols_.end() ? std::vector<SymbolInfo>{} : it->second;
}

std::vector<Location>
SymbolIndex::FindReferencesInUri(const std::string &name,
                                 const std::string &uri) const {
  auto it = uri_to_refs_.find(uri);
  if (it == uri_to_refs_.end())
    return {};
  std::vector<Location> locs;
  for (const auto &ref : it->second) {
    if (ref.name == name) {
      locs.push_back({ref.uri, ref.range});
    }
  }
  return locs;
}

std::vector<Location>
SymbolIndex::FindReferencesById(const std::string &symbol_id) const {
  auto it = id_to_refs_.find(symbol_id);
  if (it == id_to_refs_.end())
    return {};
  std::vector<Location> locs;
  for (const auto &ref : it->second) {
    locs.push_back({ref.uri, ref.range});
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
    exit_requested_ = true;
    exit_code_ = shutdown_ ? 0 : 1;
  }
}

JsonNode LspServer::Initialize(const JsonNode &params) {
  if (params.Has("rootUri") && params["rootUri"].IsString()) {
    root_uri_ = params["rootUri"].AsString();
    root_path_ = UriToPath(root_uri_);
  }
  IndexWorkspace();

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
  caps["textDocumentSync"] = JsonNode(std::move(sync));

  std::unordered_map<std::string, JsonNode> result;
  result["capabilities"] = JsonNode(std::move(caps));
  result["serverInfo"] = JsonNode(std::unordered_map<std::string, JsonNode>{
      {"name", JsonNode(std::string("lpc-lsp"))},
      {"version", JsonNode(std::string("1.0.0"))}});
  return JsonNode(std::move(result));
}

std::optional<ReferenceInfo>
SymbolIndex::ReferenceAtPosition(const std::string &uri,
                                 const Position &pos) const {
  auto it = uri_to_refs_.find(uri);
  if (it == uri_to_refs_.end())
    return std::nullopt;
  for (const auto &ref : it->second) {
    if (RangeContains(ref.range, pos)) {
      return ref;
    }
  }
  return std::nullopt;
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

  auto ref = symbol_index_.ReferenceAtPosition(uri, {line, character});
  auto defs = ref ? symbol_index_.FindDefinitionById(ref->symbol_id)
                  : symbol_index_.FindDefinition(word);
  if (defs.empty())
    return JsonNode();

  std::vector<JsonNode> locations;
  for (const auto &def : defs) {
    std::unordered_map<std::string, JsonNode> loc;
    loc["uri"] = JsonNode(def.uri);
    loc["range"] = JsonNode(RangeToJson(def.selection_range));
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

  auto ref_at_pos = symbol_index_.ReferenceAtPosition(uri, {line, character});
  if (ref_at_pos && ref_at_pos->symbol_id.find("#unresolved:") != std::string::npos)
    return JsonNode(std::vector<JsonNode>{});
  auto refs = ref_at_pos ? symbol_index_.FindReferencesById(ref_at_pos->symbol_id)
                         : symbol_index_.FindReferences(word);
  std::vector<JsonNode> locations;
  for (const auto &ref : refs) {
    std::unordered_map<std::string, JsonNode> loc;
    loc["uri"] = JsonNode(ref.uri);
    loc["range"] = JsonNode(RangeToJson(ref.range));
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

  auto ref_at_pos = symbol_index_.ReferenceAtPosition(uri, {line, character});
  if (ref_at_pos && ref_at_pos->symbol_id.find("#unresolved:") != std::string::npos)
    return JsonNode();
  auto refs = ref_at_pos ? symbol_index_.FindReferencesById(ref_at_pos->symbol_id)
                         : symbol_index_.FindReferences(word);
  if (refs.empty())
    return JsonNode();

  std::unordered_map<std::string, std::vector<JsonNode>> changes_by_uri;
  for (const auto &ref : refs) {
    std::unordered_map<std::string, JsonNode> text_edit;
    text_edit["range"] = JsonNode(RangeToJson(ref.range));
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

  auto ref = symbol_index_.ReferenceAtPosition(uri, {line, character});
  auto defs = ref ? symbol_index_.FindDefinitionById(ref->symbol_id)
                  : symbol_index_.FindDefinition(word);
  if (defs.empty())
    return JsonNode();

  std::string hover_text;
  for (const auto &def : defs) {
    hover_text += (def.detail.empty() ? (def.kind + " " + def.name) : def.detail) +
                  " (" + def.uri + ")\n";
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
    if (!sym.detail.empty())
      item["detail"] = JsonNode(sym.detail);
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
    s["range"] = JsonNode(RangeToJson(sym.range));
    s["selectionRange"] = JsonNode(RangeToJson(sym.selection_range));
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
  PublishDiagnostics(uri, text);
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
    PublishDiagnostics(uri, text);
  }
}

void LspServer::DidClose(const JsonNode &params) {
  std::string uri = params["textDocument"]["uri"].IsString()
                        ? params["textDocument"]["uri"].AsString()
                        : "";
  open_docs_.erase(uri);
  std::string text = ReadFileAll(UriToPath(uri));
  if (!text.empty()) {
    symbol_index_.IndexDocument(uri, text);
  } else {
    symbol_index_.RemoveDocument(uri);
  }
  ClearDiagnostics(uri);
}

void LspServer::IndexWorkspace() {
  if (root_path_.empty()) return;
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path root(root_path_);
  if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return;
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
       it != end && !ec; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != ".lpc") continue;
    std::string path = it->path().string();
    symbol_index_.IndexDocument(PathToUri(path), ReadFileAll(path));
  }
}

void LspServer::PublishDiagnostics(const std::string &uri, const std::string &text) {
  std::string path = UriToPath(uri);
  std::vector<std::string> include_dirs;
  if (!root_path_.empty()) include_dirs.push_back(root_path_);
  auto result = lpc::frontend::CompileSourceToMir(path, text, include_dirs);
  std::vector<JsonNode> diagnostics;
  for (const auto &d : result.diagnostics.All()) {
    int line = d.span.line > 0 ? d.span.line - 1 : 0;
    int col = d.span.column > 0 ? d.span.column - 1 : 0;
    int len = d.span.length > 0 ? d.span.length : 1;
    int severity = d.level == lpc::frontend::DiagnosticLevel::Error
                       ? 1
                       : d.level == lpc::frontend::DiagnosticLevel::Warning ? 2 : 3;
    Range r;
    r.start = {line, col};
    r.end = {line, col + len};
    diagnostics.push_back(JsonNode(std::unordered_map<std::string, JsonNode>{
        {"range", JsonNode(RangeToJson(r))},
        {"severity", JsonNode(severity)},
        {"source", JsonNode(std::string("lpc"))},
        {"message", JsonNode(d.message)}}));
  }
  SendNotification("textDocument/publishDiagnostics",
                   JsonNode(std::unordered_map<std::string, JsonNode>{
                       {"uri", JsonNode(uri)},
                       {"diagnostics", JsonNode(std::move(diagnostics))}}));
}

void LspServer::ClearDiagnostics(const std::string &uri) {
  SendNotification("textDocument/publishDiagnostics",
                   JsonNode(std::unordered_map<std::string, JsonNode>{
                       {"uri", JsonNode(uri)},
                       {"diagnostics", JsonNode(std::vector<JsonNode>{})}}));
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
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':')
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
  while (!exit_requested_ && ReadRpcMessage(in, msg)) {
    HandleMessage(msg);
    msg = JsonNode();
  }
}

} // namespace lsp
} // namespace lpc
