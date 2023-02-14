#ifndef __COMPILER_AST_H__
#define __COMPILER_AST_H__
#include <vector>
#include <string>

#include "visitor.h"

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

};

union Value
{
    int ival;
    double dval;
    const char *sval;
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
    AbstractExpression() {}
    AbstractExpression(ExpressionType t) : type(t) {}
protected:
    ExpressionType type;
};

class VarDeclExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
private:
    DeclType type;
    string type_name;
    string name;
};

class ValueExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
private:
    Value *val = nullptr;
};

class OperationExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
private:
    int oper = 0;
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
private:
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
private:
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
private:
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
private:
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
private:
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
private:
    vector<AbstractExpression *> fields;
};

class DocumentExpression : public AbstractExpression
{
public:
    virtual void accept(Visitor *visitor);
    virtual string get_name();
    virtual void pre_print(int deep);
    virtual ExpressionType get_type();
private:
    string file_name;
    // funcs, fields
    vector<AbstractExpression *> contents;
};

#endif