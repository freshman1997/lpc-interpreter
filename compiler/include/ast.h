#ifndef __COMPILER_AST_H__
#define __COMPILER_AST_H__
#include <vector>
#include <string>

#include "visitor.h"
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
    func_,
    class_,
    user_define_,
};

class ExpressionVisitor
{
public:
    virtual void accept(Visitor *visitor) = 0;
    virtual ExpressionType get_type() = 0;
    virtual string get_name() = 0;
    virtual void pre_print(int deep) = 0;

    virtual ~ExpressionVisitor() {};
};

class AbstractExpression : public ExpressionVisitor
{
};

class VarDeclExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

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
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
    ~ConstructExpression() {}

    int type;       // 0 数组， 1 哈希表
    vector<AbstractExpression *> body;
};

class ValueExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
    virtual ~ValueExpression(){};

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
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    TokenKind op = TokenKind::k_none;
    AbstractExpression *exp = nullptr;
};


class BinaryExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    TokenKind oper = TokenKind::k_none;
    AbstractExpression *l = nullptr;
    AbstractExpression *r = nullptr;
};

class FunctionDeclExpression : public VarDeclExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    bool lambda = false;
    bool is_varargs = false;
    DeclType returnType = DeclType::void_;
    vector<AbstractExpression *> params;
    vector<AbstractExpression *> body;
};

class CallExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *callee = nullptr;
    vector<AbstractExpression *> params;
};

class IndexExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    bool toend = false;
    AbstractExpression *l = nullptr;
    AbstractExpression *idx = nullptr;
    AbstractExpression *idx1 = nullptr; // for sub array
};

class NewExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *id = nullptr;
    vector<AbstractExpression *> inits;
};

class ImportExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    vector<Token *> path;
    Token *asName = nullptr;
};

class TripleExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *cond = nullptr;
    AbstractExpression *first = nullptr;
    AbstractExpression *second = nullptr;
};

class IfExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
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
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    vector<AbstractExpression *> inits;
    vector<AbstractExpression *> conditions;
    vector<AbstractExpression *> operations;
    vector<AbstractExpression *> body;
};

class ForeachExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    vector<AbstractExpression*> decls;
    AbstractExpression *container = nullptr;
    vector<AbstractExpression *> body;
};

class ReturnExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *ret = nullptr;
};

class BreakExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
};

class ContinueExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
};

class whileExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *cond = nullptr;
    vector<AbstractExpression *> body;
};

class DoWhileExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *cond = nullptr;
    vector<AbstractExpression *> body;
};

class CaseExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *caser = nullptr;
    vector<vector<AbstractExpression *>> bodys;
    vector<AbstractExpression *> body;
};

class DefaultExpression : public CaseExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
};

class SwitchCaseExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *selector = nullptr;
    vector<AbstractExpression *> cases;
};

class ClassExpression : public VarDeclExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    Token *className= nullptr;
    vector<AbstractExpression *> fields;
};

class DocumentExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
    ~DocumentExpression() {}

    string file_name;
    // funcs, fields
    vector<AbstractExpression *> contents;
};

#endif