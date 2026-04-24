#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "lpc_value.h"

namespace {

std::mutex g_debug_lock;

static std::string CurrentTimeString()
{
    std::time_t now = std::time(nullptr);
    std::tm local_tm;
#if defined(_WIN32)
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local_tm);
    return std::string(buf);
}

static std::string ResolveLogPath()
{
    const char *path = std::getenv("LPC_DEBUG_LOG");
    if (path && *path) {
        return std::string(path);
    }
    return std::string("log/debug.log");
}

static bool IsFileLogDisabled()
{
    const char *disable = std::getenv("LPC_DEBUG_NO_FILE");
    if (!disable || !*disable) {
        return false;
    }
    std::string v(disable);
    return v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "ON";
}

static bool IsConsoleLogDisabled()
{
    const char *disable = std::getenv("LPC_DEBUG_NO_STDERR");
    if (!disable || !*disable) {
        return false;
    }
    std::string v(disable);
    return v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "ON";
}

static bool IsTimestampDisabled()
{
    const char *disable = std::getenv("LPC_DEBUG_NO_TIMESTAMP");
    if (!disable || !*disable) {
        return false;
    }
    std::string v(disable);
    return v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "ON";
}

static int MaxObjectDepth()
{
    const char *env = std::getenv("LPC_DEBUG_MAX_DEPTH");
    if (!env || !*env) {
        return 6;
    }
    int v = std::atoi(env);
    if (v <= 0) {
        return 1;
    }
    if (v > 64) {
        return 64;
    }
    return v;
}

static std::string VFormat(const char *fmt, va_list ap)
{
    if (!fmt) {
        return std::string();
    }

    va_list ap_copy;
    va_copy(ap_copy, ap);
    int needed = std::vsnprintf(nullptr, 0, fmt, ap_copy);
    va_end(ap_copy);
    if (needed <= 0) {
        return std::string();
    }

    std::vector<char> buf(static_cast<size_t>(needed) + 1);
    std::vsnprintf(buf.data(), buf.size(), fmt, ap);
    return std::string(buf.data(), static_cast<size_t>(needed));
}

static void AppendIndent(std::string &out, int depth)
{
    for (int i = 0; i < depth; ++i) {
        out.append("  ");
    }
}

static void AppendValue(std::string &out, const lpc_value_t *v, int depth, std::unordered_set<luint64_t> &seen);

static void AppendArray(std::string &out, lpc_array_t *arr, int depth, std::unordered_set<luint64_t> &seen)
{
    if (depth >= MaxObjectDepth()) {
        out.append("<max-depth-array>");
        return;
    }
    if (!arr) {
        out.append("null");
        return;
    }
    const luint64_t key = reinterpret_cast<luint64_t>(arr);
    if (seen.count(key)) {
        out.append("<cycle-array>");
        return;
    }
    seen.insert(key);

    const int sz = static_cast<int>(arr->get_size());
    if (sz == 0) {
        out.append("[]");
        seen.erase(key);
        return;
    }
    out.append("[\n");
    for (int i = 0; i < sz; ++i) {
        AppendIndent(out, depth + 1);
        AppendValue(out, arr->get(i), depth + 1, seen);
        if (i + 1 < sz) {
            out.append(",");
        }
        out.append("\n");
    }
    AppendIndent(out, depth);
    out.append("]");
    seen.erase(key);
}

static void AppendMapping(std::string &out, lpc_mapping_t *m, int depth, std::unordered_set<luint64_t> &seen)
{
    if (depth >= MaxObjectDepth()) {
        out.append("<max-depth-mapping>");
        return;
    }
    if (!m) {
        out.append("null");
        return;
    }
    const luint64_t key = reinterpret_cast<luint64_t>(m);
    if (seen.count(key)) {
        out.append("<cycle-mapping>");
        return;
    }
    seen.insert(key);

    const int sz = static_cast<int>(m->get_size());
    if (sz == 0) {
        out.append("({})");
        seen.erase(key);
        return;
    }

    out.append("([\n");
    for (int i = 0; i < sz; ++i) {
        bucket_t *b = m->iterate(i);
        AppendIndent(out, depth + 1);
        if (!b || !b->pair) {
            out.append("<invalid>: <invalid>");
        } else {
            AppendValue(out, &b->pair[0], depth + 1, seen);
            out.append(": ");
            AppendValue(out, &b->pair[1], depth + 1, seen);
        }
        if (i + 1 < sz) {
            out.append(",");
        }
        out.append("\n");
    }
    m->reset_iterator();
    AppendIndent(out, depth);
    out.append("]) ");
    out.pop_back();
    seen.erase(key);
}

