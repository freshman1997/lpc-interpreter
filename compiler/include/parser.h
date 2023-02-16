#ifndef __COMPILER_PARSER_H__
#define __COMPILER_PARSER_H__
#include <unordered_map>

#include "scanner.h"
#include "ast.h"

struct HidenSet
{
    string name;
    HidenSet *next;
};

struct MacroParam
{
    string name;
    MacroParam *next;
};

struct MacroArg
{
    bool is_var_args;
    Token *tok;
};

typedef Token *macro_handler_fn(Token *);

struct Macro
{
    string name;
    bool isobj_like;
    MacroParam *params;
    Token *body;
    Token *end;
    macro_handler_fn *handler;
};

string get_cwd();
string get_father();

class Parser
{
public:
    void init_base_include(const vector<string> &includes);
    Token * preprocessing(Token *);
    ExpressionVisitor * parse(const char *filename);
    Token * parse_file(const char *filename);

    void enter_dir();
    void exit_dir();

private:
    // 已经读取的头文件部分等缓存
    string cur_compile_dir;
    unordered_map<string, Token *> incs;
    unordered_map<string, Macro *> macros;
};


#endif
