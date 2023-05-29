#include <iostream>
#include <process.h>
#include <stdlib.h>

#include "debug.h"
#include "runtime/vm.h"

using namespace std;

lpc_debugger_t::lpc_debugger_t(lpc_vm_t *_vm)
{
    this->vm = _vm;
    mode = cmd_t::next;
    op_funcs[cmd_t::step] = std::bind(&lpc_debugger_t::step, this);
    op_funcs[cmd_t::next] = std::bind(&lpc_debugger_t::next, this);
    op_funcs[cmd_t::continue_run] = std::bind(&lpc_debugger_t::continue_run, this);
    op_funcs[cmd_t::run_out] = std::bind(&lpc_debugger_t::run_out, this);
    op_funcs[cmd_t::exit] = std::bind(&lpc_debugger_t::exit, this);
}

bool lpc_debugger_t::can_run()
{
    if (mode == cmd_t::none) return true;

    call_info_t *cur_ci = vm->get_call_info();
    const char *pc = cur_ci->savepc;
    const char *start_pc;
    std::vector<std::pair<luint32_t, luint32_t>> *lineMap;
    const char *obj_name;
    
    if (cur_ci->father) {
        cur_func = &cur_ci->father->func_table[cur_ci->funcIdx];
        lineMap = &cur_ci->father->lineMap;
        obj_name = cur_ci->father->name;
        start_pc = cur_ci->father->instructions;
    } else {
        cur_func = &cur_ci->cur_obj->get_proto()->func_table[cur_ci->funcIdx];
        lineMap = &cur_ci->cur_obj->get_proto()->lineMap;
        obj_name = cur_ci->cur_obj->get_proto()->name;
        start_pc = cur_ci->cur_obj->get_proto()->instructions;
    }

    /*auto iter = break_points.find(obj_name);
    if (iter == break_points.end()) {
        return true;
    }

    const std::set<lint32_t> &lineBreakPoints = iter->second;*/

    switch ((cmd_t)mode)
    {
    case cmd_t::next: {
        luint32_t offset = luint32_t(pc - start_pc);
        if (pcoff > offset) {
            return true;
        } 

        return false;
    }
    case cmd_t::step: {
        bool res = cur_pc != pc;
        cur_pc = pc;
        return res;
    }
    /*case lpc_debug_mode::continue_run: {
        
        break;
    }*/
    default:
        break;
    }

    // 当前执行的指令是否在断点上
    /*auto bIter = lineBreakPoints.find(target->first);
    if (bIter != lineBreakPoints.end()) {
        // 命中断点
        return false;
    }*/

    return true;
}

void lpc_debugger_t::set_break_point(const std::string &obj_name, lint32_t line)
{
    std::set<lint32_t> &lineBreakPoints = break_points[obj_name];
    lineBreakPoints.insert(line);
}

void lpc_debugger_t::reset_break_point(const std::string &obj_name, lint32_t line)
{
    std::set<lint32_t> &lineBreakPoints = break_points[obj_name];
    lineBreakPoints.erase(line);
}

void lpc_debugger_t::clear_all_break_points()
{
    break_points.clear();
}

void lpc_debugger_t::step()
{
    mode = cmd_t::step;
    call_info_t *cur_ci = vm->get_call_info();
    const char *pc = cur_ci->savepc;
    cur_pc = pc + 1;
    vm->run();
}

void lpc_debugger_t::next()
{
    mode = cmd_t::next;

    std::vector<std::pair<luint32_t, luint32_t>> *lineMap;
    call_info_t *cur_ci = vm->get_call_info();
    const char *pc = cur_ci->savepc;
    const char *start_pc;

    if (cur_ci->father) {
        lineMap = &cur_ci->father->lineMap;
        start_pc = cur_ci->father->instructions;
    } else {
        lineMap = &cur_ci->cur_obj->get_proto()->lineMap;
        start_pc = cur_ci->cur_obj->get_proto()->instructions;
    }

    luint32_t offset = luint32_t(pc - start_pc);
    std::pair<luint32_t, luint32_t> *target = nullptr;
    if (pcoff == 0) {
        for (auto &it : *lineMap) {
            if (it.second >= offset) {
                target = &it;
                break;    
            }

            ++cur_line;
        }
        
        pcoff = target->second;
    } else {
        if (cur_line + 1 >= lineMap->size()) {
            mode = cmd_t::none;
        } else  {
            ++cur_line;
            target = &lineMap->at(cur_line);
            pcoff = lineMap->at(cur_line).second;
        }
    }
    
    if (target) cout << offset << ", " << target->second << endl;
    
    vm->run();
}

void lpc_debugger_t::run_out()
{
    call_info_t *cur_ci = vm->get_call_info();
    const char *pc = cur_ci->savepc;
    if (cur_ci->father) {
        cur_func = &cur_ci->father->func_table[cur_ci->funcIdx];
    } else {
        cur_func = &cur_ci->cur_obj->get_proto()->func_table[cur_ci->funcIdx];
    }
}

void lpc_debugger_t::continue_run()
{
    mode = cmd_t::continue_run;
    vm->run();
}

void lpc_debugger_t::exit()
{
    force_exit = true;
}

void lpc_debugger_t::start()
{
    vm->run_main();
    if (vm->get_call_info()) {
        cout << "the lpc programing language debugger is starting...\n";
        run();
    }
}

void lpc_debugger_t::fetch_cmd(cmd_def &cmd)
{
    static string str;
    str.clear();
    char ch = 0;

    while (true) {
        cout << "> ";
		while ((ch = cin.get())) {
			if (ch == '\n') break;
			str.push_back(ch);
		}

        // processing the command
        if (str == "s") {
            cmd.cmd = cmd_t::step;
            return;
        } else if (str == "n") {
            cmd.cmd = cmd_t::next;
            return;
        } else if (str == "c") {
            cmd.cmd = cmd_t::continue_run;
            return;
        } if (str == "exit") {
            cmd.cmd = cmd_t::exit;
            return;
        } else {
            cout << "unkonw command: " << str << endl;
        }

        str.clear();
    }
}

void lpc_debugger_t::run()
{
    cmd_def cmd;
    while (!force_exit) {
        fetch_cmd(cmd);
        auto iter = op_funcs.find(cmd.cmd);
        if (iter == op_funcs.end()) {
            continue;
        }

        iter->second();

        call_info_t *cur_ci = vm->get_call_info();
        if (!cur_ci) {
            break;
        }
    }
}
