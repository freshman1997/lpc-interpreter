#include "lsp/json_rpc.h"
#include <cmath>
#include <cstring>
#include <cstdio>

namespace lpc {
namespace lsp {

static void SerializeValue(std::string &out, const JsonNode &node) {
    if (node.IsNull()) {
        out.append("null", 4);
    } else if (node.IsBool()) {
        if (node.AsBool()) out.append("true", 4);
        else out.append("false", 5);
    } else if (node.IsNumber()) {
        double d = node.AsNumber();
        if (std::isfinite(d) && d == std::floor(d) && std::abs(d) < 1e15) {
            char buf[32];
            int n = std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
            out.append(buf, static_cast<std::size_t>(n));
        } else {
            char buf[64];
            int n = std::snprintf(buf, sizeof(buf), "%.17g", d);
            out.append(buf, static_cast<std::size_t>(n));
        }
    } else if (node.IsString()) {
        const std::string &s = node.AsString();
        out.push_back('"');
        for (char c : s) {
            switch (c) {
                case '"':  out.append("\\\"", 2); break;
                case '\\': out.append("\\\\", 2); break;
                case '\b': out.append("\\b", 2); break;
                case '\f': out.append("\\f", 2); break;
                case '\n': out.append("\\n", 2); break;
                case '\r': out.append("\\r", 2); break;
                case '\t': out.append("\\t", 2); break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char hex[8];
                        int n = std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned char>(c));
                        out.append(hex, static_cast<std::size_t>(n));
                    } else {
                        out.push_back(c);
                    }
                    break;
            }
        }
        out.push_back('"');
    } else if (node.IsArray()) {
        out.push_back('[');
        const auto &arr = node.AsArray();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) out.push_back(',');
            SerializeValue(out, arr[i]);
        }
        out.push_back(']');
    } else if (node.IsObject()) {
        out.push_back('{');
        const auto &obj = node.AsObject();
        bool first = true;
        for (auto &[key, val] : obj) {
            if (!first) out.push_back(',');
            first = false;
            out.push_back('"');
            for (char c : key) {
                if (c == '"') out.append("\\\"", 2);
                else if (c == '\\') out.append("\\\\", 2);
                else out.push_back(c);
            }
            out.append("\":", 2);
            SerializeValue(out, val);
        }
        out.push_back('}');
    }
}

std::string JsonSerialize(const JsonNode &node) {
    std::string out;
    out.reserve(256);
    SerializeValue(out, node);
    return out;
}

class JsonParser {
public:
    explicit JsonParser(const std::string &text) : text_(text), pos_(0) {}

    JsonNode Parse() {
        SkipWhitespace();
        auto node = ParseValue();
        SkipWhitespace();
        return node;
    }

private:
    char Peek() { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    char Advance() { return pos_ < text_.size() ? text_[pos_++] : '\0'; }

    void SkipWhitespace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
               text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    void Expect(char c) {
        SkipWhitespace();
        if (Peek() != c) return;
        Advance();
    }

    JsonNode ParseValue() {
        SkipWhitespace();
        char c = Peek();
        if (c == '"') return ParseString();
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == 't' || c == 'f') return ParseBool();
        if (c == 'n') return ParseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
        return JsonNode();
    }

