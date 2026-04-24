#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>

#include "debug.h"
#include "runtime/vm.h"
#include "type/lpc_proto.h"
#include "type/lpc_closure.h"
#include "type/lpc_object.h"
#include "lpc_value.h"

using namespace std;

extern string get_cwd();

static int get_call_depth(lpc_vm_t *vm)
{
    int depth = 0;
    call_info_t *ci = vm->get_call_info();
    while (ci) {
        depth++;
        ci = ci->pre;
    }
    return depth;
}

static std::string trim_copy(const std::string &s)
{
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) {
        ++b;
    }
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) {
        --e;
    }
    return s.substr(b, e - b);
}

static bool parse_break_spec(const std::string &spec, const std::string &default_file, std::string *out_file, lint32_t *out_line)
{
    if (!out_file || !out_line) {
        return false;
    }
    const std::string s = trim_copy(spec);
    if (s.empty()) {
        return false;
    }

    std::string file = default_file;
    std::string line_text = s;
    size_t colon = s.rfind(':');
    if (colon != std::string::npos) {
        file = trim_copy(s.substr(0, colon));
        line_text = trim_copy(s.substr(colon + 1));
    }
    lint32_t line = static_cast<lint32_t>(atoi(line_text.c_str()));
    if (file.empty() || line <= 0) {
        return false;
    }

    *out_file = file;
    *out_line = line;
    return true;
}

static bool is_all_digits(const std::string &s)
{
    if (s.empty()) return false;
    for (char ch : s) {
        if (ch < '0' || ch > '9') return false;
    }
    return true;
}

static int parse_positive_or_default(const std::string &s, int defv)
{
    std::string t = trim_copy(s);
    if (t.empty()) return defv;
    if (!is_all_digits(t)) return -1;
    int v = atoi(t.c_str());
    return v > 0 ? v : -1;
}

static int frame_index_of(call_info_t *base, call_info_t *target)
{
    int idx = 0;
    call_info_t *it = base;
    while (it) {
        if (it == target) return idx;
        it = it->next;
        ++idx;
    }
    return -1;
}

static bool parse_break_with_if(
    const std::string &args,
    const std::string &default_file,
    std::string *out_file,
    lint32_t *out_line,
    std::string *out_cond)
{
    if (!out_file || !out_line || !out_cond) {
        return false;
    }
    std::string body = trim_copy(args);
    size_t pos = body.find(" if ");
    if (pos == std::string::npos) {
        out_cond->clear();
        return parse_break_spec(body, default_file, out_file, out_line);
    }

    std::string spec = trim_copy(body.substr(0, pos));
    std::string cond = trim_copy(body.substr(pos + 4));
    if (cond.empty()) {
        return false;
    }
    if (!parse_break_spec(spec, default_file, out_file, out_line)) {
        return false;
    }
    *out_cond = cond;
    return true;
}

static std::vector<std::pair<std::string, lint32_t>> collect_breakpoints(const std::unordered_map<std::string, std::set<lint32_t>> &bps)
{
    std::vector<std::pair<std::string, lint32_t>> out;
    for (const auto &it : bps) {
        for (lint32_t ln : it.second) {
            out.push_back({it.first, ln});
        }
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });
    return out;
}

static bool contains_bp(const std::unordered_map<std::string, std::set<lint32_t>> &bps, const std::string &file, lint32_t line)
{
    auto it = bps.find(file);
    return it != bps.end() && it->second.count(line) > 0;
}

static std::vector<std::pair<std::string, lint32_t>> collect_all_breakpoints(
    const std::unordered_map<std::string, std::set<lint32_t>> &bps,
    const std::unordered_map<std::string, std::set<lint32_t>> &tbps)
{
    auto rows = collect_breakpoints(bps);
    auto trows = collect_breakpoints(tbps);
    rows.insert(rows.end(), trows.begin(), trows.end());
    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });
    rows.erase(std::unique(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        return a.first == b.first && a.second == b.second;
    }), rows.end());
    return rows;
}

