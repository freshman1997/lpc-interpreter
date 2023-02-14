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
    string name;
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
    macro_handler_fn *handler;
};

class Parser
{
public:
    void init_base_include(const vector<string> &includes);
    void preprocessing(Token *);
    ExpressionVisitor * parse(const char *filename);

private:
    // 已经读取的头文件部分等缓存
    vector<Token *> caches;
    unordered_map<string, Macro *> macros;
};


#endif
