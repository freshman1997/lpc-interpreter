#ifndef __COMPILER_AST_H__
#define __COMPILER_AST_H__
#include <vector>
#include <string>

#include "token.h"

using namespace std;

enum class ExpressionType
{
    var_decl_,
    value_,
    uop_,
    oper_,
    index_,
    call_,
    new_,

    triple_,
    if_,
    for_normal_,
    foreach_,
    while_,
    do_while_,
    switch_case_,
    case_,
    default_,
    func_decl_,
    break_,
    continue_,
    return_,
    class_,
    construct_,
    import_,
    document_,
};

enum class DeclType
{
    none_,
    void_,
    int_,
    float_,
    bool_,
    mapping_,
    mixed_,
    object_,
    string_,
    array_,
    buffer_,
    func_,
    class_,
    user_define_,
    varargs_,
};

class AbstractExpression {
public:
    lint32_t fromLine = 0;
    lint32_t toLine = 0;
    virtual ExpressionType get_type() = 0;
    virtual ~AbstractExpression(){}
};

class VarDeclExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::var_decl_; };
    bool varargs = false;
    bool is_arr = false;
    bool is_static = false;
    DeclType dtype = DeclType::none_;
    Token *user_define_type = nullptr;
    Token *name = nullptr;
};

class ConstructExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::construct_; };
    int ctype;       // 0 数组， 1 哈希表
    vector<AbstractExpression *> body;
};

class ValueExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::value_; };

    bool is_arr = false;    // for 连续声明
    bool is_var_arg = false;
    union Value
    {
        int ival;
        float dval;
        Token *sval = nullptr;
        ~Value(){}
    } val;

    // 0 整形，1 浮点，2 字符串，3 布尔, 4 标识符
    int valType;
};

class UnaryExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::uop_; };


    TokenKind op = TokenKind::k_none;
    AbstractExpression *exp = nullptr;
};


class BinaryExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::oper_; };

    TokenKind oper = TokenKind::k_none;
    AbstractExpression *l = nullptr;
    AbstractExpression *r = nullptr;
};

class FunctionDeclExpression : public VarDeclExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::func_decl_; };

    bool is_arr = false;
    bool lambda = false;
    bool is_varargs = false;
    DeclType returnType = DeclType::void_;
    vector<AbstractExpression *> params;
    vector<AbstractExpression *> body;
};

class CallExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::call_; };

    bool callInherit = false;
    AbstractExpression *callee = nullptr;
    vector<AbstractExpression *> params;
};

class IndexExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::index_; };

    bool toend = false;
    AbstractExpression *l = nullptr;
    AbstractExpression *idx = nullptr;
    AbstractExpression *idx1 = nullptr; // for sub array
};

class NewExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::new_; };

    AbstractExpression *id = nullptr;
    vector<AbstractExpression *> inits;
};

class ImportExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::import_; };

    vector<Token *> path;
    Token *asName = nullptr;
};

class TripleExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::triple_; };

    AbstractExpression *cond = nullptr;
    AbstractExpression *first = nullptr;
    AbstractExpression *second = nullptr;
};

class IfExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::if_; };

    struct If {
        int type;   // 0 if, 1 else if, 2 else
        AbstractExpression *cond = nullptr;
        vector<AbstractExpression *> body;
    };

    vector<If> exps;
}; 

class ForNormalExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::for_normal_; };

    vector<AbstractExpression *> inits;
    vector<AbstractExpression *> conditions;
    vector<AbstractExpression *> operations;
    vector<AbstractExpression *> body;
};

class ForeachExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::foreach_; };

    vector<AbstractExpression*> decls;
    AbstractExpression *container = nullptr;
    vector<AbstractExpression *> body;
};

class ReturnExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::return_; };

    AbstractExpression *ret = nullptr;
};

class BreakExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::break_; };
};

class ContinueExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::continue_; };
};

class whileExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::while_; };

    AbstractExpression *cond = nullptr;
    vector<AbstractExpression *> body;
};

class DoWhileExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::do_while_; };

    AbstractExpression *cond = nullptr;
    vector<AbstractExpression *> body;
};

class CaseExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::case_; };

    AbstractExpression *caser = nullptr;
    vector<AbstractExpression *> bodys;
};

class DefaultExpression : public CaseExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::default_; };
};

class SwitchCaseExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::switch_case_; };

    AbstractExpression *selector = nullptr;
    vector<AbstractExpression *> cases;
};

class ClassExpression : public VarDeclExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::class_; };

    Token *className= nullptr;
    vector<AbstractExpression *> fields;
};

class DocumentExpression : public AbstractExpression
{
public:
    virtual ExpressionType get_type() { return ExpressionType::document_; };

    string file_name;
    // funcs, fields
    vector<AbstractExpression *> contents;
    string inherit;
    void * gen = nullptr;
};

#endif