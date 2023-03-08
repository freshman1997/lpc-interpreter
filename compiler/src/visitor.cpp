#include <iostream>
#include "ast.h"

using namespace std;

void ConcretVisitor::visit(AbstractExpression *exp)
{
    switch (exp->get_type())
    {
    case ExpressionType::var_decl_:
    {
        visit_decl(exp);
        break;
    }
    case ExpressionType::value_:
    {
        visit_value(exp);
        break;
    }
    case ExpressionType::func_decl_:
    {
        visit_func_decl(exp);
        break;
    }
    case ExpressionType::document_: {
        visit_doc(exp);
        break;
    }

    default:
        break;
    }
}

void ConcretVisitor::visit_decl(AbstractExpression *exp)
{
    VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(exp);
    switch (var->dtype)
    {
    case DeclType::int_:
        cout << "int ";
        break;
    default:
        break;
    }

    cout << var->name->strval;
}

void ConcretVisitor::visit_value(AbstractExpression *exp)
{
    ValueExpression *val = dynamic_cast<ValueExpression *>(exp);
    cout << "<VALUE ";
    if (val->valType == 4 || val->valType == 2) {
        cout << val->val.sval;
    } else if (val->valType == 0) {
        cout << val->val.ival;
    } else if (val->valType == 1) {
        cout << val->val.dval;
    } else if (val->valType == 3){
        cout << val->val.ival ? "true" : "false";
    }
    cout << " >\n";
}

void ConcretVisitor::visit_assign(AbstractExpression *exp)
{
    BinaryExpression *bin = dynamic_cast<BinaryExpression *>(exp);
    cout << "<ASSIGN ";
    visit(bin->l);
    cout << '=';
    visit(bin->r);
    cout << " >\n";
}

void ConcretVisitor::visit_func_decl(AbstractExpression *exp)
{
    FunctionDeclExpression *func = dynamic_cast<FunctionDeclExpression *>(exp);
    cout << "<FUNC " << func->name->strval << (func->params.empty() ? "" : " params: ");
    int i = 0;
    for(auto &it : func->params) {
        visit(it);
        if (i < func->params.size() - 1) cout << ", ";
    }
    cout << " -> void {\n";
    for (auto &it : func->body) {
        visit(it);
    }
    cout << "}\n";
}

void ConcretVisitor::visit_index(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_oper(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_new(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_return(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_for_normal(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_foreach(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_while(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_do_while(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_switch_case(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_class(AbstractExpression *exp)
{
}

void ConcretVisitor::visit_doc(AbstractExpression *exp)
{
    DocumentExpression *doc = dynamic_cast<DocumentExpression *>(exp);
    for (auto &it : doc->contents) {
        visit(it);
    }
}
