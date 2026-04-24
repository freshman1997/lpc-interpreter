#ifndef __VM_DEBUG_H__
#define __VM_DEBUG_H__

#include <string>
#include <unordered_map>
#include <vector>
#include <set>
#include <functional>

#include "lpc.h"

class lpc_vm_t;
class function_proto_t;
struct call_info_t;

enum class cmd_t : char
{
    none = 0,
    step,
    next,
    continue_run,
    run_out,
    exit,
};

struct cmd_def
{
    cmd_t cmd;
    lint32_t parm1 = 0;
    lint32_t param2 = 0;
    std::string sparam1;
    std::string sparam2;
};

class lpc_debugger_t
{
public:
    lpc_debugger_t(lpc_vm_t *_vm);
    bool can_run();

    void set_break_point(const std::string &, lint32_t line);
    void reset_break_point(const std::string &, lint32_t line);
    void clear_all_break_points();
    void start();

private:
    void step();
    void next();
    void run_out();
    void continue_run();
    void do_exit();

    void fetch_cmd(cmd_def &);
    void run();
    bool load_script_from_file(const std::string &path);
    call_info_t * active_frame();
    bool is_frame_alive(call_info_t *ci) const;
    void run_auto_break_commands();
    void run_single_inspect_command(const std::string &cmdline);
    bool resolve_variable_value(call_info_t *ci, const std::string &expr, std::string *out) const;
    void print_frame_info();
    void print_backtrace();
    void print_source_context(lint32_t explicit_line = -1);
    void print_args();
    void print_locals();
    void print_upvalues();
    void print_watchpoints();
    void print_displays();
    void run_display_items();
    void print_variable(const std::string &name);
    lint32_t get_current_line();
    bool check_breakpoint();
    bool check_watchpoint();

    struct watch_point_t {
        lint32_t id = 0;
        std::string expr;
        bool enabled = true;
        bool initialized = false;
        std::string last_value;
        lint32_t hit_count = 0;
        lint32_t ignore_count = 0;
    };

    struct display_item_t {
        lint32_t id = 0;
        std::string expr;
        bool enabled = true;
    };

    std::unordered_map<std::string, std::set<lint32_t>> break_points;
    std::unordered_map<std::string, std::set<lint32_t>> temp_break_points;
    std::unordered_map<std::string, std::set<lint32_t>> temp_only_break_points;
    std::unordered_map<std::string, std::set<lint32_t>> disabled_break_points;
    std::unordered_map<std::string, std::unordered_map<lint32_t, lint32_t>> break_hit_count;
    std::unordered_map<std::string, std::unordered_map<lint32_t, lint32_t>> break_ignore_count;
    std::unordered_map<std::string, std::unordered_map<lint32_t, std::string>> break_conditions;
    std::unordered_map<std::string, std::unordered_map<lint32_t, std::vector<std::string>>> break_commands;
    std::unordered_map<cmd_t, std::function<void()>> op_funcs;

    lpc_vm_t *vm;
    const char *cur_pc = nullptr;
    function_proto_t *cur_func = nullptr;

    lint32_t cur_line = 0;
    luint32_t pcoff = 0;
    lint32_t run_out_depth = -1;
    lint32_t next_depth_ = 0;
    lint32_t next_line_ = 0;
    bool stepping_ = false;
    call_info_t *selected_frame_ = nullptr;
    std::string last_break_file_;
    lint32_t last_break_line_ = -1;
    bool last_break_hit_ = false;
    std::vector<watch_point_t> watch_points_;
    lint32_t next_watch_id_ = 1;
    lint32_t last_watch_hit_id_ = -1;
    bool last_watch_hit_ = false;
    std::string last_watch_old_value_;
    std::string last_watch_new_value_;
    std::vector<display_item_t> displays_;
    lint32_t next_display_id_ = 1;
    std::vector<std::string> scripted_cmds_;
    size_t scripted_cmd_idx_ = 0;
    bool scripted_strict_ = false;
    call_info_t *until_frame_ = nullptr;
    std::string until_file_;
    lint32_t until_line_ = -1;

    bool force_exit = false;
    cmd_t mode = cmd_t::none;
};

#endif
