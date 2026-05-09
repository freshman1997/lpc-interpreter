#include "cli/dap_server.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fcntl.h>
#include <io.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "vm/runtime/vm.h"
#include "vm/runtime/debugger.h"

namespace lpc {
namespace vm {

#ifdef _WIN32
using DapSocket = SOCKET;
static constexpr DapSocket kInvalidDapSocket = INVALID_SOCKET;
static void CloseDapSocket(DapSocket fd) { closesocket(fd); }
static bool InitDapSockets() {
    static bool initialized = false;
    if (initialized) return true;
    WSADATA wsa;
    initialized = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    return initialized;
}
#else
using DapSocket = int;
static constexpr DapSocket kInvalidDapSocket = -1;
static void CloseDapSocket(DapSocket fd) { close(fd); }
static bool InitDapSockets() { return true; }
#endif

static std::string EscapeJson(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

static void DapLog(const std::string &message) {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec) / "log";
    if (!ec) {
        std::filesystem::create_directories(dir, ec);
        std::ofstream out((dir / "lpc_dap_debug.log").string(), std::ios::app);
        if (out.is_open()) {
            out << message << std::endl;
        }
    }
}

static std::string ExtractString(const std::string &json, const std::string &key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return "";
    if (json[pos] == '"') {
        pos++;
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                char next = json[pos + 1];
                if (next == '"' || next == '\\') { result += next; pos += 2; }
                else if (next == 'n') { result += '\n'; pos += 2; }
                else if (next == 'r') { result += '\r'; pos += 2; }
                else if (next == 't') { result += '\t'; pos += 2; }
                else { result += json[pos]; pos++; }
            } else { result += json[pos]; pos++; }
            }
        return result;
    }
    std::string result;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') {
        result += json[pos]; pos++;
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t')) result.pop_back();
    return result;
}

static int ExtractInt(const std::string &json, const std::string &key, int default_val = 0) {
    std::string val = ExtractString(json, key);
    if (val.empty()) return default_val;
    return std::atoi(val.c_str());
}

static bool ExtractBool(const std::string &json, const std::string &key, bool default_val = false) {
    std::string val = ExtractString(json, key);
    if (val == "true") return true;
    if (val == "false") return false;
    return default_val;
}

static std::string ExtractObject(const std::string &json, const std::string &key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "{}";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "{}";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '{') return "{}";
    int depth = 0;
    std::size_t start = pos;
    while (pos < json.size()) {
        if (json[pos] == '{') depth++;
        else if (json[pos] == '}') { depth--; if (depth == 0) break; }
        else if (json[pos] == '"') { pos++; while (pos < json.size() && json[pos] != '"') { if (json[pos] == '\\') pos++; pos++; } }
        pos++;
    }
    return json.substr(start, pos - start + 1);
}

static std::string ExtractArray(const std::string &json, const std::string &key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "[]";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "[]";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '[') return "[]";
    int depth = 0;
    std::size_t start = pos;
    while (pos < json.size()) {
        if (json[pos] == '[') depth++;
        else if (json[pos] == ']') { depth--; if (depth == 0) break; }
        else if (json[pos] == '"') { pos++; while (pos < json.size() && json[pos] != '"') { if (json[pos] == '\\') pos++; pos++; } }
        pos++;
    }
    return json.substr(start, pos - start + 1);
}

static std::vector<std::string> SplitArrayItems(const std::string &array_json) {
    std::vector<std::string> items;
    if (array_json.size() < 2 || array_json[0] != '[') return items;
    int depth = 0;
    std::size_t start = 1;
    bool in_string = false;
    for (std::size_t i = 1; i < array_json.size(); ++i) {
        char c = array_json[i];
        if (in_string) {
            if (c == '\\' && i + 1 < array_json.size()) { i++; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            depth--;
            if (depth < 0) {
                if (i > start) items.push_back(array_json.substr(start, i - start));
                break;
            }
        }
        if (c == ',' && depth == 0) {
            if (i > start) items.push_back(array_json.substr(start, i - start));
            start = i + 1;
        }
    }
    return items;
}

struct BreakpointSpec {
    int line = 0;
    std::string condition;
};

struct SourceBreakpoints {
    std::string path;
    std::vector<BreakpointSpec> specs;
};

struct FrameView {
    DebugFrame frame;
    std::string source_path;
    std::string normalized_source_path;
    int source_ref = 0;
};

static std::string NormalizePath(const std::string &p) {
    std::string result = p;
    if (!result.empty()) {
        std::error_code ec;
        std::filesystem::path path(result);
        if (path.is_relative()) {
            path = std::filesystem::current_path(ec) / path;
        }
        if (!ec) {
            std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
            if (!ec) {
                result = canonical.string();
            } else {
                result = path.lexically_normal().string();
            }
        }
    }
    for (auto &c : result) {
        if (c == '\\') c = '/';
    }
#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
#endif
    return result;
}

static std::string ClientSourcePath(const std::string &p) {
    if (p.empty()) return "";

    std::error_code ec;
    std::filesystem::path path(p);
    if (path.is_relative()) {
        path = std::filesystem::current_path(ec) / path;
    }
    if (!ec) {
        std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            return canonical.string();
        }
        return path.lexically_normal().string();
    }
    return p;
}

static std::string ClientSourceName(const std::string &p) {
    if (p.empty()) return "";
    std::filesystem::path path(p);
    std::string name = path.filename().string();
    return name.empty() ? p : name;
}

