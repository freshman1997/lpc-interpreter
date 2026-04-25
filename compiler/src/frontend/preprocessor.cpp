#include "frontend/preprocessor.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>
#include <unordered_set>

namespace lpc {
namespace frontend {

struct PreprocessContext {
    std::unordered_map<std::string, std::string> defines;
    std::unordered_set<std::string> include_guard;
    std::vector<std::filesystem::path> include_dirs;
    DiagnosticSink *diag = nullptr;
};

static std::string TrimLeft(const std::string &s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) {
        ++i;
    }
    return s.substr(i);
}

static std::string Trim(const std::string &s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

static std::string ReadFileAll(const std::string &path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool IsIdentStart(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

static bool IsIdentChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

static std::string ExpandMacroToken(
    const std::string &token,
    const PreprocessContext &ctx,
    std::unordered_set<std::string> *expand_stack,
    int depth);

static std::string ExpandMacrosInText(
    const std::string &text,
    const PreprocessContext &ctx,
    std::unordered_set<std::string> *expand_stack,
    int depth) {
    if (depth > 64 || text.empty()) {
        return text;
    }

    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        if (IsIdentStart(text[i])) {
            size_t j = i + 1;
            while (j < text.size() && IsIdentChar(text[j])) {
                ++j;
            }
            const std::string token = text.substr(i, j - i);
            out += ExpandMacroToken(token, ctx, expand_stack, depth + 1);
            i = j;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

static std::string ExpandMacroToken(
    const std::string &token,
    const PreprocessContext &ctx,
    std::unordered_set<std::string> *expand_stack,
    int depth) {
    if (depth > 64) {
        return token;
    }
    auto it = ctx.defines.find(token);
    if (it == ctx.defines.end()) {
        return token;
    }

    if (!expand_stack) {
        return it->second;
    }
    if (expand_stack->count(token)) {
        return token;
    }

    expand_stack->insert(token);
    const std::string expanded = ExpandMacrosInText(it->second, ctx, expand_stack, depth + 1);
    expand_stack->erase(token);
    return expanded;
}

static bool EvalMacroExpr(const std::string &expr, const PreprocessContext &ctx) {
    std::unordered_set<std::string> expand_stack;
    std::string e = Trim(ExpandMacrosInText(expr, ctx, &expand_stack, 0));
    if (e.empty()) return false;

    size_t p_or = e.find("||");
    if (p_or != std::string::npos) {
        return EvalMacroExpr(e.substr(0, p_or), ctx) || EvalMacroExpr(e.substr(p_or + 2), ctx);
    }
    size_t p_and = e.find("&&");
    if (p_and != std::string::npos) {
        return EvalMacroExpr(e.substr(0, p_and), ctx) && EvalMacroExpr(e.substr(p_and + 2), ctx);
    }
    if (!e.empty() && e[0] == '!') {
        return !EvalMacroExpr(e.substr(1), ctx);
    }

    size_t p_eq = e.find("==");
    if (p_eq != std::string::npos) {
        std::string l = Trim(e.substr(0, p_eq));
        std::string r = Trim(e.substr(p_eq + 2));
        std::unordered_set<std::string> stack_l;
        std::unordered_set<std::string> stack_r;
        l = Trim(ExpandMacrosInText(l, ctx, &stack_l, 0));
        r = Trim(ExpandMacrosInText(r, ctx, &stack_r, 0));
        return l == r;
    }
    size_t p_ne = e.find("!=");
    if (p_ne != std::string::npos) {
        std::string l = Trim(e.substr(0, p_ne));
        std::string r = Trim(e.substr(p_ne + 2));
        std::unordered_set<std::string> stack_l;
        std::unordered_set<std::string> stack_r;
        l = Trim(ExpandMacrosInText(l, ctx, &stack_l, 0));
        r = Trim(ExpandMacrosInText(r, ctx, &stack_r, 0));
        return l != r;
    }

    if (e.rfind("defined(", 0) == 0 && e.back() == ')') {
        std::string key = Trim(e.substr(8, e.size() - 9));
        return ctx.defines.count(key) > 0;
    }
    if (ctx.defines.count(e)) return true;
    if (e == "1" || e == "true") return true;
    if (e == "0" || e == "false") return false;
    return false;
}

static std::string ProcessRecursive(const std::string &path, const std::string &text, PreprocessContext &ctx);

static bool ResolveIncludePath(
    const std::string &rel,
    bool quote_mode,
    const std::filesystem::path &current,
    const PreprocessContext &ctx,
    std::filesystem::path *resolved) {
    namespace fs = std::filesystem;

    std::vector<fs::path> roots;
    if (quote_mode) {
        roots.push_back(current);
    }
    for (const auto &d : ctx.include_dirs) {
        roots.push_back(d);
    }

    for (const auto &root : roots) {
        fs::path candidate = root / rel;
        std::error_code ec;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            *resolved = fs::weakly_canonical(candidate);
            return true;
        }
    }

    fs::path direct(rel);
    std::error_code ec;
    if (fs::exists(direct, ec) && fs::is_regular_file(direct, ec)) {
        *resolved = fs::weakly_canonical(direct);
        return true;
    }

    return false;
}

static std::string HandleInclude(const std::string &line, const std::filesystem::path &current, PreprocessContext &ctx, int line_no) {
    size_t q1 = line.find('"');
    size_t q2 = line.find('"', q1 == std::string::npos ? q1 : q1 + 1);
    bool quote_mode = true;
    if (q1 == std::string::npos || q2 == std::string::npos || q2 <= q1 + 1) {
        size_t a1 = line.find('<');
        size_t a2 = line.find('>', a1 == std::string::npos ? a1 : a1 + 1);
        if (a1 != std::string::npos && a2 != std::string::npos && a2 > a1 + 1) {
            q1 = a1;
            q2 = a2;
            quote_mode = false;
        }
    }
    if (q1 == std::string::npos || q2 == std::string::npos || q2 <= q1 + 1) {
        if (ctx.diag) {
            SourceSpan sp;
            sp.line = line_no;
            sp.column = 1;
            sp.length = static_cast<int>(line.size());
            ctx.diag->Add(DiagnosticLevel::Error, sp, "invalid #include format, expected #include \"file\" or #include <file>");
        }
        return "";
    }

    std::string rel = line.substr(q1 + 1, q2 - q1 - 1);
    std::filesystem::path resolved;
    if (!ResolveIncludePath(rel, quote_mode, current, ctx, &resolved)) {
        if (ctx.diag) {
            SourceSpan sp;
            sp.line = line_no;
            sp.column = 1;
            sp.length = static_cast<int>(line.size());
            ctx.diag->Add(DiagnosticLevel::Error, sp, "include file not found: " + rel);
        }
        return "";
    }

    std::string norm = resolved.string();
    if (ctx.include_guard.count(norm)) {
        return "";
    }
    std::string body = ReadFileAll(norm);
    if (body.empty()) {
        if (ctx.diag) {
            SourceSpan sp;
            sp.line = line_no;
            sp.column = 1;
            sp.length = static_cast<int>(line.size());
            ctx.diag->Add(DiagnosticLevel::Error, sp, "include file not found: " + rel);
        }
        return "";
    }

    ctx.include_guard.insert(norm);
    return ProcessRecursive(norm, body, ctx);
}

static std::string ProcessRecursive(const std::string &path, const std::string &text, PreprocessContext &ctx) {
    namespace fs = std::filesystem;

    std::stringstream in(text);
    std::string line;
    std::stringstream out;
    int line_no = 0;

    const fs::path current = fs::path(path).parent_path();

    struct CondFrame {
        bool parent_active = true;
        bool current_active = true;
        bool branch_taken = false;
    };
    std::vector<CondFrame> conds;

    auto is_active = [&]() {
        for (const auto &c : conds) {
            if (!c.current_active) return false;
        }
        return true;
    };

    while (std::getline(in, line)) {
        ++line_no;
        std::string t = TrimLeft(line);

        if (t.rfind("#if", 0) == 0) {
            std::string expr = Trim(t.substr(3));
            bool parent = is_active();
            bool cond = parent && EvalMacroExpr(expr, ctx);
            CondFrame f;
            f.parent_active = parent;
            f.current_active = cond;
            f.branch_taken = cond;
            conds.push_back(f);
            continue;
        }
        if (t.rfind("#elif", 0) == 0) {
            if (conds.empty()) continue;
            std::string expr = Trim(t.substr(5));
            CondFrame &f = conds.back();
            if (!f.parent_active || f.branch_taken) {
                f.current_active = false;
            } else {
                bool cond = EvalMacroExpr(expr, ctx);
                f.current_active = cond;
                f.branch_taken = cond;
            }
            continue;
        }
        if (t.rfind("#else", 0) == 0) {
            if (conds.empty()) continue;
            CondFrame &f = conds.back();
            f.current_active = f.parent_active && !f.branch_taken;
            f.branch_taken = true;
            continue;
        }
        if (t.rfind("#endif", 0) == 0) {
            if (!conds.empty()) conds.pop_back();
            continue;
        }

        if (!is_active()) {
            continue;
        }

        if (t.rfind("#include", 0) == 0) {
            std::string body = HandleInclude(t, current, ctx, line_no);
            out << body;
            if (!body.empty() && body.back() != '\n') {
                out << '\n';
            }
            continue;
        }

        if (t.rfind("#define", 0) == 0) {
            std::string rest = Trim(t.substr(7));
            size_t sp = rest.find_first_of(" \t");
            std::string key = sp == std::string::npos ? rest : rest.substr(0, sp);
            std::string val = sp == std::string::npos ? "1" : Trim(rest.substr(sp + 1));
            if (!key.empty()) {
                ctx.defines[key] = val;
            }
            continue;
        }

        if (t.rfind("#undef", 0) == 0) {
            std::string key = Trim(t.substr(6));
            if (!key.empty()) {
                ctx.defines.erase(key);
            }
            continue;
        }

        std::unordered_set<std::string> expand_stack;
        std::string expanded = ExpandMacrosInText(line, ctx, &expand_stack, 0);
        out << expanded << '\n';
    }

    return out.str();
}

PreprocessResult PreprocessSource(
    const std::string &path,
    const std::string &text,
    DiagnosticSink *diag,
    const std::vector<std::string> &include_dirs) {
    PreprocessResult r;
    PreprocessContext ctx;
    ctx.diag = diag;
    for (const auto &inc : include_dirs) {
        std::filesystem::path p = std::filesystem::path(inc);
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec)) {
            ctx.include_dirs.push_back(std::filesystem::weakly_canonical(p));
        }
    }
    ctx.include_guard.insert(std::filesystem::weakly_canonical(std::filesystem::path(path)).string());
    r.text = ProcessRecursive(path, text, ctx);
    r.defines = ctx.defines;
    return r;
}

} // namespace frontend
} // namespace lpc