static void AppendObject(std::string &out, lpc_object_t *obj, int depth, std::unordered_set<luint64_t> &seen)
{
    if (depth >= MaxObjectDepth()) {
        out.append("<max-depth-object>");
        return;
    }
    if (!obj) {
        out.append("null");
        return;
    }

    const luint64_t key = reinterpret_cast<luint64_t>(obj);
    if (seen.count(key)) {
        out.append("<cycle-object>");
        return;
    }
    seen.insert(key);

    object_proto_t *proto = obj->get_proto();
    const char *name = (proto && proto->name) ? proto->name : obj->get_name();
    out.append("object(");
    out.append(name ? name : "<unknown>");
    out.append(") {");

    if (!proto || proto->nvariable <= 0 || !obj->get_locals()) {
        out.append("}");
        seen.erase(key);
        return;
    }

    out.append("\n");
    for (int i = 0; i < proto->nvariable; ++i) {
        AppendIndent(out, depth + 1);
        const char *vname = nullptr;
        if (proto->variable_table && proto->variable_table[i].name) {
            vname = proto->variable_table[i].name;
        }
        if (!vname && proto->gvprotos && proto->gvprotos[i].name) {
            vname = proto->gvprotos[i].name;
        }
        if (!vname) {
            out.append("var");
            out.append(std::to_string(i));
        } else {
            out.append(vname);
        }
        out.append(": ");
        AppendValue(out, &obj->get_locals()[i], depth + 1, seen);
        if (i + 1 < proto->nvariable) {
            out.append(",");
        }
        out.append("\n");
    }
    AppendIndent(out, depth);
    out.append("}");
    seen.erase(key);
}

static void AppendValue(std::string &out, const lpc_value_t *v, int depth, std::unordered_set<luint64_t> &seen)
{
    if (!v) {
        out.append("null");
        return;
    }
    if (v->is_null()) {
        out.append("null");
    } else if (v->is_undefined()) {
        out.append("undefined");
    } else if (v->is_int()) {
        out.append(std::to_string(v->get_int()));
    } else if (v->is_float()) {
        out.append(std::to_string(v->get_float()));
    } else if (v->is_bool()) {
        out.append(v->get_bool() ? "true" : "false");
    } else if (v->is_string()) {
        lpc_string_t *s = reinterpret_cast<lpc_string_t *>(v->get_gcobj());
        out.push_back('"');
        out.append(s ? s->get_str() : "");
        out.push_back('"');
    } else if (v->is_array()) {
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(v->get_gcobj());
        if (v->is_class()) {
            out.append("class");
        }
        AppendArray(out, arr, depth, seen);
    } else if (v->is_mapping()) {
        lpc_mapping_t *m = reinterpret_cast<lpc_mapping_t *>(v->get_gcobj());
        AppendMapping(out, m, depth, seen);
    } else if (v->is_object()) {
        lpc_object_t *obj = reinterpret_cast<lpc_object_t *>(v->get_gcobj());
        AppendObject(out, obj, depth, seen);
    } else if (v->is_function()) {
        out.append("<function>");
    } else if (v->is_closure()) {
        out.append("<closure>");
    } else {
        out.append("<unknown>");
    }
}

