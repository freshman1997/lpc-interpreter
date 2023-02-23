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
    return ExpressionType::var_decl_;
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
    return ExpressionType::value_;
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
    return ExpressionType::construct_;
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
    return ExpressionType::oper_;
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
    return ExpressionType::uop_;
}

////////////////////////////////////////////

void IfExpression::accept(Visitor *visitor)
{
    
}

string IfExpression::get_name()
{
    return "DECL";
}

void IfExpression::pre_print(int deep)
{

}

ExpressionType IfExpression::get_type()
{
    return ExpressionType::if_;
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
    return ExpressionType::func_decl_;
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
    return ExpressionType::call_;
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
    return ExpressionType::index_;
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
    return ExpressionType::for_normal_;
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
    return ExpressionType::foreach_;
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
    return ExpressionType::return_;
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
    return ExpressionType::break_;
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
    return ExpressionType::while_;
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
    return ExpressionType::do_while_;
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
    return ExpressionType::case_;
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
    return ExpressionType::switch_case_;
}

/////////////////////////////////////

void ClassExpression::accept(Visitor *visitor)
{
    
}

string ClassExpression::get_name()
{
    return "switch";
}

void ClassExpression::pre_print(int deep)
{

}

ExpressionType ClassExpression::get_type()
{
    return ExpressionType::class_;
}

/////////////////////////////////////

void TripleExpression::accept(Visitor *visitor)
{
    
}

string TripleExpression::get_name()
{
    return "switch";
}

void TripleExpression::pre_print(int deep)
{

}

ExpressionType TripleExpression::get_type()
{
    return ExpressionType::class_;
}
/////////////////////////////////////

void DocumentExpression::accept(Visitor *visitor)
{
    
}

string DocumentExpression::get_name()
{
    return "document";
}

void DocumentExpression::pre_print(int deep)
{

}

ExpressionType DocumentExpression::get_type()
{
    return ExpressionType::document_;
}

/////////////////////////////////////

void ContinueExpression::accept(Visitor *visitor)
{
    
}

string ContinueExpression::get_name()
{
    return "continue";
}

void ContinueExpression::pre_print(int deep)
{

}

ExpressionType ContinueExpression::get_type()
{
    return ExpressionType::continue_;
}
/////////////////////////////////////

void NewExpression::accept(Visitor *visitor)
{
    
}

string NewExpression::get_name()
{
    return "continue";
}

void NewExpression::pre_print(int deep)
{

}

ExpressionType NewExpression::get_type()
{
    return ExpressionType::new_;
}

/////////////////////////////////////

void DefaultExpression::accept(Visitor *visitor)
{
    
}

string DefaultExpression::get_name()
{
    return "default";
}

void DefaultExpression::pre_print(int deep)
{

}

ExpressionType DefaultExpression::get_type()
{
    return ExpressionType::default_;
}

/////////////////////////////////////

void ImportExpression::accept(Visitor *visitor)
{
    
}

string ImportExpression::get_name()
{
    return "import";
}

void ImportExpression::pre_print(int deep)
{

}

ExpressionType ImportExpression::get_type()
{
    return ExpressionType::import_;
}