static std::string csv_escape(const std::string &s)
{
    bool need_quote = false;
    for (char ch : s) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            need_quote = true;
            break;
        }
    }
    if (!need_quote) {
        return s;
    }
    std::string out;
    out.reserve(s.size() + 4);
    out.push_back('"');
    for (char ch : s) {
        if (ch == '"') {
            out.push_back('"');
            out.push_back('"');
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

static string value_to_string(const lpc_value_t &val)
{
    if (val.is_null()) return "null";
    if (val.is_undefined()) return "undefined(0)";
    if (val.is_int()) return to_string(val.get_int());
    if (val.is_float()) return to_string(val.get_float()) + "f";
    if (val.is_bool()) return val.get_bool() ? "true" : "false";
    if (val.is_string()) {
        auto *obj = val.get_gcobj();
        if (obj) return "\"" + string(obj->str.get_str()) + "\"";
        return "\"\"";
    }
    if (val.is_array()) {
        if (val.is_class()) return "<class>";
        return "<array>";
    }
    if (val.is_mapping()) return "<mapping>";
    if (val.is_object()) return "<object>";
    if (val.is_function()) return "<function>";
    if (val.is_closure()) return "<closure>";
    return "<unknown>";
}

lpc_debugger_t::lpc_debugger_t(lpc_vm_t *_vm)
{
    this->vm = _vm;
    mode = cmd_t::none;
    const char *script = std::getenv("LPC_DEBUG_SCRIPT");
    if (script && *script) {
        load_script_from_file(script);
    }
    const char *strict = std::getenv("LPC_DEBUG_SCRIPT_STRICT");
    if (strict && *strict) {
        scripted_strict_ = (std::string(strict) == "1" || std::string(strict) == "true" ||
            std::string(strict) == "TRUE" || std::string(strict) == "on" || std::string(strict) == "ON");
    }
}

bool lpc_debugger_t::can_run()
{
    if (mode == cmd_t::none) return true;

    call_info_t *cur_ci = vm->get_call_info();
    if (!cur_ci) return true;

    if (check_watchpoint()) {
        return false;
    }

    switch (mode)
    {
    case cmd_t::step: {
        if (stepping_) {
            stepping_ = false;
            return true;
        }
        return false;
    }
    case cmd_t::next: {
        if (stepping_) {
            stepping_ = false;
            return true;
        }
        int depth = get_call_depth(vm);
        if (depth > next_depth_) return true;
        if (depth < next_depth_) return false;
        lint32_t line = get_current_line();
        if (line > 0 && line != next_line_) {
            cur_line = line;
            return false;
        }
        return true;
    }
    case cmd_t::continue_run: {
        return !check_breakpoint();
    }
    case cmd_t::run_out: {
        int depth = get_call_depth(vm);
        if (depth < run_out_depth) return false;
        return true;
    }
    default:
        break;
    }

    return true;
}

void lpc_debugger_t::set_break_point(const std::string &obj_name, lint32_t line)
{
    break_points[obj_name].insert(line);
    if (!break_hit_count.count(obj_name)) {
        break_hit_count[obj_name] = std::unordered_map<lint32_t, lint32_t>();
    }
    if (!break_hit_count[obj_name].count(line)) {
        break_hit_count[obj_name][line] = 0;
    }
}

void lpc_debugger_t::reset_break_point(const std::string &obj_name, lint32_t line)
{
    auto it = break_points.find(obj_name);
    if (it != break_points.end()) {
        it->second.erase(line);
        if (it->second.empty()) {
            break_points.erase(it);
        }
    }

    auto tit = temp_break_points.find(obj_name);
    if (tit != temp_break_points.end()) {
        tit->second.erase(line);
        if (tit->second.empty()) {
            temp_break_points.erase(tit);
        }
    }

    auto to_it = temp_only_break_points.find(obj_name);
    if (to_it != temp_only_break_points.end()) {
        to_it->second.erase(line);
        if (to_it->second.empty()) {
            temp_only_break_points.erase(to_it);
        }
    }

    auto dit = disabled_break_points.find(obj_name);
    if (dit != disabled_break_points.end()) {
        dit->second.erase(line);
        if (dit->second.empty()) {
            disabled_break_points.erase(dit);
        }
    }

    auto hit_it = break_hit_count.find(obj_name);
    if (hit_it != break_hit_count.end()) {
        hit_it->second.erase(line);
        if (hit_it->second.empty()) {
            break_hit_count.erase(hit_it);
        }
    }

    auto ign_it = break_ignore_count.find(obj_name);
    if (ign_it != break_ignore_count.end()) {
        ign_it->second.erase(line);
        if (ign_it->second.empty()) {
            break_ignore_count.erase(ign_it);
        }
    }

    auto cond_it = break_conditions.find(obj_name);
    if (cond_it != break_conditions.end()) {
        cond_it->second.erase(line);
        if (cond_it->second.empty()) {
            break_conditions.erase(cond_it);
        }
    }

    auto cmd_it = break_commands.find(obj_name);
    if (cmd_it != break_commands.end()) {
        cmd_it->second.erase(line);
        if (cmd_it->second.empty()) {
            break_commands.erase(cmd_it);
        }
    }
}

void lpc_debugger_t::clear_all_break_points()
{
    break_points.clear();
    temp_break_points.clear();
    temp_only_break_points.clear();
    disabled_break_points.clear();
    break_hit_count.clear();
    break_ignore_count.clear();
    break_conditions.clear();
    break_commands.clear();
    until_frame_ = nullptr;
    until_file_.clear();
    until_line_ = -1;
}

void lpc_debugger_t::step()
{
    mode = cmd_t::step;
    stepping_ = true;
    vm->run();
}

void lpc_debugger_t::next()
{
    mode = cmd_t::next;
    stepping_ = true;
    next_depth_ = get_call_depth(vm);
    next_line_ = get_current_line();
    vm->run();
}

void lpc_debugger_t::run_out()
{
    mode = cmd_t::run_out;
    run_out_depth = get_call_depth(vm);
    vm->run();
}

void lpc_debugger_t::continue_run()
{
    mode = cmd_t::continue_run;
    vm->run();
}

void lpc_debugger_t::do_exit()
{
    force_exit = true;
}

call_info_t *lpc_debugger_t::active_frame()
{
    call_info_t *cur = vm->get_call_info();
    if (!selected_frame_) {
        return cur;
    }
    call_info_t *it = vm->get_base_call();
    while (it) {
        if (it == selected_frame_) {
            return selected_frame_;
        }
        it = it->next;
    }
    selected_frame_ = nullptr;
    return cur;
}

bool lpc_debugger_t::is_frame_alive(call_info_t *ci) const
{
    if (!ci) {
        return false;
    }
    call_info_t *it = vm->get_base_call();
    while (it) {
        if (it == ci) {
            return true;
        }
        it = it->next;
    }
    return false;
}

bool lpc_debugger_t::resolve_variable_value(call_info_t *ci, const std::string &expr, std::string *out) const
{
    if (!ci || !out) {
        return false;
    }
    const function_proto_t *func = vm->get_frame_function(ci);
    if (!func) {
        return false;
    }
    std::string key = trim_copy(expr);
    if (key.empty()) {
        return false;
    }

    auto resolve_simple_ptr = [&](const std::string &name) -> lpc_value_t * {
        std::string key1 = trim_copy(name);
        if (key1.empty()) {
            return nullptr;
        }

        if (key1.compare(0, 3, "arg") == 0 && key1.size() > 3 && is_all_digits(key1.substr(3))) {
            int idx = atoi(key1.substr(3).c_str());
            if (idx >= 0 && idx < func->nargs) {
                return ci->base + idx;
            }
        }

        if (key1.compare(0, 5, "local") == 0 && key1.size() > 5 && is_all_digits(key1.substr(5))) {
            int idx = atoi(key1.substr(5).c_str());
            if (idx >= 0 && idx < func->nlocal) {
                return ci->base + idx;
            }
        }

        if (func->vprotos) {
            for (int i = 0; i < func->nlocal; ++i) {
                if (func->vprotos[i].name && key1 == func->vprotos[i].name) {
                    return ci->base + i;
                }
            }
        }

        if (is_all_digits(key1)) {
            int idx = atoi(key1.c_str());
            if (idx >= 0 && idx < func->nlocal) {
                return ci->base + idx;
            }
        }
        return nullptr;
    };

    size_t dot = key.find('.');
    if (dot != std::string::npos) {
        std::string left = trim_copy(key.substr(0, dot));
        std::string field = trim_copy(key.substr(dot + 1));
        if (left.empty() || field.empty()) {
            return false;
        }

        lpc_value_t *base = resolve_simple_ptr(left);
        if (!base || !base->is_object()) {
            return false;
        }

        lpc_object_t *obj = reinterpret_cast<lpc_object_t *>(base->get_gcobj());
        if (!obj || !obj->get_locals()) {
            return false;
        }

        object_proto_t *proto = obj->get_proto();
        if (!proto || proto->nvariable <= 0) {
            return false;
        }

        int idx = -1;
        if (is_all_digits(field)) {
            idx = atoi(field.c_str());
        } else {
            for (int i = 0; i < proto->nvariable; ++i) {
                if (proto->variable_table && proto->variable_table[i].name && field == proto->variable_table[i].name) {
                    idx = i;
                    break;
                }
                if (idx < 0 && proto->gvprotos && proto->gvprotos[i].name && field == proto->gvprotos[i].name) {
                    idx = i;
                }
            }
        }
        if (idx < 0 || idx >= proto->nvariable) {
            return false;
        }

        *out = value_to_string(obj->get_locals()[idx]);
        return true;
    }

    lpc_value_t *val = resolve_simple_ptr(key);
    if (!val) {
        return false;
    }
    *out = value_to_string(*val);
    return true;
}

void lpc_debugger_t::run_single_inspect_command(const std::string &cmdline)
{
    std::string c = trim_copy(cmdline);
    if (c.empty()) return;
    if (c == "bt" || c == "backtrace") {
        print_backtrace();
        return;
    }
    if (c == "where" || c == "frame" || c == "info frame" || c == "i f") {
        print_frame_info();
        return;
    }
    if (c == "locals" || c == "info locals" || c == "i locals") {
        print_locals();
        return;
    }
    if (c == "args" || c == "info args" || c == "i args") {
        print_args();
        return;
    }
    if (c == "upvalues" || c == "info upvalues") {
        print_upvalues();
        return;
    }
    if (c == "list" || c == "l") {
        print_source_context(-1);
        return;
    }
    if (c.compare(0, 5, "list ") == 0 || c.compare(0, 2, "l ") == 0) {
        size_t sp = c.find(' ');
        std::string arg = trim_copy(c.substr(sp + 1));
        if (is_all_digits(arg)) {
            print_source_context(atoi(arg.c_str()));
        }
        return;
    }
    if (c.compare(0, 2, "p ") == 0 || c.compare(0, 6, "print ") == 0 || c.compare(0, 3, "x ") == 0) {
        size_t sp = c.find(' ');
        std::string arg = trim_copy(c.substr(sp + 1));
        if (!arg.empty()) {
            print_variable(arg);
        }
        return;
    }
}

bool lpc_debugger_t::load_script_from_file(const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    scripted_cmds_.clear();
    scripted_cmd_idx_ = 0;

    std::string line;
    while (std::getline(in, line)) {
        line = trim_copy(line);
        if (line.empty()) {
            continue;
        }
        if (line.size() >= 1 && line[0] == '#') {
            continue;
        }
        scripted_cmds_.push_back(line);
    }
    in.close();
    return true;
}

void lpc_debugger_t::run_auto_break_commands()
{
    if (!last_break_hit_ || last_break_file_.empty() || last_break_line_ <= 0) {
        return;
    }
    auto fit = break_commands.find(last_break_file_);
    if (fit == break_commands.end()) {
        return;
    }
    auto lit = fit->second.find(last_break_line_);
    if (lit == fit->second.end()) {
        return;
    }

    for (const std::string &cmd : lit->second) {
        std::cout << "[auto] " << cmd << "\n";
        run_single_inspect_command(cmd);
    }
}

bool lpc_debugger_t::check_watchpoint()
{
    call_info_t *ci = vm->get_call_info();
    if (!ci) {
        return false;
    }

    for (auto &w : watch_points_) {
        if (!w.enabled) {
            continue;
        }
        std::string cur;
        if (!resolve_variable_value(ci, w.expr, &cur)) {
            continue;
        }
        if (!w.initialized) {
            w.last_value = cur;
            w.initialized = true;
            continue;
        }
        if (w.last_value != cur) {
            if (w.ignore_count > 0) {
                w.ignore_count -= 1;
                w.last_value = cur;
                continue;
            }
            w.hit_count += 1;
            last_watch_hit_id_ = w.id;
            last_watch_old_value_ = w.last_value;
            last_watch_new_value_ = cur;
            last_watch_hit_ = true;
            w.last_value = cur;
            return true;
        }
    }
    last_watch_hit_ = false;
    return false;
}

void lpc_debugger_t::print_watchpoints()
{
    if (watch_points_.empty()) {
        cout << "No watchpoints.\n";
        return;
    }
    for (const auto &w : watch_points_) {
        cout << "  [" << w.id << "] " << w.expr
             << (w.enabled ? "" : " (disabled)")
             << (w.initialized ? (" value=" + w.last_value) : " (uninitialized)")
             << " hit=" << w.hit_count
             << " ignore=" << w.ignore_count
             << "\n";
    }
}

void lpc_debugger_t::print_displays()
{
    if (displays_.empty()) {
        cout << "No display items.\n";
        return;
    }
    for (const auto &d : displays_) {
        cout << "  [" << d.id << "] " << d.expr
             << (d.enabled ? "" : " (disabled)")
             << "\n";
    }
}

void lpc_debugger_t::run_display_items()
{
    if (displays_.empty()) {
        return;
    }
    call_info_t *ci = active_frame();
    if (!ci) {
        return;
    }
    for (const auto &d : displays_) {
        if (!d.enabled) {
            continue;
        }
        std::string v;
        if (!resolve_variable_value(ci, d.expr, &v)) {
            cout << "[display " << d.id << "] " << d.expr << " = <unresolved>\n";
            continue;
        }
        cout << "[display " << d.id << "] " << d.expr << " = " << v << "\n";
    }
}

void lpc_debugger_t::start()
{
    mode = cmd_t::step;
    stepping_ = false;
    vm->run_main();

    if (vm->get_call_info()) {
        cout << "LPC debugger started. Type 'help' for commands.\n";
        print_frame_info();
        run();
    } else {
        cout << "Program completed immediately.\n";
    }
}

void lpc_debugger_t::fetch_cmd(cmd_def &cmd)
{
    string line;
    static string last_cmd;

    while (true) {
        if (scripted_cmd_idx_ < scripted_cmds_.size()) {
            line = scripted_cmds_[scripted_cmd_idx_++];
            cout << "(lpcdbg:script) " << line << "\n";
        } else {
            cout << "(lpcdbg) ";
            if (!getline(cin, line)) {
                cmd.cmd = cmd_t::exit;
                return;
            }
        }

        line = trim_copy(line);
        if (line.empty()) {
            if (last_cmd.empty()) {
                continue;
            }
            line = last_cmd;
        } else {
            last_cmd = line;
        }

        if (line == "s" || line == "step") {
            cmd.cmd = cmd_t::step;
            cmd.sparam1.clear();
            return;
        } else if (line == "si" || line == "stepi") {
            cmd.cmd = cmd_t::step;
            cmd.sparam1.clear();
            return;
        } else if (line == "n" || line == "next") {
            cmd.cmd = cmd_t::next;
            cmd.sparam1.clear();
            return;
        } else if (line == "ni" || line == "nexti") {
            cmd.cmd = cmd_t::next;
            cmd.sparam1.clear();
            return;
        } else if (line == "c" || line == "continue") {
            cmd.cmd = cmd_t::continue_run;
            cmd.sparam1.clear();
            return;
        } else if (line == "r" || line == "run") {
            cmd.cmd = cmd_t::continue_run;
            cmd.sparam1.clear();
            return;
        } else if (line == "start") {
            cmd.cmd = cmd_t::step;
            cmd.sparam1.clear();
            return;
        } else if (line == "finish" || line == "out") {
            cmd.cmd = cmd_t::run_out;
            cmd.sparam1.clear();
            return;
        } else if (line == "exit" || line == "q" || line == "quit") {
            cmd.cmd = cmd_t::exit;
            cmd.sparam1.clear();
            return;
        } else if (line.compare(0, 7, "source ") == 0) {
            string path = trim_copy(line.substr(7));
            if (path.empty()) {
                cout << "Usage: source <file>\n";
                continue;
            }
            if (!load_script_from_file(path)) {
                cout << "Failed to load script: " << path << "\n";
                continue;
            }
            cout << "Loaded script: " << path << " (" << scripted_cmds_.size() << " commands)\n";
            continue;
        } else if (line == "bt" || line == "backtrace") {
            print_backtrace();
            continue;
        } else if (line == "where" || line == "frame" || line == "info frame" || line == "i f") {
            print_frame_info();
            continue;
        } else if (line.compare(0, 6, "frame ") == 0) {
            string arg = trim_copy(line.substr(6));
            if (!is_all_digits(arg)) {
                cout << "Usage: frame <index>\n";
                continue;
            }
            int idx = atoi(arg.c_str());
            call_info_t *it = vm->get_base_call();
            int cur_idx = 0;
            selected_frame_ = nullptr;
            while (it) {
                if (cur_idx == idx) {
                    selected_frame_ = it;
                    break;
                }
                it = it->next;
                ++cur_idx;
            }
            if (!selected_frame_) {
                cout << "Frame index out of range: " << idx << "\n";
                continue;
            }
            print_frame_info();
            continue;
        } else if (line == "up" || line.compare(0, 3, "up ") == 0 || line == "u" || line.compare(0, 2, "u ") == 0) {
            call_info_t *cur = active_frame();
            if (!cur) {
                cout << "No current frame.\n";
                continue;
            }
            size_t sp = line.find(' ');
            int n = (sp == string::npos) ? 1 : parse_positive_or_default(line.substr(sp + 1), 1);
            if (n <= 0) {
                cout << "Usage: up [n]\n";
                continue;
            }
            call_info_t *it = cur;
            while (n-- > 0 && it && it->pre) {
                it = it->pre;
            }
            selected_frame_ = it ? it : cur;
            print_frame_info();
            continue;
        } else if (line == "down" || line.compare(0, 5, "down ") == 0) {
            call_info_t *cur = active_frame();
            if (!cur) {
                cout << "No current frame.\n";
                continue;
            }
            size_t sp = line.find(' ');
            int n = (sp == string::npos) ? 1 : parse_positive_or_default(line.substr(sp + 1), 1);
            if (n <= 0) {
                cout << "Usage: down [n]\n";
                continue;
            }
            call_info_t *it = cur;
            while (n-- > 0 && it && it->next) {
                it = it->next;
            }
            selected_frame_ = it ? it : cur;
            print_frame_info();
            continue;
        } else if (line == "bl" || line == "breakpoints") {
            auto rows = collect_all_breakpoints(break_points, temp_break_points);
            if (rows.empty()) {
                cout << "No breakpoints.\n";
                continue;
            }
            for (size_t i = 0; i < rows.size(); ++i) {
                bool is_temp = contains_bp(temp_break_points, rows[i].first, rows[i].second);
                bool is_disabled = contains_bp(disabled_break_points, rows[i].first, rows[i].second);
                int hit = 0;
                if (break_hit_count.count(rows[i].first) && break_hit_count[rows[i].first].count(rows[i].second)) {
                    hit = break_hit_count[rows[i].first][rows[i].second];
                }
                int ignore = 0;
                if (break_ignore_count.count(rows[i].first) && break_ignore_count[rows[i].first].count(rows[i].second)) {
                    ignore = break_ignore_count[rows[i].first][rows[i].second];
                }
                string cond;
                if (break_conditions.count(rows[i].first) && break_conditions[rows[i].first].count(rows[i].second)) {
                    cond = break_conditions[rows[i].first][rows[i].second];
                }
                int cmdn = 0;
                if (break_commands.count(rows[i].first) && break_commands[rows[i].first].count(rows[i].second)) {
                    cmdn = static_cast<int>(break_commands[rows[i].first][rows[i].second].size());
                }
                cout << "  [" << i << "] " << rows[i].first << ":" << rows[i].second
                     << (is_temp ? " (temp)" : "")
                     << (is_disabled ? " (disabled)" : "")
                     << " hit=" << hit
                     << " ignore=" << ignore;
                if (!cond.empty()) {
                    cout << " if(" << cond << ")";
                }
                if (cmdn > 0) {
                    cout << " cmds=" << cmdn;
                }
                cout << "\n";
            }
            continue;
        } else if (line == "info break" || line == "info breakpoints" || line == "i b") {
            auto rows = collect_all_breakpoints(break_points, temp_break_points);
            if (rows.empty()) {
                cout << "No breakpoints.\n";
                continue;
            }
            for (size_t i = 0; i < rows.size(); ++i) {
                bool is_temp = contains_bp(temp_break_points, rows[i].first, rows[i].second);
                bool is_disabled = contains_bp(disabled_break_points, rows[i].first, rows[i].second);
                int hit = 0;
                if (break_hit_count.count(rows[i].first) && break_hit_count[rows[i].first].count(rows[i].second)) {
                    hit = break_hit_count[rows[i].first][rows[i].second];
                }
                int ignore = 0;
                if (break_ignore_count.count(rows[i].first) && break_ignore_count[rows[i].first].count(rows[i].second)) {
                    ignore = break_ignore_count[rows[i].first][rows[i].second];
                }
                string cond;
                if (break_conditions.count(rows[i].first) && break_conditions[rows[i].first].count(rows[i].second)) {
                    cond = break_conditions[rows[i].first][rows[i].second];
                }
                int cmdn = 0;
                if (break_commands.count(rows[i].first) && break_commands[rows[i].first].count(rows[i].second)) {
                    cmdn = static_cast<int>(break_commands[rows[i].first][rows[i].second].size());
                }
                cout << "  [" << i << "] " << rows[i].first << ":" << rows[i].second
                     << (is_temp ? " (temp)" : "")
                     << (is_disabled ? " (disabled)" : "")
                     << " hit=" << hit
                     << " ignore=" << ignore;
                if (!cond.empty()) {
                    cout << " if(" << cond << ")";
                }
                if (cmdn > 0) {
                    cout << " cmds=" << cmdn;
                }
                cout << "\n";
            }
            continue;
        } else if (line == "info break csv" || line == "i b csv") {
            auto rows = collect_all_breakpoints(break_points, temp_break_points);
            cout << "id,file,line,temp,disabled,hit,ignore,condition,commands\n";
            for (size_t i = 0; i < rows.size(); ++i) {
                bool is_temp = contains_bp(temp_break_points, rows[i].first, rows[i].second);
                bool is_disabled = contains_bp(disabled_break_points, rows[i].first, rows[i].second);
                int hit = 0;
                if (break_hit_count.count(rows[i].first) && break_hit_count[rows[i].first].count(rows[i].second)) {
                    hit = break_hit_count[rows[i].first][rows[i].second];
                }
                int ignore = 0;
                if (break_ignore_count.count(rows[i].first) && break_ignore_count[rows[i].first].count(rows[i].second)) {
                    ignore = break_ignore_count[rows[i].first][rows[i].second];
                }
                string cond;
                if (break_conditions.count(rows[i].first) && break_conditions[rows[i].first].count(rows[i].second)) {
                    cond = break_conditions[rows[i].first][rows[i].second];
                }
                int cmdn = 0;
                if (break_commands.count(rows[i].first) && break_commands[rows[i].first].count(rows[i].second)) {
                    cmdn = static_cast<int>(break_commands[rows[i].first][rows[i].second].size());
                }
                cout << i << ","
                     << csv_escape(rows[i].first) << ","
                     << rows[i].second << ","
                     << (is_temp ? 1 : 0) << ","
                     << (is_disabled ? 1 : 0) << ","
                     << hit << ","
                     << ignore << ","
                     << csv_escape(cond) << ","
                     << cmdn << "\n";
            }
            continue;
        } else if (line.compare(0, 10, "condition ") == 0) {
            size_t sp = line.find(' ');
            string rest = trim_copy(line.substr(sp + 1));
            size_t sp2 = rest.find(' ');
            if (sp2 == string::npos) {
                cout << "Usage: condition <id> <expr>\n";
                continue;
            }
            string idtxt = trim_copy(rest.substr(0, sp2));
            string expr = trim_copy(rest.substr(sp2 + 1));
            if (!is_all_digits(idtxt) || expr.empty()) {
                cout << "Usage: condition <id> <expr>\n";
                continue;
            }
            auto rows = collect_all_breakpoints(break_points, temp_break_points);
            int id = atoi(idtxt.c_str());
            if (id < 0 || id >= static_cast<int>(rows.size())) {
                cout << "Breakpoint id out of range: " << id << "\n";
                continue;
            }
            break_conditions[rows[id].first][rows[id].second] = expr;
            cout << "Condition set for " << rows[id].first << ":" << rows[id].second
                 << " if " << expr << "\n";
            continue;
        } else if (line.compare(0, 7, "ignore ") == 0) {
            size_t sp = line.find(' ');
            string rest = trim_copy(line.substr(sp + 1));
            size_t sp2 = rest.find(' ');
            if (sp2 == string::npos) {
                cout << "Usage: ignore <id> <count>\n";
                continue;
            }
            string idtxt = trim_copy(rest.substr(0, sp2));
            string cnttxt = trim_copy(rest.substr(sp2 + 1));
            if (!is_all_digits(idtxt) || !is_all_digits(cnttxt)) {
                cout << "Usage: ignore <id> <count>\n";
                continue;
            }
            auto rows = collect_all_breakpoints(break_points, temp_break_points);
            int id = atoi(idtxt.c_str());
            int cnt = atoi(cnttxt.c_str());
            if (id < 0 || id >= static_cast<int>(rows.size()) || cnt < 0) {
                cout << "Invalid ignore parameters.\n";
                continue;
            }
            break_ignore_count[rows[id].first][rows[id].second] = cnt;
            cout << "Ignore count set for " << rows[id].first << ":" << rows[id].second
                 << " => " << cnt << "\n";
            continue;
        } else if (line.compare(0, 7, "disable") == 0) {
            size_t sp = line.find(' ');
            if (sp == string::npos) {
                cout << "Usage: disable <id>\n";
                continue;
            }
            string arg = trim_copy(line.substr(sp + 1));
            if (!is_all_digits(arg)) {
                cout << "Usage: disable <id>\n";
                continue;
            }
            auto rows = collect_all_breakpoints(break_points, temp_break_points);
            int id = atoi(arg.c_str());
            if (id < 0 || id >= static_cast<int>(rows.size())) {
                cout << "Breakpoint id out of range: " << id << "\n";
                continue;
            }
            disabled_break_points[rows[id].first].insert(rows[id].second);
            cout << "Breakpoint disabled: " << rows[id].first << ":" << rows[id].second << "\n";
            continue;
        } else if (line.compare(0, 6, "enable") == 0) {
            size_t sp = line.find(' ');
            if (sp == string::npos) {
                cout << "Usage: enable <id>\n";
                continue;
            }
            string arg = trim_copy(line.substr(sp + 1));
            if (!is_all_digits(arg)) {
                cout << "Usage: enable <id>\n";
                continue;
            }
            auto rows = collect_all_breakpoints(break_points, temp_break_points);
            int id = atoi(arg.c_str());
            if (id < 0 || id >= static_cast<int>(rows.size())) {
                cout << "Breakpoint id out of range: " << id << "\n";
                continue;
            }
            auto it = disabled_break_points.find(rows[id].first);
            if (it != disabled_break_points.end()) {
                it->second.erase(rows[id].second);
                if (it->second.empty()) {
                    disabled_break_points.erase(it);
                }
            }
            cout << "Breakpoint enabled: " << rows[id].first << ":" << rows[id].second << "\n";
            continue;
        } else if (line == "list" || line == "l") {
            print_source_context(-1);
            continue;
        } else if (line == "info display" || line == "i display") {
            print_displays();
            continue;
        } else if (line.compare(0, 8, "display ") == 0) {
            string expr = trim_copy(line.substr(8));
            if (expr.empty()) {
                cout << "Usage: display <expr>\n";
                continue;
            }
            display_item_t d;
            d.id = next_display_id_++;
            d.expr = expr;
            displays_.push_back(d);
            cout << "Display item set: [" << d.id << "] " << d.expr << "\n";
            continue;
        } else if (line.compare(0, 10, "undisplay ") == 0) {
            string arg = trim_copy(line.substr(10));
            if (!is_all_digits(arg)) {
                cout << "Usage: undisplay <id>\n";
                continue;
            }
            int id = atoi(arg.c_str());
            bool removed = false;
            for (auto it = displays_.begin(); it != displays_.end(); ++it) {
                if (it->id == id) {
                    displays_.erase(it);
                    removed = true;
                    break;
                }
            }
            if (removed) {
                cout << "Display item removed: " << id << "\n";
            } else {
                cout << "Display id not found: " << id << "\n";
            }
            continue;
        } else if (line.compare(0, 16, "disable display ") == 0 || line.compare(0, 15, "enable display ") == 0) {
            bool enable = line.compare(0, 15, "enable display ") == 0;
            string arg = trim_copy(line.substr(enable ? 15 : 16));
            if (!is_all_digits(arg)) {
                cout << "Usage: " << (enable ? "enable" : "disable") << " display <id>\n";
                continue;
            }
            int id = atoi(arg.c_str());
            bool found = false;
            for (auto &d : displays_) {
                if (d.id == id) {
                    d.enabled = enable;
                    found = true;
                    break;
                }
            }
            if (found) {
                cout << "Display item " << (enable ? "enabled" : "disabled") << ": " << id << "\n";
            } else {
                cout << "Display id not found: " << id << "\n";
            }
            continue;
        } else if (line == "info watch" || line == "i watch" || line == "watchpoints") {
            print_watchpoints();
            continue;
        } else if (line == "info watch csv" || line == "i watch csv") {
            cout << "id,expr,enabled,initialized,value,hit,ignore\n";
            for (const auto &w : watch_points_) {
                cout << w.id << ","
                     << csv_escape(w.expr) << ","
                     << (w.enabled ? 1 : 0) << ","
                     << (w.initialized ? 1 : 0) << ","
                     << csv_escape(w.initialized ? w.last_value : string()) << ","
                     << w.hit_count << ","
                     << w.ignore_count << "\n";
            }
            continue;
        } else if (line.compare(0, 16, "condition watch ") == 0) {
            string rest = trim_copy(line.substr(16));
            size_t sp = rest.find(' ');
            if (sp == string::npos) {
                cout << "Usage: condition watch <id> <expr>\n";
                continue;
            }
            string idtxt = trim_copy(rest.substr(0, sp));
            string expr = trim_copy(rest.substr(sp + 1));
            if (!is_all_digits(idtxt) || expr.empty()) {
                cout << "Usage: condition watch <id> <expr>\n";
                continue;
            }
            int id = atoi(idtxt.c_str());
            bool found = false;
            for (auto &w : watch_points_) {
                if (w.id == id) {
                    w.expr = expr;
                    w.initialized = false;
                    w.last_value.clear();
                    found = true;
                    break;
                }
            }
            if (found) {
                cout << "Watchpoint condition updated: [" << id << "] " << expr << "\n";
            } else {
                cout << "Watchpoint id not found: " << id << "\n";
            }
            continue;
        } else if (line.compare(0, 13, "ignore watch ") == 0) {
            string rest = trim_copy(line.substr(13));
            size_t sp = rest.find(' ');
            if (sp == string::npos) {
                cout << "Usage: ignore watch <id> <count>\n";
                continue;
            }
            string idtxt = trim_copy(rest.substr(0, sp));
            string cnttxt = trim_copy(rest.substr(sp + 1));
            if (!is_all_digits(idtxt) || !is_all_digits(cnttxt)) {
                cout << "Usage: ignore watch <id> <count>\n";
                continue;
            }
            int id = atoi(idtxt.c_str());
            int cnt = atoi(cnttxt.c_str());
            bool found = false;
            for (auto &w : watch_points_) {
                if (w.id == id) {
                    w.ignore_count = cnt;
                    found = true;
                    break;
                }
            }
            if (found) {
                cout << "Watchpoint ignore count set: [" << id << "] => " << cnt << "\n";
            } else {
                cout << "Watchpoint id not found: " << id << "\n";
            }
            continue;
        } else if (line.compare(0, 6, "watch ") == 0) {
            string expr = trim_copy(line.substr(6));
            if (expr.empty()) {
                cout << "Usage: watch <var-or-index>\n";
                continue;
            }
            watch_point_t w;
            w.id = next_watch_id_++;
            w.expr = expr;
            watch_points_.push_back(w);
            cout << "Watchpoint set: [" << w.id << "] " << w.expr << "\n";
            continue;
        } else if (line.compare(0, 8, "unwatch ") == 0 || line.compare(0, 13, "delete watch ") == 0) {
            size_t sp = line.find(' ');
            string arg = trim_copy(line.substr(sp + 1));
            if (line.compare(0, 13, "delete watch ") == 0) {
                arg = trim_copy(line.substr(13));
            }
            if (!is_all_digits(arg)) {
                cout << "Usage: unwatch <id>\n";
                continue;
            }
            int id = atoi(arg.c_str());
            bool removed = false;
            for (auto it = watch_points_.begin(); it != watch_points_.end(); ++it) {
                if (it->id == id) {
                    watch_points_.erase(it);
                    removed = true;
                    break;
                }
            }
            if (removed) {
                cout << "Watchpoint removed: " << id << "\n";
            } else {
                cout << "Watchpoint id not found: " << id << "\n";
            }
            continue;
        } else if (line.compare(0, 14, "disable watch ") == 0 || line.compare(0, 13, "enable watch ") == 0) {
            bool enable = line.compare(0, 13, "enable watch ") == 0;
            string arg = trim_copy(line.substr(enable ? 13 : 14));
            if (!is_all_digits(arg)) {
                cout << "Usage: " << (enable ? "enable" : "disable") << " watch <id>\n";
                continue;
            }
            int id = atoi(arg.c_str());
            bool found = false;
            for (auto &w : watch_points_) {
                if (w.id == id) {
                    w.enabled = enable;
                    found = true;
                    break;
                }
            }
            if (found) {
                cout << "Watchpoint " << (enable ? "enabled" : "disabled") << ": " << id << "\n";
            } else {
                cout << "Watchpoint id not found: " << id << "\n";
            }
            continue;
        } else if (line.compare(0, 5, "list ") == 0 || line.compare(0, 2, "l ") == 0) {
            size_t sp = line.find(' ');
            string arg = trim_copy(line.substr(sp + 1));
            if (!is_all_digits(arg)) {
                cout << "Usage: list [line]\n";
                continue;
            }
            print_source_context(atoi(arg.c_str()));
            continue;
        } else if (line == "info source" || line == "i source") {
            print_source_context(-1);
            continue;
        } else if (line == "info stack" || line == "i stack") {
            print_backtrace();
            continue;
        } else if (line.compare(0, 5, "ifbt ") == 0 || line.compare(0, 13, "if_breaktrace ") == 0) {
            size_t sp = line.find(' ');
            string args = trim_copy(line.substr(sp + 1));
            if (args.empty()) {
                cout << "Usage: ifbt <line> or ifbt <file>:<line>\n";
                continue;
            }
            string bfile;
            lint32_t bline = 0;
            vm_frame_info_t fi;
            string default_file;
            call_info_t *ci = vm->get_call_info();
            if (ci && vm->get_frame_info(ci, &fi) && fi.object_name) {
                default_file = fi.object_name;
            }
            if (!parse_break_spec(args, default_file, &bfile, &bline)) {
                cout << "Invalid breakpoint. Usage: ifbt <line> or ifbt <file>:<line>\n";
                continue;
            }

            set_break_point(bfile, bline);
            cout << "Breakpoint set: " << bfile << ":" << bline << "\n";
            print_frame_info();
            print_backtrace();
            continue;
        } else if (line == "locals") {
            print_locals();
            continue;
        } else if (line == "args" || line == "info args" || line == "i args") {
            print_args();
            continue;
        } else if (line == "info locals" || line == "i locals") {
            print_locals();
            continue;
        } else if (line == "upvalues" || line == "info upvalues") {
            print_upvalues();
            continue;
        } else if (line.compare(0, 2, "p ") == 0 || line.compare(0, 6, "print ") == 0 || line.compare(0, 3, "x ") == 0) {
            size_t sp = line.find(' ');
            cmd.sparam1 = trim_copy(line.substr(sp + 1));
            if (cmd.sparam1.empty()) {
                cout << "Usage: p <var-or-index>\n";
                continue;
            }
            print_variable(cmd.sparam1);
            continue;
        } else if (line.compare(0, 2, "b ") == 0 || line.compare(0, 6, "break ") == 0) {
            size_t sp = line.find(' ');
            string args = line.substr(sp + 1);
            string bfile;
            lint32_t bline = 0;
            string cond;
            vm_frame_info_t fi;
            string default_file;
            call_info_t *ci = vm->get_call_info();
            if (ci && vm->get_frame_info(ci, &fi) && fi.object_name) {
                default_file = fi.object_name;
            }
            if (parse_break_with_if(args, default_file, &bfile, &bline, &cond)) {
                set_break_point(bfile, bline);
                if (!cond.empty()) {
                    break_conditions[bfile][bline] = cond;
                }
                cout << "Breakpoint set: " << bfile << ":" << bline;
                if (!cond.empty()) {
                    cout << " if " << cond;
                }
                cout << "\n";
            } else {
                cout << "Invalid breakpoint. Usage: b <line> [if <expr>] or b <file>:<line> [if <expr>]\n";
            }
            continue;
        } else if (line.compare(0, 3, "tb ") == 0 || line.compare(0, 7, "tbreak ") == 0) {
            size_t sp = line.find(' ');
            string args = line.substr(sp + 1);
            string bfile;
            lint32_t bline = 0;
            vm_frame_info_t fi;
            string default_file;
            call_info_t *ci = active_frame();
            if (ci && vm->get_frame_info(ci, &fi) && fi.object_name) {
                default_file = fi.object_name;
            }
            if (parse_break_spec(args, default_file, &bfile, &bline)) {
                set_break_point(bfile, bline);
                temp_break_points[bfile].insert(bline);
                cout << "Temporary breakpoint set: " << bfile << ":" << bline << "\n";
            } else {
                cout << "Invalid breakpoint. Usage: tb <line> or tb <file>:<line>\n";
            }
            continue;
        } else if (line.compare(0, 6, "until ") == 0 || line.compare(0, 8, "advance ") == 0) {
            size_t sp = line.find(' ');
            string arg = trim_copy(line.substr(sp + 1));
            if (!is_all_digits(arg)) {
                cout << "Usage: until <line>\n";
                continue;
            }
            int target_line = atoi(arg.c_str());
            call_info_t *ci = active_frame();
            vm_frame_info_t fi;
            string bfile;
            if (ci && vm->get_frame_info(ci, &fi) && fi.object_name) {
                bfile = fi.object_name;
            }
            if (bfile.empty() || target_line <= 0) {
                cout << "Cannot resolve frame source for until.\n";
                continue;
            }
            set_break_point(bfile, target_line);
            temp_break_points[bfile].insert(target_line);
            temp_only_break_points[bfile].insert(target_line);
            until_frame_ = ci;
            until_file_ = bfile;
            until_line_ = target_line;
            cmd.cmd = cmd_t::continue_run;
            cmd.sparam1.clear();
            return;
        } else if (line.compare(0, 4, "del ") == 0 || line.compare(0, 7, "delete ") == 0) {
            size_t sp = line.find(' ');
            string args = trim_copy(line.substr(sp + 1));
            if (is_all_digits(args)) {
                auto rows = collect_all_breakpoints(break_points, temp_break_points);
                int id = atoi(args.c_str());
                if (id < 0 || id >= static_cast<int>(rows.size())) {
                    cout << "Breakpoint id out of range: " << id << "\n";
                    continue;
                }
                reset_break_point(rows[id].first, rows[id].second);
                cout << "Breakpoint cleared: " << rows[id].first << ":" << rows[id].second << "\n";
                continue;
            }
            string bfile;
            lint32_t bline = 0;
            vm_frame_info_t fi;
            string default_file;
            call_info_t *ci = vm->get_call_info();
            if (ci && vm->get_frame_info(ci, &fi) && fi.object_name) {
                default_file = fi.object_name;
            }
            if (parse_break_spec(args, default_file, &bfile, &bline)) {
                reset_break_point(bfile, bline);
                cout << "Breakpoint cleared: " << bfile << ":" << bline << "\n";
            } else {
                cout << "Invalid breakpoint. Usage: del <line> or del <file>:<line>\n";
            }
            continue;
        } else if (line.compare(0, 9, "commands ") == 0) {
            size_t sp = line.find(' ');
            string args = trim_copy(line.substr(sp + 1));
            if (!is_all_digits(args)) {
                cout << "Usage: commands <id>\n";
                continue;
            }

            auto rows = collect_all_breakpoints(break_points, temp_break_points);
            int id = atoi(args.c_str());
            if (id < 0 || id >= static_cast<int>(rows.size())) {
                cout << "Breakpoint id out of range: " << id << "\n";
                continue;
            }

            cout << "Enter command list for breakpoint " << id << " (type 'end' to finish).\n";
            vector<string> cmds;
            while (true) {
                cout << "> ";
                string c;
                if (!getline(cin, c)) break;
                c = trim_copy(c);
                if (c == "end") break;
                if (!c.empty()) cmds.push_back(c);
            }

            break_commands[rows[id].first][rows[id].second] = cmds;
            cout << "Attached " << cmds.size() << " command(s) to "
                 << rows[id].first << ":" << rows[id].second << "\n";
            continue;
        } else if (line == "clear") {
            clear_all_break_points();
            cout << "All breakpoints cleared.\n";
            continue;
        } else if (line.compare(0, 5, "help ") == 0) {
            string topic = trim_copy(line.substr(5));
            if (topic == "break" || topic == "b") {
                cout << "break usage:\n"
                     << "  b <line>\n"
                     << "  b <file>:<line>\n"
                     << "  b <spec> if <expr>     expr supports: hit==N, hit%N==K\n"
                     << "  tb <spec>              temporary breakpoint\n"
                     << "  condition <id> <expr>  update condition\n"
                     << "  ignore <id> <count>    ignore first N hits\n"
                     << "  disable/enable <id>    toggle breakpoint\n"
                     << "  del <id>|<spec>        delete breakpoint\n";
            } else if (topic == "watch") {
                cout << "watch usage:\n"
                     << "  watch <expr>               expr: name/index/argN/localN/obj.field\n"
                     << "  unwatch <id>\n"
                     << "  condition watch <id> <expr>\n"
                     << "  ignore watch <id> <count>\n"
                     << "  disable/enable watch <id>\n"
                     << "  info watch [csv]\n";
            } else if (topic == "display") {
                cout << "display usage:\n"
                     << "  display <expr>\n"
                     << "  undisplay <id>\n"
                     << "  disable/enable display <id>\n"
                     << "  info display\n";
            } else if (topic == "frame") {
                cout << "frame usage:\n"
                     << "  frame <n>\n"
                     << "  up [n], down [n]\n"
                     << "  where / info frame\n"
                     << "  bt / info stack\n";
            } else if (topic == "list" || topic == "source") {
                cout << "source usage:\n"
                     << "  list [line]\n"
                     << "  info source\n";
            } else {
                cout << "Unknown help topic: " << topic << "\n"
                     << "Try: help break | help watch | help display | help frame | help source\n";
            }
            continue;
        } else if (line == "help") {
            cout << "Commands:\n"
                 << "  s, step       Step one instruction\n"
                 << "  si, stepi     Step one instruction (gdb alias)\n"
                 << "  n, next       Step over (next line in current function)\n"
                 << "  ni, nexti     Step over (gdb alias)\n"
                 << "  c, continue   Continue execution\n"
                 << "  r, run        Continue execution (gdb-like alias)\n"
                 << "  start         Step from current position\n"
                 << "  finish, out   Step out of current function\n"
                 << "  b <line>      Set breakpoint at line in current file\n"
                 << "  b <file>:<line> Set breakpoint\n"
                 << "  b <spec> if <expr> Set conditional breakpoint (hit%N==K or hit==N)\n"
                 << "  tb <spec>     Set temporary breakpoint (currently normal bp)\n"
                 << "  until <line>  Continue until line (temp breakpoint)\n"
                 << "  advance <line> Alias of until\n"
                 << "  frame <n>     Select frame by index (from bt)\n"
                 << "  up [n]        Select caller frame\n"
                 << "  down [n]      Select callee frame\n"
                 << "  del <line>    Delete breakpoint at line in current file\n"
                 << "  del <file>:<line> Delete breakpoint\n"
                 << "  del <id>      Delete breakpoint by id\n"
                 << "  disable <id>  Disable breakpoint\n"
                 << "  enable <id>   Enable breakpoint\n"
                 << "  condition <id> <expr> Set/replace condition\n"
                 << "  ignore <id> <count> Ignore first N hits\n"
                 << "  commands <id> Attach auto commands (end with 'end')\n"
                 << "  bl            List breakpoints\n"
                 << "  info break    List breakpoints with ids\n"
                 << "  info break csv Print breakpoints in CSV format\n"
                 << "  ifbt <spec>   Set breakpoint and print frame+backtrace\n"
                 << "  where         Show current frame\n"
                 << "  info frame    Show current frame info\n"
                 << "  info stack    Show call stack\n"
                 << "  info source   Show source context\n"
                 << "  list [line]   Show source context\n"
                 << "  watch <expr>  Add watchpoint for local var/index\n"
                 << "  unwatch <id>  Remove watchpoint\n"
                 << "  info watch    List watchpoints\n"
                 << "  info watch csv Print watchpoints in CSV format\n"
                 << "  disable watch <id> Disable watchpoint\n"
                 << "  enable watch <id>  Enable watchpoint\n"
                 << "  condition watch <id> <expr> Update watch expression\n"
                 << "  ignore watch <id> <n> Ignore first N watch changes\n"
                 << "  display <expr> Add auto display expression\n"
                 << "  undisplay <id> Remove display item\n"
                 << "  info display   List display items\n"
                 << "  disable display <id> Disable display item\n"
                 << "  enable display <id>  Enable display item\n"
                 << "  source <file>  Load and execute debugger command script\n"
                 << "  clear         Clear all breakpoints\n"
                 << "  bt, backtrace Show call stack\n"
                 << "  args          Show function arguments\n"
                 << "  locals        Show local variables\n"
                 << "  upvalues      Show closure upvalues\n"
                 << "  p <var>       Print variable value (by index)\n"
                 << "  <empty line>  Repeat previous command\n"
                 << "  help <topic>  Show focused help (break/watch/display/frame/source)\n"
                 << "  exit, q       Exit debugger\n";
            continue;
        } else {
            if (scripted_cmd_idx_ > 0 && scripted_cmd_idx_ <= scripted_cmds_.size()) {
                cout << "Unknown command in script: " << line << "\n";
                if (scripted_strict_) {
                    cmd.cmd = cmd_t::exit;
                    return;
                }
                continue;
            }
            cout << "Unknown command: " << line << " (type 'help' for commands)\n";
        }
    }
}

void lpc_debugger_t::run()
{
    cmd_def cmd;
    while (!force_exit) {
        fetch_cmd(cmd);
        if (cmd.cmd == cmd_t::exit) {
            do_exit();
            break;
        }

        switch (cmd.cmd)
        {
        case cmd_t::step:
            step();
            break;
        case cmd_t::next:
            next();
            break;
        case cmd_t::continue_run:
            continue_run();
            break;
        case cmd_t::run_out:
            run_out();
            break;
        default:
            break;
        }

        call_info_t *cur_ci = vm->get_call_info();
        if (vm->has_error()) {
            cout << "Runtime error: " << vm->last_error() << "\n";
            vm->clear_last_error();
        }
        if (last_watch_hit_) {
            cout << "Watchpoint " << last_watch_hit_id_ << " triggered: "
                 << last_watch_old_value_ << " -> " << last_watch_new_value_ << "\n";
            last_watch_hit_ = false;
        }
        if (!cur_ci) {
            cout << "Program exited.\n";
            break;
        }

        run_auto_break_commands();
        run_display_items();

        print_frame_info();
    }
}

void lpc_debugger_t::print_frame_info()
{
    call_info_t *ci = active_frame();
    if (!ci) return;

    vm_frame_info_t frame;
    if (vm->get_frame_info(ci, &frame)) {
        cout << "  at " << (frame.object_name ? frame.object_name : "<unknown>")
             << "::" << (frame.function_name ? frame.function_name : "<unknown>");
        if (frame.has_line) {
            cout << ":" << frame.line;
        }
        cout << " pc=" << frame.pc_offset;
        int fi = frame_index_of(vm->get_base_call(), ci);
        if (fi >= 0) {
            cout << " frame=#" << fi;
        }
        cout << " depth=" << get_call_depth(vm) << "\n";
    }
}

void lpc_debugger_t::print_backtrace()
{
    call_info_t *ci = vm->get_base_call();
    if (!ci) {
        cout << "No call stack.\n";
        return;
    }

    int frame_num = 0;
    while (ci) {
        vm_frame_info_t frame;
        if (vm->get_frame_info(ci, &frame)) {
            cout << "  #" << frame_num << " "
                 << (frame.object_name ? frame.object_name : "<unknown>")
                 << "::" << (frame.function_name ? frame.function_name : "<unknown>");
            if (frame.has_line) {
                cout << ":" << frame.line;
            }
            cout << "\n";
        }
        ci = ci->next;
        frame_num++;
    }
}

void lpc_debugger_t::print_source_context(lint32_t explicit_line)
{
    call_info_t *ci = active_frame();
    if (!ci) {
        cout << "No current frame.\n";
        return;
    }

    vm_frame_info_t frame;
    if (!vm->get_frame_info(ci, &frame) || !frame.object_name) {
        cout << "No source info for current frame.\n";
        return;
    }

    string obj = frame.object_name;
    string path1 = get_cwd() + "/" + obj + ".lpc";
    string path2 = get_cwd() + "/" + obj;
    ifstream in(path1.c_str(), ios::binary);
    string used = path1;
    if (!in.is_open()) {
        in.clear();
        in.open(path2.c_str(), ios::binary);
        used = path2;
    }
    if (!in.is_open()) {
        cout << "Cannot open source: " << path1 << "\n";
        return;
    }

    vector<string> lines;
    string line;
    while (getline(in, line)) {
        lines.push_back(line);
    }
    in.close();
    if (lines.empty()) {
        cout << "Source is empty: " << used << "\n";
        return;
    }

    int target = explicit_line > 0 ? explicit_line : static_cast<int>(frame.line);
    if (target <= 0) target = 1;
    if (target > static_cast<int>(lines.size())) target = static_cast<int>(lines.size());
    int from = target - 3;
    int to = target + 3;
    if (from < 1) from = 1;
    if (to > static_cast<int>(lines.size())) to = static_cast<int>(lines.size());

    cout << "Source: " << used << "\n";
    for (int i = from; i <= to; ++i) {
        cout << (i == target ? "=> " : "   ") << i << "| " << lines[static_cast<size_t>(i - 1)] << "\n";
    }
}

void lpc_debugger_t::print_locals()
{
    call_info_t *ci = active_frame();
    if (!ci) {
        cout << "No current frame.\n";
        return;
    }

    const function_proto_t *func = vm->get_frame_function(ci);
    if (!func) {
        cout << "No function info for current frame.\n";
        return;
    }

    cout << "Locals (nargs=" << func->nargs << ", nlocal=" << func->nlocal << "):\n";
    for (int i = 0; i < func->nlocal; ++i) {
        lpc_value_t *val = ci->base + i;
        string name;
        if (func->vprotos && func->vprotos[i].name) {
            name = func->vprotos[i].name;
        } else if (i < func->nargs) {
            name = "arg" + to_string(i);
        } else {
            name = "local" + to_string(i - func->nargs);
        }
        cout << "  [" << i << "] " << name << " = " << value_to_string(*val) << "\n";
    }
}

void lpc_debugger_t::print_args()
{
    call_info_t *ci = active_frame();
    if (!ci) {
        cout << "No current frame.\n";
        return;
    }

    const function_proto_t *func = vm->get_frame_function(ci);
    if (!func) {
        cout << "No function info for current frame.\n";
        return;
    }

    if (func->nargs <= 0) {
        cout << "No arguments.\n";
        return;
    }

    cout << "Args (nargs=" << func->nargs << "):\n";
    for (int i = 0; i < func->nargs; ++i) {
        lpc_value_t *val = ci->base + i;
        string name;
        if (func->vprotos && func->vprotos[i].name) {
            name = func->vprotos[i].name;
        } else {
            name = "arg" + to_string(i);
        }
        cout << "  [" << i << "] " << name << " = " << value_to_string(*val) << "\n";
    }
}

void lpc_debugger_t::print_upvalues()
{
    call_info_t *ci = active_frame();
    if (!ci) {
        cout << "No current frame.\n";
        return;
    }
    if (!ci->callee || ci->callee->header.type != (lint8_t)value_type::closure_) {
        cout << "Current frame is not a closure call.\n";
        return;
    }

    lpc_closure_t *cl = reinterpret_cast<lpc_closure_t *>(ci->callee);
    function_proto_t *proto = cl->proto;
    if (!proto || proto->nupvalue <= 0) {
        cout << "No upvalues.\n";
        return;
    }

    cout << "Upvalues (nupvalue=" << proto->nupvalue << "):\n";
    for (int i = 0; i < proto->nupvalue; ++i) {
        lpc_value_t *v = cl->get(i);
        const lint16_t sk = proto->upvalue_source_kind ? proto->upvalue_source_kind[i] : -1;
        const lint16_t si = proto->upvalue_source_index ? proto->upvalue_source_index[i] : -1;
        string origin = (sk == upvalue_from_parent_local) ? "parent-local" :
            (sk == upvalue_from_parent_upvalue ? "parent-upvalue" : "unknown");
        cout << "  [" << i << "] " << origin << "(" << si << ") = "
             << (v ? value_to_string(*v) : string("<null>")) << "\n";
    }
}

void lpc_debugger_t::print_variable(const std::string &name)
{
    call_info_t *ci = active_frame();
    if (!ci) {
        cout << "No current frame.\n";
        return;
    }

    const function_proto_t *func = vm->get_frame_function(ci);
    if (!func) {
        cout << "No function info.\n";
        return;
    }

    if (func->vprotos) {
        for (int i = 0; i < func->nlocal; ++i) {
            if (func->vprotos[i].name && name == func->vprotos[i].name) {
                lpc_value_t *val = ci->base + i;
                cout << name << " = " << value_to_string(*val) << "\n";
                return;
            }
        }
    }

    int idx = atoi(name.c_str());
    if (idx >= 0 && idx < func->nlocal) {
        lpc_value_t *val = ci->base + idx;
        cout << "[" << idx << "] = " << value_to_string(*val) << "\n";
        return;
    }

    cout << "Variable not found: " << name << "\n";
}

lint32_t lpc_debugger_t::get_current_line()
{
    call_info_t *ci = vm->get_call_info();
    if (!ci) return 0;

    vm_frame_info_t frame;
    if (vm->get_frame_info(ci, &frame) && frame.has_line) {
        return frame.line;
    }
    return 0;
}

bool lpc_debugger_t::check_breakpoint()
{
    call_info_t *ci = vm->get_call_info();
    if (!ci) return false;

    vm_frame_info_t frame;
    if (!vm->get_frame_info(ci, &frame) || !frame.has_line) return false;

    const char *obj_name = frame.object_name;
    if (!obj_name) return false;
    std::string file = obj_name;

    if (until_frame_) {
        if (!is_frame_alive(until_frame_) || ci != until_frame_) {
            if (!until_file_.empty() && until_line_ > 0 && contains_bp(temp_only_break_points, until_file_, until_line_)) {
                reset_break_point(until_file_, until_line_);
            }
            until_frame_ = nullptr;
            until_file_.clear();
            until_line_ = -1;
        }
    }

    auto it = break_points.find(file);
    if (it == break_points.end() || it->second.count(frame.line) == 0) {
        last_break_hit_ = false;
        return false;
    }

    if (contains_bp(disabled_break_points, file, frame.line)) {
        last_break_hit_ = false;
        return false;
    }

    if (!break_hit_count.count(file)) {
        break_hit_count[file] = std::unordered_map<lint32_t, lint32_t>();
    }
    break_hit_count[file][frame.line] += 1;
    int hit = break_hit_count[file][frame.line];

    if (break_ignore_count.count(file) && break_ignore_count[file].count(frame.line)) {
        int left = break_ignore_count[file][frame.line];
        if (left > 0) {
            break_ignore_count[file][frame.line] = left - 1;
            last_break_hit_ = false;
            return false;
        }
    }

    auto citf = break_conditions.find(file);
    if (citf != break_conditions.end()) {
        auto cit = citf->second.find(frame.line);
        if (cit != citf->second.end()) {
            std::string cond = trim_copy(cit->second);
            bool pass = true;
            size_t modpos = cond.find("hit%");
            size_t eqpos = cond.find("==");
            if (modpos != std::string::npos && eqpos != std::string::npos && modpos == 0 && eqpos > modpos + 4) {
                std::string modn = trim_copy(cond.substr(4, eqpos - 4));
                std::string rhs = trim_copy(cond.substr(eqpos + 2));
                if (is_all_digits(modn) && is_all_digits(rhs)) {
                    int m = atoi(modn.c_str());
                    int k = atoi(rhs.c_str());
                    pass = (m > 0) ? ((hit % m) == k) : false;
                }
            } else if (cond.compare(0, 5, "hit==") == 0) {
                std::string rhs = trim_copy(cond.substr(5));
                if (is_all_digits(rhs)) {
                    int k = atoi(rhs.c_str());
                    pass = (hit == k);
                }
            }
            if (!pass) {
                last_break_hit_ = false;
                return false;
            }
        }
    }

    last_break_file_ = file;
    last_break_line_ = frame.line;
    last_break_hit_ = true;

    auto tit = temp_break_points.find(file);
    if (tit != temp_break_points.end() && tit->second.count(frame.line) > 0) {
        if (contains_bp(temp_only_break_points, file, frame.line) && until_frame_ && ci != until_frame_) {
            last_break_hit_ = false;
            return false;
        }
        tit->second.erase(frame.line);
        if (tit->second.empty()) {
            temp_break_points.erase(tit);
        }
        auto to_it = temp_only_break_points.find(file);
        if (to_it != temp_only_break_points.end()) {
            to_it->second.erase(frame.line);
            if (to_it->second.empty()) {
                temp_only_break_points.erase(to_it);
            }
        }
        if (!until_file_.empty() && until_file_ == file && until_line_ == frame.line) {
            until_frame_ = nullptr;
            until_file_.clear();
            until_line_ = -1;
        }
        reset_break_point(file, frame.line);
    }
    return true;
}