class DapServer {
public:
    DapServer() : DapServer(kInvalidDapSocket, false) {}
    DapServer(DapSocket io_socket, bool attach_mode)
        : vm_(nullptr), seq_(1), running_(true), vm_paused_(false), config_done_(false),
          launched_(false), vm_finished_(false), attach_mode_(attach_mode),
          io_socket_(io_socket), next_var_ref_(100), break_on_exceptions_(false) {}

    void Run() {
        while (running_) {
            std::string content = ReadMessage();
            if (content.empty()) {
                running_ = false;
                break;
            }
            HandleMessage(content);
        }
        vm_paused_ = false;
        resume_cv_.notify_all();
        if (vm_thread_.joinable()) {
            vm_thread_.join();
        }
        if (attach_mode_ && vm_) {
            vm_->set_debug_hook({});
            vm_->debugger().set_active(false);
        }
        if (vm_) {
            vm_->set_output_hook({});
        }
        while (active_breaks_.load() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void SetVm(Vm *vm) { vm_ = vm; }
    void SetEntryFunction(const std::string &entry_function) { entry_function_ = entry_function; }

    void StartVmThread() {
        vm_thread_ = std::thread([this]() {
            Vm *vm = vm_;
            if (!vm) return;
            DapLog("[vm] RunEntry(" + entry_function_ + ") begin");
            RuntimeError e = vm->RunEntry(entry_function_.c_str());
            std::lock_guard<std::mutex> lock(state_mutex_);
            vm_finished_ = true;
            if (e.ok()) {
                DapLog("[vm] terminated normally");
                SendEvent("terminated", "{}");
            } else {
                DapLog("[vm] stopped with error: " + e.message);
                std::string body = "{\"reason\":\"exception\",\"threadId\":1,\"description\":\"" + EscapeJson(e.message) + "\"}";
                SendEvent("stopped", body);
            }
        });
    }

    RuntimeError OnVmBreak(std::uint32_t pc) {
        active_breaks_.fetch_add(1);
        struct BreakGuard {
            DapServer *server;
            ~BreakGuard() { server->active_breaks_.fetch_sub(1); }
        } break_guard{this};

        Debugger &dbg = vm_->debugger();
        const Chunk &chunk = vm_->chunk();
        const std::vector<Frame> &frames = vm_->frames();
        auto chunk_resolver = [this](std::uint64_t version_id) -> const Chunk * {
            return vm_ ? vm_->GetChunkForVersion(version_id) : nullptr;
        };

        auto bt = dbg.GetBacktrace(chunk, frames, chunk_resolver);
        std::string source_path = dbg.SourcePath(chunk);

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            cached_frames_.clear();
            cached_frames_.reserve(bt.size());
            for (std::size_t i = 0; i < bt.size(); ++i) {
                FrameView view;
                view.frame = bt[i];
                std::string src_path = bt[i].source_path.empty() ? source_path : bt[i].source_path;
                view.source_path = ClientSourcePath(src_path);
                view.normalized_source_path = NormalizePath(src_path);
                if (!view.source_path.empty() && !std::filesystem::exists(view.source_path)) {
                    view.source_ref = static_cast<int>(i) + 100;
                }
                cached_frames_.push_back(std::move(view));
            }
            cached_locals_.clear();
            cached_args_.clear();
            cached_globals_.clear();
        }

        std::string frames_json = "[";
        for (std::size_t i = 0; i < bt.size(); ++i) {
            if (i > 0) frames_json += ",";
            int frame_id = static_cast<int>(i) + 1000;
            const FrameView &view = cached_frames_[i];
            const DebugFrame &f = view.frame;
            frames_json += "{\"id\":" + std::to_string(frame_id)
                + ",\"name\":\"" + EscapeJson(f.name) + "\""
                + ",\"line\":" + std::to_string(f.source_line)
                + ",\"column\":1"
                + ",\"moduleName\":\"" + EscapeJson(f.module_name) + "\""
                + ",\"moduleVersion\":" + std::to_string(f.module_version_id)
                + ",\"source\":{\"name\":\"" + EscapeJson(ClientSourceName(view.source_path)) + "\""
                + ",\"path\":\"" + EscapeJson(view.source_path) + "\""
                + ",\"sourceReference\":" + std::to_string(view.source_ref) + "}}";
        }
        frames_json += "]";

        int top_line = bt.empty() ? 1 : bt[0].line;
        if (!bt.empty()) {
            DapLog("[stop] source=" + bt[0].source_path
                + " source_line=" + std::to_string(bt[0].source_line)
                + " output_line=" + std::to_string(bt[0].line)
                + " pc=" + std::to_string(bt[0].pc));
        }

        bool is_exception = dbg.last_break_exception();
        if (is_exception) dbg.set_last_break_exception(false);

        std::string reason = is_exception ? "exception" : "breakpoint";
        std::uint64_t top_module_version = bt.empty() ? 0 : bt[0].module_version_id;
        std::string top_module_name = bt.empty() ? "" : bt[0].module_name;
        std::string body = "{\"reason\":\"" + reason + "\",\"threadId\":1,\"allThreadsStopped\":true"
            + std::string(",\"hitBreakpointIds\":[]")
            + ",\"moduleName\":\"" + EscapeJson(top_module_name) + "\""
            + ",\"moduleVersion\":" + std::to_string(top_module_version)
            + "}";

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (last_active_version_ != 0 && last_active_version_ != top_module_version) {
                std::string mv_body = "{\"moduleVersion\":" + std::to_string(top_module_version)
                    + ",\"previousVersion\":" + std::to_string(last_active_version_) + "}";
                SendEvent("moduleVersionChange", mv_body);
            }
            last_active_version_ = top_module_version;
        }

        {
            std::lock_guard<std::mutex> lock(send_mutex_);
            SendEvent("stopped", body);
        }

        {
            std::unique_lock<std::mutex> lock(resume_mutex_);
            vm_paused_ = true;
            resume_cv_.wait(lock, [this] { return !vm_paused_ || !running_ || vm_finished_; });
        }

        if (!running_) {
            return RuntimeError::Error(RuntimeErrorCode::InternalError, "debugger quit");
        }
        return RuntimeError::Ok();
    }

private:
    Vm *vm_;
    int seq_;
    bool running_;
    bool vm_paused_;
    bool config_done_;
    bool launched_;
    bool vm_finished_;
    bool attach_mode_;
    DapSocket io_socket_;
    std::atomic<int> active_breaks_{0};
    std::mutex state_mutex_;
    std::mutex send_mutex_;
    std::mutex resume_mutex_;
    std::condition_variable resume_cv_;
    std::thread vm_thread_;
    std::vector<FrameView> cached_frames_;
    std::vector<DebugVariable> cached_locals_;
    std::vector<DebugVariable> cached_args_;
    std::vector<DebugVariable> cached_globals_;
    std::vector<Value> cached_local_values_;
    std::vector<Value> cached_arg_values_;
    std::vector<Value> cached_global_values_;
    std::map<std::string, SourceBreakpoints> source_breakpoints_;
    std::map<int, Value> expandable_vars_;
    std::map<int, std::string> source_refs_;
    int next_var_ref_;
    std::string entry_function_ = "main";
    bool break_on_exceptions_;
    std::uint64_t last_active_version_ = 0;

