#include "ast.h"

void ConcretVisitor::visit(AbstractExpression *exp)
{
    switch (exp->get_type()) {
        case ExpressionType::var_decl_: {

            break;
        }
        case ExpressionType::value_: {
            visit_value();
            break;
        }

        default: break;
    }
}

void ConcretVisitor::visit_decl(AbstractExpression *decl)
{

}

void ConcretVisitor::visit_value()
{

}

void ConcretVisitor::visit_assign()
{

}

void ConcretVisitor::visit_func_decl()
{

}

void ConcretVisitor::visit_index()
{

}

void ConcretVisitor::visit_oper()
{

}

void ConcretVisitor::visit_new()
{

}

void ConcretVisitor::visit_return()
{

}

void ConcretVisitor::visit_for_normal()
{

}

void ConcretVisitor::visit_foreach()
{

}

void ConcretVisitor::visit_while()
{

}

void ConcretVisitor::visit_do_while()
{

}

void ConcretVisitor::visit_switch_case()
{

}

void ConcretVisitor::visit_class()
{

}

void ConcretVisitor::visit_doc()
{

}
