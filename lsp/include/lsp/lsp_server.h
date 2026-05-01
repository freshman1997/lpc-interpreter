#ifndef LPC_LSP_SERVER_H
#define LPC_LSP_SERVER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>
#include <iostream>

#include "lsp/json_rpc.h"

namespace lpc {
namespace lsp {

struct Position {
    int line = 0;
    int character = 0;
};

struct Range {
    Position start;
    Position end;
};

struct Location {
    std::string uri;
    Range range;
};

struct SymbolInfo {
    std::string name;
    std::string kind;
    std::string symbol_id;
    std::string uri;
    Range range;
    Range selection_range;
    std::string container_name;
    std::string detail;
};

struct ReferenceInfo {
    std::string name;
    std::string symbol_id;
    std::string uri;
    Range range;
};

struct DocumentVersion {
    int version = 0;
    std::string text;
};

class SymbolIndex {
public:
    void IndexDocument(const std::string &uri, const std::string &text);
    void RemoveDocument(const std::string &uri);
    std::vector<SymbolInfo> FindDefinition(const std::string &name) const;
    std::vector<SymbolInfo> FindDefinitionById(const std::string &symbol_id) const;
    std::vector<Location> FindReferences(const std::string &name) const;
    std::vector<Location> FindReferencesById(const std::string &symbol_id) const;
    std::vector<Location> FindReferencesInUri(const std::string &name, const std::string &uri) const;
    std::vector<std::pair<std::string, std::string>> Rename(const std::string &old_name, const std::string &new_name) const;
    std::vector<SymbolInfo> AllSymbols() const;
    std::optional<SymbolInfo> SymbolAtPosition(const std::string &uri, const Position &pos) const;
    std::optional<ReferenceInfo> ReferenceAtPosition(const std::string &uri, const Position &pos) const;

private:
    void AddSymbol(const SymbolInfo &sym);
    void AddReference(const ReferenceInfo &ref);
    void ParseReferences(const std::string &uri, const std::string &text);
    void ParseDocument(const std::string &uri, const std::string &text);
    std::unordered_map<std::string, std::vector<SymbolInfo>> name_to_symbols_;
    std::unordered_map<std::string, std::vector<SymbolInfo>> id_to_symbols_;
    std::unordered_map<std::string, std::vector<SymbolInfo>> uri_to_symbols_;
    std::unordered_map<std::string, std::vector<ReferenceInfo>> name_to_refs_;
    std::unordered_map<std::string, std::vector<ReferenceInfo>> id_to_refs_;
    std::unordered_map<std::string, std::vector<ReferenceInfo>> uri_to_refs_;
};

class LspServer {
public:
    using SendMessageFn = std::function<void(const std::string &)>;

    explicit LspServer(SendMessageFn send_fn);
    ~LspServer() = default;

    void HandleMessage(const JsonNode &message);
    void Run(std::istream &in);

    const SymbolIndex &symbol_index() const { return symbol_index_; }

private:
    void HandleRequest(const JsonNode &id, const std::string &method, const JsonNode &params);
    void HandleNotification(const std::string &method, const JsonNode &params);

    JsonNode Initialize(const JsonNode &params);
    JsonNode Shutdown();
    JsonNode TextDocumentDefinition(const JsonNode &params);
    JsonNode TextDocumentReferences(const JsonNode &params);
    JsonNode TextDocumentRename(const JsonNode &params);
    JsonNode TextDocumentHover(const JsonNode &params);
    JsonNode TextDocumentCompletion(const JsonNode &params);
    JsonNode TextDocumentDocumentSymbol(const JsonNode &params);
    JsonNode WorkspaceSymbol(const JsonNode &params);

    void IndexWorkspace();
    void PublishDiagnostics(const std::string &uri, const std::string &text);
    void ClearDiagnostics(const std::string &uri);
    void DidOpen(const JsonNode &params);
    void DidChange(const JsonNode &params);
    void DidClose(const JsonNode &params);

    void SendResponse(const JsonNode &id, const JsonNode &result);
    void SendError(const JsonNode &id, int code, const std::string &message);
    void SendNotification(const std::string &method, const JsonNode &params);

    std::string UriToPath(const std::string &uri) const;
    std::string PathToUri(const std::string &path) const;

    SendMessageFn send_fn_;
    SymbolIndex symbol_index_;
    std::unordered_map<std::string, DocumentVersion> open_docs_;
    bool initialized_ = false;
    bool shutdown_ = false;
    std::string root_uri_;
    std::string root_path_;
};

} // namespace lsp
} // namespace lpc

#endif