    bool ReadByte(char *out) {
        if (io_socket_ == kInvalidDapSocket) {
            return static_cast<bool>(std::cin.get(*out));
        }
        int n = recv(io_socket_, out, 1, 0);
        return n == 1;
    }

    bool ReadBytes(char *out, int len) {
        if (io_socket_ == kInvalidDapSocket) {
            std::cin.read(out, len);
            return std::cin.gcount() == len;
        }
        int total = 0;
        while (total < len) {
            int n = recv(io_socket_, out + total, len - total, 0);
            if (n <= 0) return false;
            total += n;
        }
        return true;
    }

    bool WriteBytes(const std::string &data) {
        if (io_socket_ == kInvalidDapSocket) {
            std::cout << data << std::flush;
            return true;
        }
        const char *buf = data.data();
        int total = 0;
        int len = static_cast<int>(data.size());
        while (total < len) {
            int n = send(io_socket_, buf + total, len - total, 0);
            if (n <= 0) return false;
            total += n;
        }
        return true;
    }

    std::string ReadMessage() {
        int content_length = -1;
        std::string header_line;
        char ch = '\0';
        while (ReadByte(&ch)) {
            if (ch != '\n') {
                header_line.push_back(ch);
                continue;
            }
            while (!header_line.empty() && (header_line.back() == '\r' || header_line.back() == '\n'))
                header_line.pop_back();
            if (header_line.empty()) {
                if (content_length >= 0) break;
                continue;
            }
            auto colon = header_line.find(':');
            if (colon != std::string::npos) {
                std::string key = header_line.substr(0, colon);
                while (!key.empty() && key.back() == ' ') key.pop_back();
                if (key == "Content-Length") {
                    content_length = std::atoi(header_line.substr(colon + 1).c_str());
                }
            }
            header_line.clear();
        }
        if (content_length <= 0) return "";
        std::string content(content_length, '\0');
        if (!ReadBytes(&content[0], content_length)) return "";
        return content;
    }

