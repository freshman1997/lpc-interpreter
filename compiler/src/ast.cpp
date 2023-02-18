#include "ast.h"

void VarDeclExpression::accept(Visitor *visitor)
{

}

string VarDeclExpression::get_name()
{
    return "DECL";
}

void VarDeclExpression::pre_print(int deep)
{

}

ExpressionType VarDeclExpression::get_type()
{
    return ExpressionType::var_decl;
}


////////////////////////////////////////
void ValueExpression::accept(Visitor *visitor)
{
    
}

string ValueExpression::get_name()
{
    return "DECL";
}

void ValueExpression::pre_print(int deep)
{

}

ExpressionType ValueExpression::get_type()
{
    return ExpressionType::var_decl;
}
/////////////////////////////////////////////

void ConstructExpression::accept(Visitor *visitor)
{
    
}

string ConstructExpression::get_name()
{
    return "DECL";
}

void ConstructExpression::pre_print(int deep)
{

}

ExpressionType ConstructExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////////////
void BinaryExpression::accept(Visitor *visitor)
{
    
}

string BinaryExpression::get_name()
{
    return "DECL";
}

void BinaryExpression::pre_print(int deep)
{

}

ExpressionType BinaryExpression::get_type()
{
    return ExpressionType::oper;
}

////////////////////////////////////////////

void UnaryExpression::accept(Visitor *visitor)
{
    
}

string UnaryExpression::get_name()
{
    return "DECL";
}

void UnaryExpression::pre_print(int deep)
{

}

ExpressionType UnaryExpression::get_type()
{
    return ExpressionType::oper;
}

////////////////////////////////////////////

void AssignExpression::accept(Visitor *visitor)
{
    
}

string AssignExpression::get_name()
{
    return "DECL";
}

void AssignExpression::pre_print(int deep)
{

}

ExpressionType AssignExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void FunctionDeclExpression::accept(Visitor *visitor)
{
    
}

string FunctionDeclExpression::get_name()
{
    return "DECL";
}

void FunctionDeclExpression::pre_print(int deep)
{

}

ExpressionType FunctionDeclExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void CallExpression::accept(Visitor *visitor)
{
    
}

string CallExpression::get_name()
{
    return "DECL";
}

void CallExpression::pre_print(int deep)
{

}

ExpressionType CallExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void IndexExpression::accept(Visitor *visitor)
{
    
}

string IndexExpression::get_name()
{
    return "DECL";
}

void IndexExpression::pre_print(int deep)
{

}

ExpressionType IndexExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void ForNormalExpression::accept(Visitor *visitor)
{
    
}

string ForNormalExpression::get_name()
{
    return "DECL";
}

void ForNormalExpression::pre_print(int deep)
{

}

ExpressionType ForNormalExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void ForeachExpression::accept(Visitor *visitor)
{
    
}

string ForeachExpression::get_name()
{
    return "DECL";
}

void ForeachExpression::pre_print(int deep)
{

}

ExpressionType ForeachExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void ReturnExpression::accept(Visitor *visitor)
{
    
}

string ReturnExpression::get_name()
{
    return "DECL";
}

void ReturnExpression::pre_print(int deep)
{

}

ExpressionType ReturnExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void BreakExpression::accept(Visitor *visitor)
{
    
}

string BreakExpression::get_name()
{
    return "DECL";
}

void BreakExpression::pre_print(int deep)
{

}

ExpressionType BreakExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void whileExpression::accept(Visitor *visitor)
{
    
}

string whileExpression::get_name()
{
    return "DECL";
}

void whileExpression::pre_print(int deep)
{

}

ExpressionType whileExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void DoWhileExpression::accept(Visitor *visitor)
{
    
}

string DoWhileExpression::get_name()
{
    return "DECL";
}

void DoWhileExpression::pre_print(int deep)
{

}

ExpressionType DoWhileExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void CaseExpression::accept(Visitor *visitor)
{
    
}

string CaseExpression::get_name()
{
    return "DECL";
}

void CaseExpression::pre_print(int deep)
{

}

ExpressionType CaseExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void SwitchCaseExpression::accept(Visitor *visitor)
{
    
}

string SwitchCaseExpression::get_name()
{
    return "DECL";
}

void SwitchCaseExpression::pre_print(int deep)
{

}

ExpressionType SwitchCaseExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void ClassExpression::accept(Visitor *visitor)
{
    
}

string ClassExpression::get_name()
{
    return "DECL";
}

void ClassExpression::pre_print(int deep)
{

}

ExpressionType ClassExpression::get_type()
{
    return ExpressionType::var_decl;
}

/////////////////////////////////////

void DocumentExpression::accept(Visitor *visitor)
{
    
}

string DocumentExpression::get_name()
{
    return "DECL";
}

void DocumentExpression::pre_print(int deep)
{

}

ExpressionType DocumentExpression::get_type()
{
    return ExpressionType::var_decl;
}
