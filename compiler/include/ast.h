#ifndef __COMPILER_AST_H__
#define __COMPILER_AST_H__
#include <vector>
#include <string>

#include "visitor.h"
#include "token.h"

using namespace std;

enum class ExpressionType
{
    var_decl,
    value,
    assign,
    oper,
    index,
    new_,
    pointor,
    for_normal,
    foreach,
    while_,
    do_while,
    break_,
    switch_case,
    case_,
    func_decl,
    call,
    return_,
    class_,
    document,
};

enum class DeclType
{
    int_,
    float_,
    class_,
    bool_,
    mapping_,
    object_,
    string_,
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
public:
    ExpressionType type;
    virtual ~AbstractExpression() {};
};

class VarDeclExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    bool is_static = false;
    DeclType dtype;
    string type_name;
    string name;
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

    union Value
    {
        int ival;
        double dval;
        Token *sval;
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

    TokenKind op;
    AbstractExpression *exp;
};


class BinaryExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    TokenKind oper;
    AbstractExpression *l = nullptr;
    AbstractExpression *r = nullptr;
};

class AssignExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

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
private:
    vector<AbstractExpression *> params;
};

class CallExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
private:
    string name;
    vector<AbstractExpression *> params;
};

class IndexExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
private:
    AbstractExpression *l;
    AbstractExpression *idx;
};

class ForNormalExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
private:
    AbstractExpression *inits;
    AbstractExpression *conditions;
    AbstractExpression *operations;
    vector<AbstractExpression *> body;
};

class ForeachExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
private:
    AbstractExpression *one;
    AbstractExpression *two;
    AbstractExpression *container;
    vector<AbstractExpression *> body;
};

class ReturnExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
private:
    AbstractExpression *ret;
};

class BreakExpression : public AbstractExpression
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

    AbstractExpression *cond;
    vector<AbstractExpression *> body;
};

class DoWhileExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *cond;
    vector<AbstractExpression *> body;
};

class CaseExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *caser;
    AbstractExpression *break_;
};

class SwitchCaseExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

    AbstractExpression *selector;
    vector<AbstractExpression *> cases;
};

class ClassExpression : public VarDeclExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();

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