    void SendMessage(const std::string &json) {
        WriteBytes("Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json);
    }

    void SendResponse(int request_seq, const std::string &command, bool success,
                       const std::string &body = "{}", const std::string &message = "") {
        std::ostringstream out;
        out << "{\"type\":\"response\",\"seq\":" << seq_++
            << ",\"request_seq\":" << request_seq
            << ",\"command\":\"" << command << "\""
            << ",\"success\":" << (success ? "true" : "false");
        if (!message.empty()) {
            out << ",\"message\":\"" << EscapeJson(message) << "\"";
        }
        out << ",\"body\":" << body << "}";
        std::lock_guard<std::mutex> lock(send_mutex_);
        SendMessage(out.str());
    }

    void SendEvent(const std::string &name, const std::string &body = "{}") {
        std::ostringstream out;
        out << "{\"type\":\"event\",\"seq\":" << seq_++
            << ",\"event\":\"" << name << "\""
            << ",\"body\":" << body << "}";
        SendMessage(out.str());
    }

    void SendOutput(const std::string &text) {
        std::ostringstream body;
        body << "{\"category\":\"stdout\",\"output\":\"" << EscapeJson(text) << "\"}";
        std::lock_guard<std::mutex> lock(send_mutex_);
        SendEvent("output", body.str());
    }

    void HandleMessage(const std::string &content) {
        int seq = ExtractInt(content, "seq", 0);
        std::string type = ExtractString(content, "type");
        std::string command = ExtractString(content, "command");

        if (type != "request" || command.empty()) return;
        DapLog("[request] " + command);

        if (command == "initialize") {
            std::string body = "{\"supportsConfigurationDoneRequest\":true"
                ",\"supportsConditionalBreakpoints\":true"
                ",\"supportsEvaluateForHovers\":true"
                ",\"supportsCancelRequest\":true"
                ",\"exceptionBreakpointFilters\":["
                "{\"filter\":\"runtime\",\"label\":\"Runtime Errors\",\"default\":true}"
                "]}"
                ;
            SendResponse(seq, command, true, body);
            SendEvent("initialized");
            return;
        }
        if (command == "launch") { HandleLaunch(seq, content); return; }
        if (command == "attach") { HandleAttach(seq, content); return; }
        if (command == "setBreakpoints") { HandleSetBreakpoints(seq, content); return; }
        if (command == "setFunctionBreakpoints") { SendResponse(seq, command, false, "{}", "not supported"); return; }
        if (command == "setExceptionBreakpoints") {
            break_on_exceptions_ = content.find("\"runtime\"") != std::string::npos;
            if (vm_) vm_->debugger().set_break_on_exceptions(break_on_exceptions_);
            SendResponse(seq, command, true, "{\"breakpoints\":[]}");
            return;
        }
        if (command == "configurationDone") {
            config_done_ = true;
            SendResponse(seq, command, true);
            if (!launched_ && !attach_mode_) {
                DapLog("[launch] missing launch request; using default launch");
                PrepareLaunch(false);
                SendEvent("thread", "{\"reason\":\"started\",\"threadId\":1}");
            }
            FinishLaunchConfig();
            return;
        }
        if (command == "threads") {
            SendResponse(seq, command, true, "{\"threads\":[{\"id\":1,\"name\":\"LPC VM\"}]}");
            return;
        }
        if (command == "stackTrace") { HandleStackTrace(seq, content); return; }
        if (command == "scopes") { HandleScopes(seq, content); return; }
        if (command == "variables") { HandleVariables(seq, content); return; }
        if (command == "continue") {
            SendResponse(seq, command, true, "{\"allThreadsContinued\":true}");
            ResumeVm(StepMode::Continue);
            return;
        }
        if (command == "next") {
            SendResponse(seq, command, true);
            ResumeVm(StepMode::StepOver);
            return;
        }
        if (command == "stepIn") {
            SendResponse(seq, command, true);
            ResumeVm(StepMode::StepInto);
            return;
        }
        if (command == "stepOut") {
            SendResponse(seq, command, true);
            ResumeVm(StepMode::StepOut);
            return;
        }
        if (command == "pause") {
            SendResponse(seq, command, true);
            if (vm_) vm_->debugger().SetStepMode(StepMode::StepInto, 0);
            return;
        }
        if (command == "evaluate") { HandleEvaluate(seq, content); return; }
        if (command == "disconnect") {
            SendResponse(seq, command, true);
            running_ = false;
            vm_paused_ = false;
            resume_cv_.notify_all();
            return;
        }
        if (command == "cancel") { SendResponse(seq, command, true); return; }
        if (command == "source") { HandleSource(seq, content); return; }
        SendResponse(seq, command, false, "{}", "unknown command");
    }

    void HandleLaunch(int seq, const std::string &content) {
        if (attach_mode_) {
            SendResponse(seq, "launch", false, "{}", "this DAP server is waiting for attach");
            return;
        }
        bool stop_on_entry = ExtractBool(content, "stopOnEntry", true);
        ExtractString(content, "entryModule");
        DapLog(std::string("[launch] stopOnEntry=") + (stop_on_entry ? "true" : "false"));
        PrepareLaunch(stop_on_entry);

        SendResponse(seq, "launch", true);
        SendEvent("thread", "{\"reason\":\"started\",\"threadId\":1}");

        if (config_done_) {
            FinishLaunchConfig();
        }
    }

    void HandleAttach(int seq, const std::string &content) {
        bool stop_on_attach = ExtractBool(content, "stopOnAttach", false);
        DapLog(std::string("[attach] stopOnAttach=") + (stop_on_attach ? "true" : "false"));
        PrepareLaunch(stop_on_attach);
        SendResponse(seq, "attach", true);
        SendEvent("thread", "{\"reason\":\"started\",\"threadId\":1}");

        if (config_done_) {
            FinishLaunchConfig();
        }
    }

    void PrepareLaunch(bool stop_on_entry) {
        launched_ = true;
        if (!vm_) return;

        Debugger &dbg = vm_->debugger();
        dbg.set_active(true);
        vm_->set_output_hook([this](const std::string &text) {
            SendOutput(text);
        });
        if (stop_on_entry) {
            dbg.SetStepMode(StepMode::StepInto, 0);
        } else {
            dbg.SetStepMode(StepMode::Continue, 0);
        }

        vm_->set_debug_hook([this](std::uint32_t pc) -> RuntimeError {
            return this->OnVmBreak(pc);
        });
    }

    void FinishLaunchConfig() {
        if (!launched_ || !config_done_) return;

        DapLog("[launch] configurationDone; applying breakpoints");
        ApplyBreakpoints();

        if (vm_ && !attach_mode_) {
            StartVmThread();
        }

        config_done_ = false;
    }

    void HandleSetBreakpoints(int seq, const std::string &content) {
        std::string source_json = ExtractObject(content, "source");
        std::string source_path = ExtractString(source_json, "path");
        std::string bp_array = ExtractArray(content, "breakpoints");

        SourceBreakpoints sbp;
        sbp.path = NormalizePath(source_path);
        DapLog("[setBreakpoints] source=" + source_path + " normalized=" + sbp.path);

        auto bp_items = SplitArrayItems(bp_array);
        for (const auto &item : bp_items) {
            BreakpointSpec spec;
            spec.line = ExtractInt(item, "line", 0);
            spec.condition = ExtractString(item, "condition");
            sbp.specs.push_back(spec);
            DapLog("[setBreakpoints] requested line=" + std::to_string(spec.line));
        }

        source_breakpoints_[sbp.path] = sbp;

        Debugger &dbg = vm_->debugger();
        const Chunk &chunk = vm_->chunk();

        std::string bp_results = "[";
        for (std::size_t i = 0; i < sbp.specs.size(); ++i) {
            if (i > 0) bp_results += ",";
            int src_line = sbp.specs[i].line;
            bool verified = false;
            int actual_line = src_line;

            std::vector<int> output_lines;
            for (const auto &sme : chunk.debug_info.source_map) {
                if (NormalizePath(sme.source_path) == sbp.path && sme.source_line == src_line) {
                    output_lines.push_back(sme.output_line);
                }
            }

            if (!output_lines.empty()) {
                for (int oline : output_lines) {
                    for (const auto &le : chunk.line_table) {
                        if (static_cast<int>(le.line) == oline) {
                            verified = true;
                            actual_line = src_line;
                            break;
                        }
                    }
                    if (verified) break;
                }
            }

            if (!verified) {
                for (const auto &le : chunk.line_table) {
                    if (static_cast<int>(le.line) == src_line) {
                        verified = true;
                        break;
                    }
                }
            }

            if (!verified) {
                for (const auto &le : chunk.line_table) {
                    if (static_cast<int>(le.line) > src_line) {
                        actual_line = static_cast<int>(le.line);
                        verified = true;
                        break;
                    }
                }
            }

            bp_results += "{\"verified\":" + std::string(verified ? "true" : "false")
                + ",\"line\":" + std::to_string(actual_line) + "}";
            DapLog(std::string("[setBreakpoints] result line=") + std::to_string(src_line)
                + " verified=" + (verified ? "true" : "false")
                + " actual=" + std::to_string(actual_line));
        }
        bp_results += "]";

        if (launched_) {
            ApplyBreakpoints();
        }

        SendResponse(seq, "setBreakpoints", true, "{\"breakpoints\":" + bp_results + "}");
    }

    void ApplyBreakpoints() {
        if (!vm_) return;
        Debugger &dbg = vm_->debugger();
        const Chunk &chunk = vm_->chunk();
        dbg.ClearBreakpoints();
        DapLog("[applyBreakpoints] cleared; source_count=" + std::to_string(source_breakpoints_.size()));

        for (const auto &kv : source_breakpoints_) {
            const std::string &bp_path = kv.second.path;
            for (const auto &spec : kv.second.specs) {
                DapLog("[applyBreakpoints] source=" + bp_path + " line=" + std::to_string(spec.line));
                std::vector<int> output_lines;
                for (const auto &sme : chunk.debug_info.source_map) {
                    if (NormalizePath(sme.source_path) == bp_path && sme.source_line == spec.line) {
                        output_lines.push_back(sme.output_line);
                    }
                }
                DapLog("[applyBreakpoints] source_map_hits=" + std::to_string(output_lines.size()));

                bool applied = false;
                for (int oline : output_lines) {
                    for (const auto &le : chunk.line_table) {
                        if (static_cast<int>(le.line) == oline) {
                            int id = dbg.AddBreakpoint(le.pc, "", oline);
                            if (!spec.condition.empty()) {
                                dbg.SetBreakpointCondition(id, spec.condition);
                            }
                            applied = true;
                            DapLog("[applyBreakpoints] applied id=" + std::to_string(id)
                                + " output_line=" + std::to_string(oline)
                                + " pc=" + std::to_string(le.pc));
                            break;
                        }
                    }
                    if (applied) break;
                }

                if (!applied) {
                    int id = dbg.AddBreakpointByLine(chunk, spec.line);
                    int pc = -1;
                    bool verified = false;
                    int actual_line = spec.line;
                    for (const auto &bp : dbg.breakpoints()) {
                        if (bp.id == id) {
                            pc = static_cast<int>(bp.pc);
                            verified = bp.verified;
                            actual_line = bp.line;
                            break;
                        }
                    }
                    DapLog("[applyBreakpoints] fallback id=" + std::to_string(id)
                        + " requested_line=" + std::to_string(spec.line)
                        + " actual_line=" + std::to_string(actual_line)
                        + " pc=" + std::to_string(pc)
                        + " verified=" + (verified ? "true" : "false"));
                    if (!spec.condition.empty()) {
                        dbg.SetBreakpointCondition(id, spec.condition);
                    }
                }
            }
        }
    }

    void HandleStackTrace(int seq, const std::string &content) {
        std::lock_guard<std::mutex> lock(state_mutex_);

        std::string frames_json = "[";
        for (std::size_t i = 0; i < cached_frames_.size(); ++i) {
            if (i > 0) frames_json += ",";
            const FrameView &view = cached_frames_[i];
            const DebugFrame &f = view.frame;
            int frame_id = static_cast<int>(i) + 1000;
            if (view.source_ref != 0) {
                source_refs_[view.source_ref] = view.source_path;
            }

            frames_json += "{\"id\":" + std::to_string(frame_id)
                + ",\"name\":\"" + EscapeJson(f.name) + "\""
                + ",\"line\":" + std::to_string(f.source_line)
                + ",\"column\":1"
                + ",\"moduleName\":\"" + EscapeJson(f.module_name) + "\""
                + ",\"moduleVersion\":" + std::to_string(f.module_version_id)
                + ",\"source\":{\"name\":\"" + EscapeJson(ClientSourceName(view.source_path)) + "\""
                + ",\"path\":\"" + EscapeJson(view.source_path) + "\""
                + ",\"sourceReference\":" + std::to_string(view.source_ref) + "}}";
        }
        frames_json += "]";

        int total = static_cast<int>(cached_frames_.size());
        SendResponse(seq, "stackTrace", true,
            "{\"stackFrames\":" + frames_json
            + ",\"totalFrames\":" + std::to_string(total) + "}");
    }

    void HandleScopes(int seq, const std::string &content) {
        int frame_id = ExtractInt(content, "frameId", 0);
        std::string scopes = "["
            "{\"name\":\"Locals\",\"variablesReference\":1,\"expensive\":false}"
            ",{\"name\":\"Arguments\",\"variablesReference\":2,\"expensive\":false}"
            ",{\"name\":\"Globals\",\"variablesReference\":3,\"expensive\":false}"
            "]";
        SendResponse(seq, "scopes", true, "{\"scopes\":" + scopes + "}");
    }

    void HandleVariables(int seq, const std::string &content) {
        int ref = ExtractInt(content, "variablesReference", 0);

        std::lock_guard<std::mutex> lock(state_mutex_);

        if (ref >= 100) {
            auto it = expandable_vars_.find(ref);
            if (it != expandable_vars_.end()) {
                std::string vars_json = FormatExpandableVars(it->second);
                SendResponse(seq, "variables", true, "{\"variables\":" + vars_json + "}");
                return;
            }
            SendResponse(seq, "variables", true, "{\"variables\":[]}");
            return;
        }

        if (cached_locals_.empty() && cached_args_.empty() && cached_globals_.empty()) {
            RefreshVariablesLocked();
        }

        std::vector<DebugVariable> *vars = nullptr;
        std::vector<Value> *vals = nullptr;
        if (ref == 1) { vars = &cached_locals_; vals = &cached_local_values_; }
        else if (ref == 2) { vars = &cached_args_; vals = &cached_arg_values_; }
        else if (ref == 3) { vars = &cached_globals_; vals = &cached_global_values_; }

        std::string vars_json = "[]";
        if (vars) {
            vars_json = FormatDebugVars(*vars, vals ? *vals : std::vector<Value>());
        }

        SendResponse(seq, "variables", true, "{\"variables\":" + vars_json + "}");
    }

    std::string FormatDebugVars(const std::vector<DebugVariable> &vars,
                                const std::vector<Value> &values) {
        std::string result = "[";
        for (std::size_t i = 0; i < vars.size(); ++i) {
            if (i > 0) result += ",";
            int child_ref = 0;
            if (i < values.size() && values[i].IsObjRef()) {
                std::uintptr_t raw = values[i].AsObj();
                if (raw >= 0x100000000ULL) {
                    child_ref = next_var_ref_++;
                    expandable_vars_[child_ref] = values[i];
                }
            }
            result += "{\"name\":\"" + EscapeJson(vars[i].name) + "\""
                + ",\"value\":\"" + EscapeJson(vars[i].value) + "\""
                + ",\"type\":\"" + EscapeJson(vars[i].type) + "\""
                + ",\"variablesReference\":" + std::to_string(child_ref) + "}";
        }
        result += "]";
        return result;
    }

    std::string FormatExpandableVars(const Value &container) {
        if (!vm_ || !container.IsObjRef()) return "[]";
        std::string result = "[";
        std::uintptr_t raw = container.AsObj();

        if (raw >= 0x100000000ULL && raw < 0x200000000ULL) {
            std::size_t sz = vm_->GetArraySize(container);
            std::size_t limit = (sz > 100) ? 100 : sz;
            for (std::size_t i = 0; i < limit; ++i) {
                if (i > 0) result += ",";
                Value elem = vm_->GetArrayElement(container, static_cast<std::int64_t>(i));
                std::string val_str = Debugger::FormatValueEx(elem, *vm_);
                int child_ref = 0;
                if (elem.IsObjRef()) {
                    std::uintptr_t eraw = elem.AsObj();
                    if (eraw >= 0x100000000ULL) {
                        child_ref = next_var_ref_++;
                        expandable_vars_[child_ref] = elem;
                    }
                }
                result += "{\"name\":\"" + std::to_string(i) + "\""
                    + ",\"value\":\"" + EscapeJson(val_str) + "\""
                    + ",\"type\":\"" + EscapeJson(Debugger::TypeName(elem)) + "\""
                    + ",\"variablesReference\":" + std::to_string(child_ref) + "}";
            }
            if (sz > 100) {
                result += ",{\"name\":\"...\",\"value\":\"(" + std::to_string(sz - 100) + " more)\",\"variablesReference\":0}";
            }
        } else if (raw >= 0x200000000ULL && raw < 0x300000000ULL) {
            auto pairs = vm_->GetMappingPairs(container);
            std::size_t limit = (pairs.size() > 100) ? 100 : pairs.size();
            for (std::size_t i = 0; i < limit; ++i) {
                if (i > 0) result += ",";
                const auto &kv = pairs[i];
                std::string key_str = Debugger::FormatValueEx(kv.first, *vm_);
                std::string val_str = Debugger::FormatValueEx(kv.second, *vm_);
                int child_ref = 0;
                if (kv.second.IsObjRef()) {
                    std::uintptr_t vraw = kv.second.AsObj();
                    if (vraw >= 0x100000000ULL) {
                        child_ref = next_var_ref_++;
                        expandable_vars_[child_ref] = kv.second;
                    }
                }
                result += "{\"name\":\"" + EscapeJson(key_str) + "\""
                    + ",\"value\":\"" + EscapeJson(val_str) + "\""
                    + ",\"type\":\"" + EscapeJson(Debugger::TypeName(kv.second)) + "\""
                    + ",\"variablesReference\":" + std::to_string(child_ref) + "}";
            }
            if (pairs.size() > 100) {
                result += ",{\"name\":\"...\",\"value\":\"(" + std::to_string(pairs.size() - 100) + " more)\",\"variablesReference\":0}";
            }
        } else if (raw >= 0x300000000ULL && raw < 0x400000000ULL) {
            const auto &field_names = vm_->GetObjectFieldNames(container);
            for (std::size_t i = 0; i < field_names.size() && i < 100; ++i) {
                if (i > 0) result += ",";
                Value val = vm_->GetObjectField(container, field_names[i]);
                std::string val_str = Debugger::FormatValueEx(val, *vm_);
                int child_ref = 0;
                if (val.IsObjRef()) {
                    std::uintptr_t vraw = val.AsObj();
                    if (vraw >= 0x100000000ULL) {
                        child_ref = next_var_ref_++;
                        expandable_vars_[child_ref] = val;
                    }
                }
                result += "{\"name\":\"" + EscapeJson(field_names[i]) + "\""
                    + ",\"value\":\"" + EscapeJson(val_str) + "\""
                    + ",\"type\":\"" + EscapeJson(Debugger::TypeName(val)) + "\""
                    + ",\"variablesReference\":" + std::to_string(child_ref) + "}";
            }
        } else if (raw >= 0x400000000ULL) {
            const auto &field_names = vm_->GetObjectFieldNames(container);
            for (std::size_t i = 0; i < field_names.size() && i < 100; ++i) {
                if (i > 0) result += ",";
                Value val = vm_->GetObjectField(container, field_names[i]);
                std::string val_str = Debugger::FormatValueEx(val, *vm_);
                int child_ref = 0;
                if (val.IsObjRef()) {
                    std::uintptr_t vraw = val.AsObj();
                    if (vraw >= 0x100000000ULL) {
                        child_ref = next_var_ref_++;
                        expandable_vars_[child_ref] = val;
                    }
                }
                result += "{\"name\":\"" + EscapeJson(field_names[i]) + "\""
                    + ",\"value\":\"" + EscapeJson(val_str) + "\""
                    + ",\"type\":\"" + EscapeJson(Debugger::TypeName(val)) + "\""
                    + ",\"variablesReference\":" + std::to_string(child_ref) + "}";
            }
        }

        result += "]";
        return result;
    }

    void HandleEvaluate(int seq, const std::string &content) {
        std::string expr = ExtractString(content, "expression");
        std::string context = ExtractString(content, "context");

        std::lock_guard<std::mutex> lock(state_mutex_);

        Debugger &dbg = vm_->debugger();
        const Chunk &chunk = vm_->chunk();
        const std::vector<Frame> &frames = vm_->frames();
        const Value *stack = vm_->StackData();
        const std::size_t stack_size = vm_->StackSize();

        Value result = Debugger::ResolveExpr(expr, chunk, frames, stack, stack_size, *vm_);
        bool found = (result.Tag() != ValueTag::Nil);

        if (!found) {
            found = dbg.TryResolveVariable(expr, chunk, frames, stack, stack_size, &result);
        }

        if (!found) {
            result = Debugger::ParseLiteral(expr);
            found = (result.Tag() != ValueTag::Nil);
        }

        std::string value_str = found ? Debugger::FormatValueEx(result, *vm_) : ("<not found: " + expr + ">");

        int var_ref = 0;
        if (found && result.IsObjRef()) {
            std::uintptr_t raw = result.AsObj();
            if (raw >= 0x100000000ULL) {
                var_ref = next_var_ref_++;
                expandable_vars_[var_ref] = result;
            }
        }

        std::string body = "{\"result\":\"" + EscapeJson(value_str) + "\""
            + ",\"variablesReference\":" + std::to_string(var_ref)
            + ",\"success\":" + (found ? "true" : "false");
        if (context == "repl") {
            body += ",\"presentationHint\":{\"kind\":\"data\"}";
        }
        body += "}";

        SendResponse(seq, "evaluate", found, body);
    }

    void HandleSource(int seq, const std::string &content) {
        int src_ref = ExtractInt(content, "sourceReference", 0);
        auto it = source_refs_.find(src_ref);
        if (it == source_refs_.end()) {
            SendResponse(seq, "source", false, "{}", "source not available");
            return;
        }
        auto lines = Debugger::SourceLines(it->second);
        std::string body;
        for (const auto &l : lines) {
            body += l + "\n";
        }
        std::string result_body = "{\"content\":\"" + EscapeJson(body) + "\""
            + ",\"mimeType\":\"text/x-lpc\"}";
        SendResponse(seq, "source", true, result_body);
    }

    void ResumeVm(StepMode mode) {
        if (!vm_) return;
        Debugger &dbg = vm_->debugger();
        std::uint32_t depth = static_cast<std::uint32_t>(vm_->frames().size());
        std::uint32_t pc = 0;
        if (!vm_->frames().empty()) {
            pc = vm_->frames().back().ip;
        }
        if (mode == StepMode::StepInto || mode == StepMode::StepOver) {
            dbg.SetSourceStepMode(mode, depth, vm_->chunk(), pc);
        } else {
            dbg.SetStepMode(mode, depth);
        }
        if (mode == StepMode::StepInto) {
            dbg.SetSourceStepMode(StepMode::StepInto, 0, vm_->chunk(), pc);
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            cached_locals_.clear();
            cached_args_.clear();
            cached_globals_.clear();
            cached_local_values_.clear();
            cached_arg_values_.clear();
            cached_global_values_.clear();
            expandable_vars_.clear();
        }

        vm_paused_ = false;
        resume_cv_.notify_all();
    }

    void RefreshVariablesLocked() {
        if (!vm_) return;
        Debugger &dbg = vm_->debugger();
        const Chunk &chunk = vm_->chunk();
        const std::vector<Frame> &frames = vm_->frames();
        const Value *stack = vm_->StackData();
        const std::size_t stack_size = vm_->StackSize();

        cached_arg_values_.clear();
        cached_local_values_.clear();
        cached_global_values_.clear();

        if (!frames.empty()) {
            const Frame &fr = frames.back();
            cached_args_ = dbg.GetArgs(chunk, fr, stack, stack_size);
            cached_locals_ = dbg.GetLocals(chunk, fr, stack, stack_size);

            if (fr.func_id < chunk.functions.size()) {
                const auto &fproto = chunk.functions[fr.func_id];
                const FunctionDebugInfo *fdi = nullptr;
                if (fr.func_id < chunk.debug_info.function_debug.size()) {
                    fdi = &chunk.debug_info.function_debug[fr.func_id];
                }
                for (std::uint16_t i = 0; i < fproto.arity; ++i) {
                    std::uint32_t slot = fr.base + i;
                    cached_arg_values_.push_back(slot < stack_size ? stack[slot] : Value::Nil());
                }
                if (fdi) {
                    for (int i = 0; i < static_cast<int>(fdi->local_names.size()); ++i) {
                        std::uint32_t slot = fr.base + fproto.arity + i;
                        cached_local_values_.push_back(slot < stack_size ? stack[slot] : Value::Nil());
                    }
                }
            }
        }
        cached_globals_ = dbg.GetGlobals(chunk);
        for (std::size_t i = 0; i < chunk.globals.size(); ++i) {
            cached_global_values_.push_back(i < chunk.globals.size() ? chunk.globals[i] : Value::Nil());
        }
    }
};

static DapServer *g_dap_server = nullptr;

RuntimeError RunDapServerStep(Vm &vm, std::uint32_t pc) {
    if (g_dap_server) {
        return g_dap_server->OnVmBreak(pc);
    }
    return RuntimeError::Ok();
}

void RunDapServer(Vm &vm, const std::string &entry_function) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    DapLog("=== LPC DAP session start ===");
    DapServer server;
    server.SetVm(&vm);
    server.SetEntryFunction(entry_function);
    g_dap_server = &server;
    server.Run();
    g_dap_server = nullptr;
}

