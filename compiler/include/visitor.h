#ifndef __COMPILER_VISITOR_H__
#define __COMPILER_VISITOR_H__
#include <string>
#include <vector>

using namespace std;

class AbstractExpression;

class Visitor
{
public:
    virtual void visit(AbstractExpression *exp) = 0;
    virtual ~Visitor() {};
};

class ConcretVisitor : public Visitor
{
public:
    virtual void visit(AbstractExpression *exp);
private:
    void visit_decl(AbstractExpression *decl);

    void visit_value();

    void visit_assign();

    void visit_func_decl();

    void visit_index();
    
    void visit_oper();

    void visit_new();

    void visit_return();

    void visit_for_normal();

    void visit_foreach();

    void visit_while();

    void visit_do_while();

    void visit_switch_case();

    void visit_class();

    void visit_doc();

};


#endif
