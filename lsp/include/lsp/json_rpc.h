#ifndef LPC_LSP_JSON_RPC_H
#define LPC_LSP_JSON_RPC_H

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <sstream>
#include <cstdint>

namespace lpc {
namespace lsp {

struct JsonNull {};

using JsonValue = std::variant<JsonNull, bool, double, std::string,
    std::vector<struct JsonNode>,
    std::unordered_map<std::string, struct JsonNode>>;

struct JsonNode {
    JsonValue val;

    JsonNode() : val(JsonNull{}) {}
    explicit JsonNode(JsonNull n) : val(n) {}
    explicit JsonNode(bool b) : val(b) {}
    explicit JsonNode(double d) : val(d) {}
    explicit JsonNode(int i) : val(static_cast<double>(i)) {}
    explicit JsonNode(std::int64_t i) : val(static_cast<double>(i)) {}
    explicit JsonNode(const std::string &s) : val(s) {}
    explicit JsonNode(const char *s) : val(std::string(s)) {}
    explicit JsonNode(std::vector<JsonNode> a) : val(std::move(a)) {}
    explicit JsonNode(std::unordered_map<std::string, JsonNode> o) : val(std::move(o)) {}

    bool IsNull() const { return std::holds_alternative<JsonNull>(val); }
    bool IsBool() const { return std::holds_alternative<bool>(val); }
    bool IsNumber() const { return std::holds_alternative<double>(val); }
    bool IsString() const { return std::holds_alternative<std::string>(val); }
    bool IsArray() const { return std::holds_alternative<std::vector<JsonNode>>(val); }
    bool IsObject() const { return std::holds_alternative<std::unordered_map<std::string, JsonNode>>(val); }

    bool AsBool() const { return std::get<bool>(val); }
    double AsNumber() const { return std::get<double>(val); }
    int AsInt() const { return static_cast<int>(std::get<double>(val)); }
    std::int64_t AsInt64() const { return static_cast<std::int64_t>(std::get<double>(val)); }
    const std::string &AsString() const { return std::get<std::string>(val); }
    const std::vector<JsonNode> &AsArray() const { return std::get<std::vector<JsonNode>>(val); }
    const std::unordered_map<std::string, JsonNode> &AsObject() const { return std::get<std::unordered_map<std::string, JsonNode>>(val); }

    const JsonNode &operator[](const std::string &key) const {
        static const JsonNode null_node;
        if (!IsObject()) return null_node;
        auto &obj = AsObject();
        auto it = obj.find(key);
        return it != obj.end() ? it->second : null_node;
    }

    const JsonNode &operator[](std::size_t idx) const {
        static const JsonNode null_node;
        if (!IsArray()) return null_node;
        auto &arr = AsArray();
        return idx < arr.size() ? arr[idx] : null_node;
    }

    std::size_t ArraySize() const {
        return IsArray() ? AsArray().size() : 0;
    }

    bool Has(const std::string &key) const {
        if (!IsObject()) return false;
        return AsObject().count(key) > 0;
    }
};

std::string JsonSerialize(const JsonNode &node);
JsonNode JsonParse(const std::string &text);

struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    std::variant<int, std::string> id;
    std::string method;
    JsonNode params;
    bool is_notification = false;
};

struct JsonRpcResponse {
    std::variant<int, std::string> id;
    JsonNode result;
    JsonNode error;
    bool has_error = false;
};

std::string EncodeRpcMessage(const JsonNode &msg);
bool ReadRpcMessage(std::istream &in, JsonNode &out_msg);
JsonNode RequestToJson(const JsonRpcRequest &req);
JsonNode ResponseToJson(const JsonRpcResponse &resp);

} // namespace lsp
} // namespace lpc

#endif