    JsonNode ParseString() {
        Advance();
        std::size_t start = pos_;
        while (pos_ < text_.size() && text_[pos_] != '"' && text_[pos_] != '\\') ++pos_;
        if (pos_ < text_.size() && text_[pos_] == '"') {
            std::string result(text_, start, pos_ - start);
            ++pos_;
            return JsonNode(std::move(result));
        }
        std::string result;
        result.append(text_, start, pos_ - start);
        while (pos_ < text_.size() && text_[pos_] != '"') {
            if (text_[pos_] == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) break;
                switch (text_[pos_]) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        ++pos_;
                        if (pos_ + 4 > text_.size()) break;
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = text_[pos_ + i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp += h - '0';
                            else if (h >= 'a' && h <= 'f') cp += 10 + h - 'a';
                            else if (h >= 'A' && h <= 'F') cp += 10 + h - 'A';
                        }
                        pos_ += 3;
                        if (cp < 0x80) {
                            result += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            result += static_cast<char>(0xC0 | (cp >> 6));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | (cp >> 12));
                            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: result += text_[pos_]; break;
                }
                ++pos_;
            } else {
                result += text_[pos_];
                ++pos_;
            }
        }
        if (pos_ < text_.size()) ++pos_;
        return JsonNode(result);
    }

    JsonNode ParseNumber() {
        bool neg = false;
        if (Peek() == '-') { neg = true; ++pos_; }
        double val = 0;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
            val = val * 10 + (text_[pos_++] - '0');
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            double frac = 0, div = 1;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                frac = frac * 10 + (text_[pos_++] - '0');
                div *= 10;
            }
            val += frac / div;
        }
        if (neg) val = -val;
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            bool eneg = false;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                eneg = (text_[pos_] == '-');
                ++pos_;
            }
            int exp = 0;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
                exp = exp * 10 + (text_[pos_++] - '0');
            if (eneg) exp = -exp;
            val *= std::pow(10.0, exp);
        }
        return JsonNode(val);
    }

    JsonNode ParseBool() {
        if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; return JsonNode(true); }
        if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; return JsonNode(false); }
        return JsonNode();
    }

    JsonNode ParseNull() {
        if (text_.compare(pos_, 4, "null") == 0) { pos_ += 4; }
        return JsonNode();
    }

    JsonNode ParseArray() {
        Advance();
        std::vector<JsonNode> items;
        SkipWhitespace();
        if (Peek() == ']') { Advance(); return JsonNode(std::move(items)); }
        while (true) {
            SkipWhitespace();
            items.push_back(ParseValue());
            SkipWhitespace();
            if (Peek() == ',') { Advance(); continue; }
            if (Peek() == ']') { Advance(); break; }
            break;
        }
        return JsonNode(std::move(items));
    }

    JsonNode ParseObject() {
        Advance();
        std::unordered_map<std::string, JsonNode> obj;
        SkipWhitespace();
        if (Peek() == '}') { Advance(); return JsonNode(std::move(obj)); }
        while (true) {
            SkipWhitespace();
            auto key = ParseString();
            SkipWhitespace();
            Expect(':');
            SkipWhitespace();
            auto val = ParseValue();
            obj[key.AsString()] = std::move(val);
            SkipWhitespace();
            if (Peek() == ',') { Advance(); continue; }
            if (Peek() == '}') { Advance(); break; }
            break;
        }
        return JsonNode(std::move(obj));
    }

    const std::string &text_;
    std::size_t pos_;
};

JsonNode JsonParse(const std::string &text) {
    JsonParser parser(text);
    return parser.Parse();
}

std::string EncodeRpcMessage(const JsonNode &msg) {
    std::string body = JsonSerialize(msg);
    std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    return header + body;
}

bool ReadRpcMessage(std::istream &in, JsonNode &out_msg) {
    std::string line;
    std::size_t content_length = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        if (line.compare(0, 16, "Content-Length: ") == 0) {
            content_length = std::stoul(line.substr(16));
        }
    }
    if (content_length == 0) return false;
    std::string body(content_length, '\0');
    in.read(body.data(), static_cast<std::streamsize>(content_length));
    if (in.gcount() != static_cast<std::streamsize>(content_length)) return false;
    out_msg = JsonParse(body);
    return true;
}

JsonNode RequestToJson(const JsonRpcRequest &req) {
    std::unordered_map<std::string, JsonNode> obj;
    obj["jsonrpc"] = JsonNode(std::string("2.0"));
    if (std::holds_alternative<int>(req.id)) {
        obj["id"] = JsonNode(std::get<int>(req.id));
    } else if (std::holds_alternative<std::string>(req.id)) {
        obj["id"] = JsonNode(std::get<std::string>(req.id));
    }
    obj["method"] = JsonNode(req.method);
    if (!req.params.IsNull()) {
        obj["params"] = req.params;
    }
    return JsonNode(std::move(obj));
}

JsonNode ResponseToJson(const JsonRpcResponse &resp) {
    std::unordered_map<std::string, JsonNode> obj;
    obj["jsonrpc"] = JsonNode(std::string("2.0"));
    if (std::holds_alternative<int>(resp.id)) {
        obj["id"] = JsonNode(std::get<int>(resp.id));
    } else if (std::holds_alternative<std::string>(resp.id)) {
        obj["id"] = JsonNode(std::get<std::string>(resp.id));
    }
    if (resp.has_error) {
        obj["error"] = resp.error;
    } else {
        obj["result"] = resp.result;
    }
    return JsonNode(std::move(obj));
}

} // namespace lsp
} // namespace lpc
