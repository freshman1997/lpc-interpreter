#ifndef __VM_DEBUG_H__
#define __VM_DEBUG_H__

/* 
    调试需要的信息：
        1、断点信息
        2、当前执行的对象和函数信息，包括对象名称、函数名和函数内部变量定义的名称，方便查找
        3、行数对于的指令的信息

    需要支持的调试功能：
        1、单步运行
        2、按行运行
        3、执行完函数回到上层调用函数
        4、可以打印当前栈帧内或上层其他栈帧的变量数据信息
        5、查看当前执行栈帧信息
        6、可以切换战阵
*/
#include <string>
#include <list>
#include <unordered_map>
#include <vector>
#include <set>
#include <functional>

#include "lpc.h"

class lpc_vm_t;
class function_proto_t;

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
    lint32_t parm1;
    lint32_t param2;
    const char *sparam1 = nullptr;
    const char *sparam2 = nullptr;
};

class lpc_debugger_t
{
public:
    lpc_debugger_t(lpc_vm_t *_vm);
    bool can_run();

public:
    void set_break_point(const std::string &, lint32_t line);
    void reset_break_point(const std::string &, lint32_t line);
    void clear_all_break_points();
    void start();

private:
    void step();
    void next();
    void run_out();
    void continue_run();
    void exit();

    void fetch_cmd(cmd_def &);
    void run();

private:
    std::unordered_map<std::string, std::set<lint32_t>> break_points;
    typedef void (*op_func)();
    std::unordered_map<cmd_t, std::function<void ()>> op_funcs;

    lpc_vm_t *vm;
    const char *cur_pc = nullptr;
    function_proto_t *cur_func = nullptr;
    const cmd_def *cur_cmd = nullptr;

    lint32_t cur_line = 0;
    lint32_t tmp_line = 0;
    luint32_t pcoff = 0;
    
    bool force_exit = false;
    cmd_t mode = cmd_t::none;
};

#endif
