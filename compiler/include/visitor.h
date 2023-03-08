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
    void visit_decl(AbstractExpression *exp);

    void visit_value(AbstractExpression *exp);

    void visit_assign(AbstractExpression *exp);

    void visit_func_decl(AbstractExpression *exp);

    void visit_index(AbstractExpression *exp);
    
    void visit_oper(AbstractExpression *exp);

    void visit_new(AbstractExpression *exp);

    void visit_return(AbstractExpression *exp);

    void visit_for_normal(AbstractExpression *exp);

    void visit_foreach(AbstractExpression *exp);

    void visit_while(AbstractExpression *exp);

    void visit_do_while(AbstractExpression *exp);

    void visit_switch_case(AbstractExpression *exp);

    void visit_class(AbstractExpression *exp);

    void visit_doc(AbstractExpression *exp);

};


#endif