static DapSocket CreateListenSocket(int port) {
    if (!InitDapSockets()) return kInvalidDapSocket;

    DapSocket listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == kInvalidDapSocket) return kInvalidDapSocket;

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));

    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        CloseDapSocket(listen_fd);
        return kInvalidDapSocket;
    }
    if (listen(listen_fd, 1) != 0) {
        CloseDapSocket(listen_fd);
        return kInvalidDapSocket;
    }
    return listen_fd;
}

static void RunDapAttachServerOnSocket(Vm &vm, DapSocket listen_fd) {
    DapLog("[attach] waiting for DAP client");
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    DapSocket client_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
    CloseDapSocket(listen_fd);
    if (client_fd == kInvalidDapSocket) {
        DapLog("[attach] accept failed");
        return;
    }

    DapLog("[attach] DAP client connected");
    DapServer server(client_fd, true);
    server.SetVm(&vm);
    server.Run();
    CloseDapSocket(client_fd);
    DapLog("[attach] DAP client disconnected");
}

bool StartDapAttachServer(Vm &vm, int port) {
    DapSocket listen_fd = CreateListenSocket(port);
    if (listen_fd == kInvalidDapSocket) {
        DapLog("[attach] listen failed on port " + std::to_string(port));
        return false;
    }
    DapLog("[attach] listening on 127.0.0.1:" + std::to_string(port));
    std::thread([&vm, listen_fd]() {
        RunDapAttachServerOnSocket(vm, listen_fd);
    }).detach();
    return true;
}

} // namespace vm
} // namespace lpc
