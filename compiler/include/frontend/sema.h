#ifndef LPC_FRONTEND_SEMA_H
#define LPC_FRONTEND_SEMA_H

#include <string>
#include <unordered_map>
#include <vector>

#include "frontend/ast.h"
#include "frontend/diagnostic.h"

namespace lpc {
namespace frontend {

struct FunctionSemanticInfo {
    std::string name;
    std::vector<std::string> locals;
    std::vector<std::string> params;
    std::vector<std::string> captures;
    std::vector<int> capture_source_kind;
    std::vector<int> capture_source_index;
};

struct SemanticModel {
    std::unordered_map<const Node *, std::string> resolved_symbol;
    std::unordered_map<const FunctionDecl *, FunctionSemanticInfo> functions;
    std::unordered_map<const LambdaExpr *, FunctionSemanticInfo> lambdas;
    std::vector<std::string> global_variables;
    std::vector<std::string> class_order;
    std::unordered_map<std::string, std::vector<std::string>> class_fields;
    std::unordered_map<const Expr *, std::string> expr_type;
    std::unordered_map<const Expr *, int> member_field_index;
};

class Sema {
public:
    explicit Sema(DiagnosticSink *diag) : diag_(diag) {}

    SemanticModel Analyze(const Module &module);

    static bool IsEfun(const std::string &name);
    static int EfunIndex(const std::string &name);

private:
    void VisitStmt(const Stmt *stmt);
    void VisitExpr(const Expr *expr);
    void EnterScope();
    void ExitScope();
    void Declare(const std::string &name, const std::string &declared_type = "mixed");
    bool Resolve(const std::string &name) const;
    std::string ResolveType(const std::string &name) const;
    void AnalyzeFunction(const FunctionDecl *func);
    void AnalyzeLambda(const LambdaExpr *lambda, const FunctionDecl *owner);
    int loop_depth_ = 0;
    int switch_depth_ = 0;
    std::unordered_map<std::string, int> function_arity_;

    struct Scope {
        std::unordered_map<std::string, bool> names;
        std::unordered_map<std::string, std::string> types;
        std::unordered_map<std::string, int> local_index;
        bool is_function_scope = false;
        const FunctionDecl *owner = nullptr;
    };

    std::vector<Scope> scopes_;
    DiagnosticSink *diag_ = nullptr;
    SemanticModel model_;
    const FunctionDecl *current_function_ = nullptr;
};

} // namespace frontend
} // namespace lpc

#endif
