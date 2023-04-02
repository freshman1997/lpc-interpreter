#ifndef __COMPILER_CODEGEN_H__
#define __COMPILER_CODEGEN_H__
#include <vector>
#include <unordered_map>
#include <string>
#include <set>
#include "token.h"
#include "type_decl.h"
#include "parser.h"

#define MAX_LOCAL (0x7fff)

void init_sfun(const char *sfunFile, Parser &parser);

struct EfunDecl
{
    std::string name;
    std::vector<DeclType> paramTypes;
    bool varargs = false;
    DeclType retType;
};

struct Func
{
    bool is_static = false;
    bool is_varargs = false;
    Token *name = nullptr;      // optional
    DeclType retType = DeclType::void_;
    Token *udef = nullptr;
    lint16_t nparams = 0;
    lint16_t nlocals = 0;
    luint32_t fromPc = 0;
    luint32_t toPc = 0;
    lint16_t idx = 0;
};

struct Local
{
    bool is_static = false;
    DeclType type = DeclType::none_;
    Token *name = nullptr;
    bool varargs = false;
    lint16_t idx = 0;
    bool is_arr = false;
    Token *declName = nullptr; // for user define type
};

struct ClassDecl
{
    bool is_static = false;
    Token *name = nullptr;
    luint16_t nfields = 0;
    // 位置：声明类型（如果是用户定义的class，则需要保存class的位置）
    std::vector<std::pair<std::pair<std::string, DeclType>, luint16_t>> fields;
};

class CodeGenerator
{
public:
    void generate(AbstractExpression *);

    void generate_ctor(AbstractExpression *);
    void generate_import(AbstractExpression *);
    void generate_decl(AbstractExpression *);
    void generate_unop(AbstractExpression *);
    void generate_binary(AbstractExpression *, bool fromAssign = false);
    void generate_if_else(AbstractExpression *, lint32_t forContinue, std::vector<lint32_t> &forBreaks);
    void generate_triple(AbstractExpression *);
    void generate_for(AbstractExpression *);
    void generate_foreach(AbstractExpression *);
    void generate_while(AbstractExpression *);
    void generate_do_while(AbstractExpression *);
    void generate_switch_case(AbstractExpression *, lint32_t);
    void generate_class(AbstractExpression *);
    void generate_index(AbstractExpression *, bool);
    void generate_call(AbstractExpression *, DeclType type = DeclType::none_);
    void generate_return(AbstractExpression *);
    void generate_func(AbstractExpression *);

    void oper_type_check(BinaryExpression *);

    template<typename T>
    lint16_t find_const(T &t, const std::vector<T> &con)
    {
        lint16_t i = 0;
        for (const auto &it : con) {
            if (t == it) return i;
            ++i;
        }

        return -1;
    }

    void push_code(luint8_t code)
    {
        opcodes.push_back(code);
    }

    lint32_t opcode_size()
    {
        return opcodes.size();
    }

    std::vector<luint8_t> & codes()
    {
        return this->opcodes;
    }

    std::vector<Func> & get_funcs()
    {
        return this->funcs;
    }

    std::vector<Local> * get_scope_locals(const string &name) 
    {
        if (locals.count(name)) {
            return &locals[name];
        }

        return nullptr;
    }

    std::pair<lint16_t, lint16_t> find_inherit_func(const string &name)
    {
        lint16_t idx = 0;
        for (auto &it : inherits) {
            CodeGenerator *g = reinterpret_cast<CodeGenerator *>(it->gen);
            std::vector<Func> &fs = g->get_funcs();
            for (auto &it1 : fs) {
                if (it1.name->strval == name) {
                    return {inherits.size() - idx - 1, it1.idx};
                }
            }
            ++idx;
        }

        return {-1, -1};
    }

    void set_parsed_files(vector<AbstractExpression *> *parsed_files)
    {
        this->parsed_files = parsed_files;
    }

    void dump();

private:
    bool on_var_decl = false;
    std::string cur_scope;
    std::string object_name;
    std::vector<luint8_t> var_init_codes;
    std::vector<luint8_t> opcodes;

    std::vector<ClassDecl> clazz;
    std::set<std::string> pre_decl_funcs;
    std::vector<Func> funcs;
    luint16_t create_idx = -1;
    luint16_t onload_in_idx = -1;
    luint16_t on_destruct_idx = -1;

    // 一个 block 就会有一层，主要是 switch case 下面的声明
    std::unordered_map<std::string, std::vector<Local>> locals;

    // constant pool
    std::vector<std::string> stringConsts;
    std::vector<lint32_t> intConsts;
    std::vector<lfloat32_t> floatConsts;

    std::vector<std::vector<std::pair<lint32_t, lint32_t>>> lookup_switch;

    vector<AbstractExpression *> *parsed_files;      // 前面语法分析的所有文件结果集，继承的时候从这里面查找、获取未继承函数、未覆盖的全局变量

    vector<DocumentExpression *> inherits;
};

#endif
