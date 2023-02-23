#ifndef __COMPILER_PARSER_H__
#define __COMPILER_PARSER_H__
#include <unordered_map>

#include "scanner.h"
#include "ast.h"

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

typedef void macro_handler_fn(Token *, Token *, Token *);

struct Macro
{
    string name;
    bool isobj_like;
    MacroParam *params;
    int psize = 0;
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

    pair<bool, Macro *> find_macro(const string &);
    void enter_dir();
    void exit_dir();
    void set_compile_file(const char *filename);
    void add_built_in_macro();

    unordered_map<string, Macro *> * get_macros();

private:
    // 已经读取的头文件部分等缓存
    string cur_compile_dir;
    string cur_file;
    string sys_inc_dir;
    int error_try = 3;
    unordered_map<string, Token *> incs;
    unordered_map<string, Macro *> builtInMacro;
    unordered_map<string, Macro *> macros;
};


#endif