static std::string FormatWithObjectSpecifier(const char *fmt, va_list ap)
{
    std::string out;
    if (!fmt) {
        return out;
    }

    std::unordered_set<luint64_t> seen;

    for (size_t i = 0; fmt[i] != '\0'; ++i) {
        if (fmt[i] != '%') {
            out.push_back(fmt[i]);
            continue;
        }
        if (fmt[i + 1] == '%') {
            out.push_back('%');
            ++i;
            continue;
        }

        size_t begin = i;
        ++i;
        while (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0') {
            ++i;
        }
        while (std::isdigit(static_cast<unsigned char>(fmt[i]))) {
            ++i;
        }
        if (fmt[i] == '*') {
            (void)va_arg(ap, int);
            ++i;
        }
        if (fmt[i] == '.') {
            ++i;
            while (std::isdigit(static_cast<unsigned char>(fmt[i]))) {
                ++i;
            }
            if (fmt[i] == '*') {
                (void)va_arg(ap, int);
                ++i;
            }
        }

        bool long_long = false;
        bool long_one = false;
        if (fmt[i] == 'l') {
            ++i;
            if (fmt[i] == 'l') {
                long_long = true;
                ++i;
            } else {
                long_one = true;
            }
        } else if (fmt[i] == 'h') {
            ++i;
            if (fmt[i] == 'h') {
                ++i;
            }
        }

        char conv = fmt[i];
        if (conv == '\0') {
            break;
        }

        if (conv == 'O') {
            lpc_value_t *v = va_arg(ap, lpc_value_t *);
            AppendValue(out, v, 0, seen);
            continue;
        }
        if (conv == 'm') {
            const char *err = std::strerror(errno);
            out.append(err ? err : "<errno>");
            continue;
        }

        const std::string spec(fmt + begin, fmt + i + 1);
        char buf[256];
        buf[0] = '\0';

        switch (conv) {
        case 'd':
        case 'i': {
            if (long_long) {
                long long x = va_arg(ap, long long);
                std::snprintf(buf, sizeof(buf), spec.c_str(), x);
            } else if (long_one) {
                long x = va_arg(ap, long);
                std::snprintf(buf, sizeof(buf), spec.c_str(), x);
            } else {
                int x = va_arg(ap, int);
                std::snprintf(buf, sizeof(buf), spec.c_str(), x);
            }
            out.append(buf);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            if (long_long) {
                unsigned long long x = va_arg(ap, unsigned long long);
                std::snprintf(buf, sizeof(buf), spec.c_str(), x);
            } else if (long_one) {
                unsigned long x = va_arg(ap, unsigned long);
                std::snprintf(buf, sizeof(buf), spec.c_str(), x);
            } else {
                unsigned int x = va_arg(ap, unsigned int);
                std::snprintf(buf, sizeof(buf), spec.c_str(), x);
            }
            out.append(buf);
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            double x = va_arg(ap, double);
            std::snprintf(buf, sizeof(buf), spec.c_str(), x);
            out.append(buf);
            break;
        }
        case 'c': {
            int x = va_arg(ap, int);
            std::snprintf(buf, sizeof(buf), spec.c_str(), x);
            out.append(buf);
            break;
        }
        case 's': {
            const char *x = va_arg(ap, const char *);
            std::snprintf(buf, sizeof(buf), spec.c_str(), x ? x : "(null)");
            out.append(buf);
            break;
        }
        case 'p': {
            void *x = va_arg(ap, void *);
            std::snprintf(buf, sizeof(buf), spec.c_str(), x);
            out.append(buf);
            break;
        }
        default:
            out.append(spec);
            break;
        }
    }
    return out;
}

} // namespace

void debug_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string msg;
    if (fmt && std::strstr(fmt, "%O") != nullptr) {
        msg = FormatWithObjectSpecifier(fmt, ap);
    } else {
        msg = VFormat(fmt, ap);
    }
    va_end(ap);

    const bool with_time = !IsTimestampDisabled();
    const std::string ts = with_time ? CurrentTimeString() : std::string();
    const std::string line = with_time ? ("[" + ts + "] " + msg) : msg;

    std::lock_guard<std::mutex> lock(g_debug_lock);

    if (!IsConsoleLogDisabled()) {
        std::fputs(line.c_str(), stderr);
        std::fflush(stderr);
    }

    if (!IsFileLogDisabled()) {
        const std::string path = ResolveLogPath();
        std::error_code ec;
        std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        std::ofstream out(path.c_str(), std::ios::app | std::ios::binary);
        if (out.is_open()) {
            out.write(line.data(), static_cast<std::streamsize>(line.size()));
            out.flush();
        }
    }
}
