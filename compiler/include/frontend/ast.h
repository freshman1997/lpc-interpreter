#ifndef LPC_FRONTEND_ast_H
#define LPC_FRONTEND_ast_H

#include <memory>
#include <string>
#include <vector>

#include "frontend/source.h"

namespace lpc {
namespace frontend {

enum class NodeKind {
    Module,
    FunctionDecl,
    ClassDecl,
    InheritDecl,
    VarDecl,
    Block,
    IfStmt,
    WhileStmt,
    ForStmt,
    ForeachStmt,
    SwitchStmt,
    CaseStmt,
    BreakStmt,
    ContinueStmt,
    CatchStmt,
    ReturnStmt,
    ExprStmt,
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    MemberExpr,
    IndexExpr,
    Identifier,
    Number,
    Float,
    String,
    LambdaExpr,
    NewExpr,
    TernaryExpr,
    DoWhileStmt,
    MappingLiteralExpr,
};

struct Node {
    explicit Node(NodeKind k) : kind(k) {}
    virtual ~Node() {}
    NodeKind kind;
    SourceSpan span;
};

struct Expr : public Node {
    explicit Expr(NodeKind k) : Node(k) {}
};

struct Stmt : public Node {
    explicit Stmt(NodeKind k) : Node(k) {}
};

struct IdentifierExpr : public Expr {
    IdentifierExpr() : Expr(NodeKind::Identifier) {}
    std::string name;
};

struct NumberExpr : public Expr {
    NumberExpr() : Expr(NodeKind::Number) {}
    std::string literal;
};

struct FloatExpr : public Expr {
    FloatExpr() : Expr(NodeKind::Float) {}
    std::string literal;
};

struct StringExpr : public Expr {
    StringExpr() : Expr(NodeKind::String) {}
    std::string literal;
};

struct BinaryExpr : public Expr {
    BinaryExpr() : Expr(NodeKind::BinaryExpr) {}
    std::string op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

struct UnaryExpr : public Expr {
    UnaryExpr() : Expr(NodeKind::UnaryExpr) {}
    std::string op;
    bool is_postfix = false;
    std::unique_ptr<Expr> operand;
};

struct CallExpr : public Expr {
    CallExpr() : Expr(NodeKind::CallExpr) {}
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
};

struct MemberExpr : public Expr {
    MemberExpr() : Expr(NodeKind::MemberExpr) {}
    std::unique_ptr<Expr> object;
    std::string member;
};

struct IndexExpr : public Expr {
    IndexExpr() : Expr(NodeKind::IndexExpr) {}
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    std::unique_ptr<Expr> index_end;
    bool is_range = false;
};

struct LambdaExpr : public Expr {
    LambdaExpr() : Expr(NodeKind::LambdaExpr) {}
    std::vector<std::string> params;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct NewExpr : public Expr {
    NewExpr() : Expr(NodeKind::NewExpr) {}
    std::string class_name;
    std::vector<std::unique_ptr<Expr>> array_items;
    bool is_array_literal = false;
};

struct TernaryExpr : public Expr {
    TernaryExpr() : Expr(NodeKind::TernaryExpr) {}
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Expr> then_expr;
    std::unique_ptr<Expr> else_expr;
};

struct DoWhileStmt : public Stmt {
    DoWhileStmt() : Stmt(NodeKind::DoWhileStmt) {}
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> body;
};

struct MappingLiteralExpr : public Expr {
    MappingLiteralExpr() : Expr(NodeKind::MappingLiteralExpr) {}
    std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> pairs;
};

struct VarDeclStmt : public Stmt {
    VarDeclStmt() : Stmt(NodeKind::VarDecl) {}
    std::string name;
    std::string declared_type;
    bool is_pointer = false;
    std::vector<std::string> decorators;
    std::unique_ptr<Expr> init;
};

struct ReturnStmt : public Stmt {
    ReturnStmt() : Stmt(NodeKind::ReturnStmt) {}
    std::unique_ptr<Expr> value;
};

struct BreakStmt : public Stmt {
    BreakStmt() : Stmt(NodeKind::BreakStmt) {}
};

struct ContinueStmt : public Stmt {
    ContinueStmt() : Stmt(NodeKind::ContinueStmt) {}
};

struct CatchStmt : public Stmt {
    CatchStmt() : Stmt(NodeKind::CatchStmt) {}
    std::unique_ptr<Stmt> body;
};

struct IfStmt : public Stmt {
    IfStmt() : Stmt(NodeKind::IfStmt) {}
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> then_branch;
    std::unique_ptr<Stmt> else_branch;
};

struct WhileStmt : public Stmt {
    WhileStmt() : Stmt(NodeKind::WhileStmt) {}
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> body;
};

struct ForStmt : public Stmt {
    ForStmt() : Stmt(NodeKind::ForStmt) {}
    std::unique_ptr<Stmt> init;
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Expr> post;
    std::unique_ptr<Stmt> body;
};

struct ForeachStmt : public Stmt {
    ForeachStmt() : Stmt(NodeKind::ForeachStmt) {}
    std::string first_name;
    std::string second_name;
    std::unique_ptr<Expr> container;
    std::unique_ptr<Stmt> body;
};

struct CaseStmt : public Stmt {
    CaseStmt() : Stmt(NodeKind::CaseStmt) {}
    bool is_default = false;
    std::unique_ptr<Expr> selector;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct SwitchStmt : public Stmt {
    SwitchStmt() : Stmt(NodeKind::SwitchStmt) {}
    std::unique_ptr<Expr> selector;
    std::vector<std::unique_ptr<CaseStmt>> cases;
};

struct ExprStmt : public Stmt {
    ExprStmt() : Stmt(NodeKind::ExprStmt) {}
    std::unique_ptr<Expr> expr;
};

struct BlockStmt : public Stmt {
    BlockStmt() : Stmt(NodeKind::Block) {}
    std::vector<std::unique_ptr<Stmt>> stmts;
};

struct FunctionDecl : public Stmt {
    FunctionDecl() : Stmt(NodeKind::FunctionDecl) {}
    std::string name;
    std::string return_type;
    bool return_is_pointer = false;
    std::vector<std::string> decorators;
    std::vector<std::string> param_types;
    std::vector<bool> param_is_pointer;
    std::vector<std::string> params;
    std::vector<std::unique_ptr<Stmt>> body;
    bool is_lambda = false;
};

struct ClassDecl : public Stmt {
    ClassDecl() : Stmt(NodeKind::ClassDecl) {}
    std::string name;
    std::string parent_name;
    std::vector<std::string> fields;
};

struct InheritDecl : public Stmt {
    InheritDecl() : Stmt(NodeKind::InheritDecl) {}
    std::string parent_name;
};

struct Module : public Node {
    Module() : Node(NodeKind::Module) {}
    std::vector<std::unique_ptr<Stmt>> decls;
};

} // namespace frontend
} // namespace lpc

#endif
