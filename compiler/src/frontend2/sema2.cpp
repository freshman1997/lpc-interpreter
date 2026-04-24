#include "frontend2/sema2.h"

#include <set>

namespace lpc {
namespace frontend2 {

static bool IsAssignOp(const std::string &op) {
    return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
        op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=";
}

static bool IsNumericType(const std::string &t) {
    return t == "int" || t == "float";
}

static bool IsTypeAssignable(const std::string &lhs, const std::string &rhs) {
    if (lhs.empty() || rhs.empty()) {
        return true;
    }
    if (lhs == "mixed" || rhs == "mixed") {
        return true;
    }
    if (lhs == rhs) {
        return true;
    }
    if (IsNumericType(lhs) && IsNumericType(rhs)) {
        return true;
    }
    return false;
}

static void CollectLocalDeclsFromStmt(const Stmt *stmt, std::set<std::string> &locals) {
    if (!stmt) {
        return;
    }

    switch (stmt->kind) {
    case NodeKind::VarDecl: {
        const VarDeclStmt *vd = static_cast<const VarDeclStmt *>(stmt);
        locals.insert(vd->name);
        break;
    }
    case NodeKind::ClassDecl:
        break;
    case NodeKind::Block: {
        const BlockStmt *b = static_cast<const BlockStmt *>(stmt);
        for (const auto &it : b->stmts) {
            CollectLocalDeclsFromStmt(it.get(), locals);
        }
        break;
    }
    case NodeKind::FunctionDecl:
        break;
    default:
        break;
    }
}

static void CollectIdentifiersFromExpr(const Expr *expr, std::vector<std::string> &used) {
    if (!expr) {
        return;
    }

    switch (expr->kind) {
    case NodeKind::Identifier: {
        const IdentifierExpr *id = static_cast<const IdentifierExpr *>(expr);
        used.push_back(id->name);
        break;
    }
    case NodeKind::BinaryExpr: {
        const BinaryExpr *b = static_cast<const BinaryExpr *>(expr);
        CollectIdentifiersFromExpr(b->lhs.get(), used);
        CollectIdentifiersFromExpr(b->rhs.get(), used);
        break;
    }
    case NodeKind::UnaryExpr: {
        const UnaryExpr *u = static_cast<const UnaryExpr *>(expr);
        CollectIdentifiersFromExpr(u->operand.get(), used);
        break;
    }
    case NodeKind::CallExpr: {
        const CallExpr *c = static_cast<const CallExpr *>(expr);
        CollectIdentifiersFromExpr(c->callee.get(), used);
        for (const auto &it : c->args) {
            CollectIdentifiersFromExpr(it.get(), used);
        }
        break;
    }
    case NodeKind::MemberExpr: {
        const MemberExpr *m = static_cast<const MemberExpr *>(expr);
        CollectIdentifiersFromExpr(m->object.get(), used);
        break;
    }
    case NodeKind::IndexExpr: {
        const IndexExpr *idx = static_cast<const IndexExpr *>(expr);
        CollectIdentifiersFromExpr(idx->object.get(), used);
        CollectIdentifiersFromExpr(idx->index.get(), used);
        CollectIdentifiersFromExpr(idx->index_end.get(), used);
        break;
    }
    case NodeKind::TernaryExpr: {
        const TernaryExpr *t = static_cast<const TernaryExpr *>(expr);
        CollectIdentifiersFromExpr(t->cond.get(), used);
        CollectIdentifiersFromExpr(t->then_expr.get(), used);
        CollectIdentifiersFromExpr(t->else_expr.get(), used);
        break;
    }
    case NodeKind::MappingLiteralExpr: {
        const MappingLiteralExpr *m = static_cast<const MappingLiteralExpr *>(expr);
        for (const auto &pair : m->pairs) {
            CollectIdentifiersFromExpr(pair.first.get(), used);
            CollectIdentifiersFromExpr(pair.second.get(), used);
        }
        break;
    }
    case NodeKind::LambdaExpr:
        break;
    default:
        break;
    }
}

static void CollectIdentifiersFromStmt(const Stmt *stmt, std::vector<std::string> &used) {
    if (!stmt) {
        return;
    }

    switch (stmt->kind) {
    case NodeKind::VarDecl: {
        const VarDeclStmt *vd = static_cast<const VarDeclStmt *>(stmt);
        CollectIdentifiersFromExpr(vd->init.get(), used);
        break;
    }
    case NodeKind::ExprStmt: {
        const ExprStmt *es = static_cast<const ExprStmt *>(stmt);
        CollectIdentifiersFromExpr(es->expr.get(), used);
        break;
    }
    case NodeKind::ReturnStmt: {
        const ReturnStmt *r = static_cast<const ReturnStmt *>(stmt);
        CollectIdentifiersFromExpr(r->value.get(), used);
        break;
    }
    case NodeKind::IfStmt: {
        const IfStmt *is = static_cast<const IfStmt *>(stmt);
        CollectIdentifiersFromExpr(is->cond.get(), used);
        CollectIdentifiersFromStmt(is->then_branch.get(), used);
        CollectIdentifiersFromStmt(is->else_branch.get(), used);
        break;
    }
    case NodeKind::WhileStmt: {
        const WhileStmt *ws = static_cast<const WhileStmt *>(stmt);
        CollectIdentifiersFromExpr(ws->cond.get(), used);
        CollectIdentifiersFromStmt(ws->body.get(), used);
        break;
    }
    case NodeKind::DoWhileStmt: {
        const DoWhileStmt *dw = static_cast<const DoWhileStmt *>(stmt);
        CollectIdentifiersFromStmt(dw->body.get(), used);
        CollectIdentifiersFromExpr(dw->cond.get(), used);
        break;
    }
    case NodeKind::ForStmt: {
        const ForStmt *fs = static_cast<const ForStmt *>(stmt);
        CollectIdentifiersFromStmt(fs->init.get(), used);
        CollectIdentifiersFromExpr(fs->cond.get(), used);
        CollectIdentifiersFromExpr(fs->post.get(), used);
        CollectIdentifiersFromStmt(fs->body.get(), used);
        break;
    }
    case NodeKind::ForeachStmt: {
        const ForeachStmt *fe = static_cast<const ForeachStmt *>(stmt);
        CollectIdentifiersFromExpr(fe->container.get(), used);
        CollectIdentifiersFromStmt(fe->body.get(), used);
        break;
    }
    case NodeKind::SwitchStmt: {
        const SwitchStmt *sw = static_cast<const SwitchStmt *>(stmt);
        CollectIdentifiersFromExpr(sw->selector.get(), used);
        for (const auto &cs : sw->cases) {
            if (!cs->is_default) {
                CollectIdentifiersFromExpr(cs->selector.get(), used);
            }
            for (const auto &st : cs->body) {
                CollectIdentifiersFromStmt(st.get(), used);
            }
        }
        break;
    }
    case NodeKind::Block: {
        const BlockStmt *b = static_cast<const BlockStmt *>(stmt);
        for (const auto &it : b->stmts) {
            CollectIdentifiersFromStmt(it.get(), used);
        }
        break;
    }
    default:
        break;
    }
}

static bool CaseEndsWithTerminator(const CaseStmt *cs) {
    if (!cs || cs->body.empty()) {
        return false;
    }
    const Stmt *tail = cs->body.back().get();
    if (!tail) {
        return false;
    }
    return tail->kind == NodeKind::BreakStmt || tail->kind == NodeKind::ReturnStmt;
}

static bool StmtDefinitelyReturns(const Stmt *stmt) {
    if (!stmt) {
        return false;
    }

    switch (stmt->kind) {
    case NodeKind::ReturnStmt:
        return true;
    case NodeKind::Block: {
        const BlockStmt *b = static_cast<const BlockStmt *>(stmt);
        for (const auto &it : b->stmts) {
            if (StmtDefinitelyReturns(it.get())) {
                return true;
            }
        }
        return false;
    }
    case NodeKind::IfStmt: {
        const IfStmt *is = static_cast<const IfStmt *>(stmt);
        return StmtDefinitelyReturns(is->then_branch.get()) && StmtDefinitelyReturns(is->else_branch.get());
    }
    default:
        return false;
    }
}

static bool FunctionDefinitelyReturns(const FunctionDecl *func) {
    if (!func) {
        return false;
    }
    for (const auto &st : func->body) {
        if (StmtDefinitelyReturns(st.get())) {
            return true;
        }
    }
    return false;
}

SemanticModel Sema2::Analyze(const Module &module) {
    model_ = SemanticModel();
    scopes_.clear();
    function_arity_.clear();

    for (const auto &decl : module.decls) {
        if (decl && decl->kind == NodeKind::FunctionDecl) {
            const FunctionDecl *fn = static_cast<const FunctionDecl *>(decl.get());
            function_arity_[fn->name] = static_cast<int>(fn->params.size());
        }
    }
    EnterScope();

    for (const auto &decl : module.decls) {
        VisitStmt(decl.get());
    }

    ExitScope();
    return model_;
}

void Sema2::VisitStmt(const Stmt *stmt) {
    if (!stmt) {
        return;
    }

    switch (stmt->kind) {
    case NodeKind::VarDecl: {
        const VarDeclStmt *vd = static_cast<const VarDeclStmt *>(stmt);
        const std::string declared_type = vd->declared_type.empty() ? "mixed" : vd->declared_type;
        Declare(vd->name, declared_type);
        if (current_function_) {
            model_.functions[current_function_].locals.push_back(vd->name);
        } else {
            model_.global_variables.push_back(vd->name);
        }
        if (vd->init) {
            VisitExpr(vd->init.get());
            const auto it_type = model_.expr_type.find(vd->init.get());
            const std::string init_type = (it_type == model_.expr_type.end()) ? "mixed" : it_type->second;
            if (!IsTypeAssignable(declared_type, init_type)) {
                diag_->Add(
                    DiagnosticLevel::Error,
                    vd->span,
                    "type mismatch in variable initialization: cannot assign '" + init_type + "' to '" + declared_type + "'");
            }
            if (declared_type == "mixed" && !scopes_.empty()) {
                auto it = model_.expr_type.find(vd->init.get());
                if (it != model_.expr_type.end() && !it->second.empty() && it->second != "mixed") {
                    scopes_.back().types[vd->name] = it->second;
                }
            }
        }
        break;
    }
    case NodeKind::FunctionDecl: {
        AnalyzeFunction(static_cast<const FunctionDecl *>(stmt));
        break;
    }
    case NodeKind::ClassDecl: {
        const ClassDecl *cl = static_cast<const ClassDecl *>(stmt);
        Declare(cl->name);
        model_.class_order.push_back(cl->name);
        model_.class_fields[cl->name] = cl->fields;
        break;
    }
    case NodeKind::InheritDecl: {
        const InheritDecl *inh = static_cast<const InheritDecl *>(stmt);
        Declare(inh->parent_name);
        break;
    }
    case NodeKind::Block: {
        const BlockStmt *b = static_cast<const BlockStmt *>(stmt);
        EnterScope();
        for (const auto &it : b->stmts) {
            VisitStmt(it.get());
        }
        ExitScope();
        break;
    }
    case NodeKind::ReturnStmt: {
        const ReturnStmt *r = static_cast<const ReturnStmt *>(stmt);
        if (r->value) {
            VisitExpr(r->value.get());
        }
        if (current_function_ && !current_function_->return_type.empty()) {
            const std::string ret_type = current_function_->return_type;
            if (ret_type == "void") {
                if (r->value) {
                    diag_->Add(
                        DiagnosticLevel::Error,
                        r->span,
                        "void function should not return a value");
                }
            } else {
                if (!r->value) {
                    diag_->Add(
                        DiagnosticLevel::Error,
                        r->span,
                        "non-void function must return a value of type '" + ret_type + "'");
                } else {
                    const auto it_type = model_.expr_type.find(r->value.get());
                    const std::string value_type = (it_type == model_.expr_type.end()) ? "mixed" : it_type->second;
                    if (!IsTypeAssignable(ret_type, value_type)) {
                        diag_->Add(
                            DiagnosticLevel::Error,
                            r->span,
                            "return type mismatch: cannot return '" + value_type + "' as '" + ret_type + "'");
                    }
                }
            }
        }
        break;
    }
    case NodeKind::IfStmt: {
        const IfStmt *is = static_cast<const IfStmt *>(stmt);
        VisitExpr(is->cond.get());
        VisitStmt(is->then_branch.get());
        VisitStmt(is->else_branch.get());
        break;
    }
    case NodeKind::WhileStmt: {
        const WhileStmt *ws = static_cast<const WhileStmt *>(stmt);
        ++loop_depth_;
        VisitExpr(ws->cond.get());
        VisitStmt(ws->body.get());
        --loop_depth_;
        break;
    }
    case NodeKind::DoWhileStmt: {
        const DoWhileStmt *dw = static_cast<const DoWhileStmt *>(stmt);
        ++loop_depth_;
        VisitStmt(dw->body.get());
        VisitExpr(dw->cond.get());
        --loop_depth_;
        break;
    }
    case NodeKind::ForStmt: {
        const ForStmt *fs = static_cast<const ForStmt *>(stmt);
        EnterScope();
        ++loop_depth_;
        VisitStmt(fs->init.get());
        VisitExpr(fs->cond.get());
        VisitExpr(fs->post.get());
        VisitStmt(fs->body.get());
        --loop_depth_;
        ExitScope();
        break;
    }
    case NodeKind::ForeachStmt: {
        const ForeachStmt *fe = static_cast<const ForeachStmt *>(stmt);
        EnterScope();
        ++loop_depth_;
        if (!fe->first_name.empty()) {
            Declare(fe->first_name);
            if (current_function_) {
                model_.functions[current_function_].locals.push_back(fe->first_name);
            }
        }
        if (!fe->second_name.empty()) {
            Declare(fe->second_name);
            if (current_function_) {
                model_.functions[current_function_].locals.push_back(fe->second_name);
            }
        }
        VisitExpr(fe->container.get());
        VisitStmt(fe->body.get());
        --loop_depth_;
        ExitScope();
        break;
    }
    case NodeKind::SwitchStmt: {
        const SwitchStmt *sw = static_cast<const SwitchStmt *>(stmt);
        VisitExpr(sw->selector.get());
        EnterScope();
        ++switch_depth_;
        for (int i = 0; i < static_cast<int>(sw->cases.size()); ++i) {
            const auto &cs = sw->cases[i];
            if (!cs->is_default) {
                VisitExpr(cs->selector.get());
            }
            for (const auto &st : cs->body) {
                VisitStmt(st.get());
            }

            if (i + 1 < static_cast<int>(sw->cases.size()) && !CaseEndsWithTerminator(cs.get())) {
                diag_->Add(
                    DiagnosticLevel::Warning,
                    cs->span,
                    "switch fallthrough is disabled in frontend2; add explicit break/return for clarity");
            }
        }
        --switch_depth_;
        ExitScope();
        break;
    }
    case NodeKind::BreakStmt:
    case NodeKind::ContinueStmt: {
        if (stmt->kind == NodeKind::ContinueStmt) {
            if (loop_depth_ <= 0) {
                diag_->Add(DiagnosticLevel::Error, stmt->span, "continue only valid inside loop");
            }
        } else {
            if (loop_depth_ <= 0 && switch_depth_ <= 0) {
                diag_->Add(DiagnosticLevel::Error, stmt->span, "break only valid inside loop or switch");
            }
        }
        break;
    }
    case NodeKind::CatchStmt: {
        const CatchStmt *cs = static_cast<const CatchStmt *>(stmt);
        EnterScope();
        VisitStmt(cs->body.get());
        ExitScope();
        break;
    }
    case NodeKind::ExprStmt: {
        const ExprStmt *e = static_cast<const ExprStmt *>(stmt);
        if (e->expr) {
            VisitExpr(e->expr.get());
        }
        break;
    }
    default:
        break;
    }
}

void Sema2::VisitExpr(const Expr *expr) {
    if (!expr) {
        return;
    }

    switch (expr->kind) {
    case NodeKind::Identifier: {
        const IdentifierExpr *id = static_cast<const IdentifierExpr *>(expr);
        if (!Resolve(id->name)) {
            diag_->Add(DiagnosticLevel::Error, expr->span, "Undefined identifier: " + id->name);
        }
        model_.resolved_symbol[expr] = id->name;
        model_.expr_type[expr] = ResolveType(id->name);
        break;
    }
    case NodeKind::UnaryExpr: {
        const UnaryExpr *u = static_cast<const UnaryExpr *>(expr);
        VisitExpr(u->operand.get());
        if (u->op == "!" || u->op == "++" || u->op == "--") {
            model_.expr_type[expr] = "int";
        } else if (u->op == "~") {
            model_.expr_type[expr] = "int";
        } else {
            model_.expr_type[expr] = model_.expr_type.count(u->operand.get()) ? model_.expr_type[u->operand.get()] : "mixed";
        }
        break;
    }
    case NodeKind::BinaryExpr: {
        const BinaryExpr *bin = static_cast<const BinaryExpr *>(expr);
        VisitExpr(bin->lhs.get());
        VisitExpr(bin->rhs.get());
        if (bin->op == "==" || bin->op == "!=" || bin->op == ">" || bin->op == ">=" || bin->op == "<" || bin->op == "<=" ||
            bin->op == "&&" || bin->op == "||") {
            model_.expr_type[expr] = "int";
        } else if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" || bin->op == "%" ||
            bin->op == "<<" || bin->op == ">>" || bin->op == "&" || bin->op == "|" || bin->op == "^") {
            model_.expr_type[expr] = "int";
        } else {
            model_.expr_type[expr] = model_.expr_type.count(bin->rhs.get()) ? model_.expr_type[bin->rhs.get()] : "mixed";
        }

        if (IsAssignOp(bin->op) && bin->lhs && bin->lhs->kind == NodeKind::Identifier) {
            const IdentifierExpr *lhs_id = static_cast<const IdentifierExpr *>(bin->lhs.get());
            const std::string lhs_type = ResolveType(lhs_id->name);
            const auto rhs_it = model_.expr_type.find(bin->rhs.get());
            const std::string rhs_type = (rhs_it == model_.expr_type.end()) ? "mixed" : rhs_it->second;
            if (!IsTypeAssignable(lhs_type, rhs_type)) {
                diag_->Add(
                    DiagnosticLevel::Error,
                    expr->span,
                    "type mismatch in assignment: cannot assign '" + rhs_type + "' to '" + lhs_type + "'");
            }
        }
        break;
    }
    case NodeKind::CallExpr: {
        const CallExpr *call = static_cast<const CallExpr *>(expr);
        VisitExpr(call->callee.get());
        for (const auto &it : call->args) {
            VisitExpr(it.get());
        }
        if (call->callee && call->callee->kind == NodeKind::Identifier) {
            const IdentifierExpr *id = static_cast<const IdentifierExpr *>(call->callee.get());
            auto iter = function_arity_.find(id->name);
            if (iter != function_arity_.end() && iter->second != static_cast<int>(call->args.size())) {
                diag_->Add(
                    DiagnosticLevel::Warning,
                    expr->span,
                    "call arity mismatch for function: " + id->name);
            }
        }
        model_.expr_type[expr] = "mixed";
        break;
    }
    case NodeKind::LambdaExpr: {
        AnalyzeLambda(static_cast<const LambdaExpr *>(expr), current_function_);
        break;
    }
    case NodeKind::NewExpr: {
        const NewExpr *ne = static_cast<const NewExpr *>(expr);
        if (ne->is_array_literal) {
            for (const auto &it : ne->array_items) {
                VisitExpr(it.get());
            }
            model_.expr_type[expr] = "array";
            break;
        }
        if (!Resolve(ne->class_name) && !model_.class_fields.count(ne->class_name)) {
            diag_->Add(DiagnosticLevel::Error, expr->span, "new unknown class: " + ne->class_name);
        }
        model_.expr_type[expr] = ne->class_name;
        break;
    }
    case NodeKind::MemberExpr: {
        const MemberExpr *m = static_cast<const MemberExpr *>(expr);
        VisitExpr(m->object.get());
        std::string object_type = "";
        if (model_.expr_type.count(m->object.get())) {
            object_type = model_.expr_type[m->object.get()];
        }
        if (!object_type.empty() && model_.class_fields.count(object_type)) {
            const auto &fields = model_.class_fields[object_type];
            int found = -1;
            for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
                if (fields[i] == m->member) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                model_.member_field_index[expr] = found;
                model_.expr_type[expr] = "mixed";
            } else {
                diag_->Add(DiagnosticLevel::Error, expr->span, "unknown class field: " + object_type + "->" + m->member);
                model_.expr_type[expr] = "mixed";
            }
        } else {
            model_.expr_type[expr] = "mixed";
        }
        break;
    }
    case NodeKind::IndexExpr: {
        const IndexExpr *idx = static_cast<const IndexExpr *>(expr);
        VisitExpr(idx->object.get());
        VisitExpr(idx->index.get());
        VisitExpr(idx->index_end.get());
        model_.expr_type[expr] = "mixed";
        break;
    }
    case NodeKind::TernaryExpr: {
        const TernaryExpr *t = static_cast<const TernaryExpr *>(expr);
        VisitExpr(t->cond.get());
        VisitExpr(t->then_expr.get());
        VisitExpr(t->else_expr.get());
        model_.expr_type[expr] = "mixed";
        break;
    }
    case NodeKind::MappingLiteralExpr: {
        const MappingLiteralExpr *m = static_cast<const MappingLiteralExpr *>(expr);
        for (const auto &pair : m->pairs) {
            VisitExpr(pair.first.get());
            VisitExpr(pair.second.get());
        }
        model_.expr_type[expr] = "mapping";
        break;
    }
    case NodeKind::Number: {
        model_.expr_type[expr] = "int";
        break;
    }
    case NodeKind::Float: {
        model_.expr_type[expr] = "float";
        break;
    }
    case NodeKind::String: {
        model_.expr_type[expr] = "string";
        break;
    }
    default:
        break;
    }
}

void Sema2::EnterScope() {
    scopes_.push_back(Scope());
}

void Sema2::ExitScope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

void Sema2::Declare(const std::string &name, const std::string &declared_type) {
    if (scopes_.empty()) {
        EnterScope();
    }
    if (scopes_.back().names.count(name)) {
        diag_->Add(DiagnosticLevel::Warning, SourceSpan(), "shadow/redeclare in same scope: " + name);
    }
    scopes_.back().names[name] = true;
    scopes_.back().types[name] = declared_type;
    scopes_.back().local_index[name] = static_cast<int>(scopes_.back().local_index.size());
}

bool Sema2::Resolve(const std::string &name) const {
    for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
        if (scopes_[i].names.count(name)) {
            return true;
        }
    }
    return IsEfun(name);
}

static const char *kEfunNames[] = {
    "call_other",
    "print",
    "puts",
    "sleep",
    "sizeof",
    "random",
    "keys",
    "values",
    "typeof",
    "to_string",
    "to_int",
    "this_object",
    "clone_object",
    "destruct",
    "sprintf",
    "write",
    "time",
    "member_array",
    "explode",
    "implode",
    "stringp",
    "intp",
    "floatp",
    "arrayp",
    "mappingp",
    "objectp",
    "nullp",
    "functionp",
    "to_float",
    "abs",
    "strlen",
    "map_delete",
    "capitalize",
    "lower_case",
    "upper_case",
    "allocate",
    "reverse",
    "min",
    "max",
    "sqrt",
    "ctime",
    "strsrch",
    "replace_string",
    "sort_array",
};
static constexpr int kEfunCount = sizeof(kEfunNames) / sizeof(kEfunNames[0]);

bool Sema2::IsEfun(const std::string &name) {
    for (int i = 0; i < kEfunCount; ++i) {
        if (name == kEfunNames[i]) {
            return true;
        }
    }
    return false;
}

int Sema2::EfunIndex(const std::string &name) {
    for (int i = 0; i < kEfunCount; ++i) {
        if (name == kEfunNames[i]) {
            return i;
        }
    }
    return -1;
}

std::string Sema2::ResolveType(const std::string &name) const {
    for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
        auto it = scopes_[i].types.find(name);
        if (it != scopes_[i].types.end()) {
            return it->second;
        }
    }
    return "mixed";
}

void Sema2::AnalyzeFunction(const FunctionDecl *func) {
    if (!func) {
        return;
    }

    FunctionSemanticInfo info;
    info.name = func->name;
    info.params = func->params;
    model_.functions[func] = info;

    Declare(func->name);

    const FunctionDecl *prev = current_function_;
    current_function_ = func;

    EnterScope();
    scopes_.back().is_function_scope = true;
    scopes_.back().owner = func;
    for (const auto &param : func->params) {
        std::string ptype = "mixed";
        int pi = static_cast<int>(&param - &func->params[0]);
        if (pi >= 0 && pi < static_cast<int>(func->param_types.size()) && !func->param_types[pi].empty()) {
            ptype = func->param_types[pi];
        }
        Declare(param, ptype);
    }
    for (const auto &st : func->body) {
        VisitStmt(st.get());
    }

    if (!func->return_type.empty() && func->return_type != "void") {
        if (!FunctionDefinitelyReturns(func)) {
            diag_->Add(
                DiagnosticLevel::Error,
                func->span,
                "non-void function may exit without returning a value: " + func->name);
        }
    }
    ExitScope();

    current_function_ = prev;
}

void Sema2::AnalyzeLambda(const LambdaExpr *lambda, const FunctionDecl *owner) {
    if (!lambda || !owner) {
        return;
    }

    FunctionSemanticInfo info;
    info.name = "<lambda>";
    info.params = lambda->params;

    std::set<std::string> lambda_locals;
    for (const auto &param : lambda->params) {
        lambda_locals.insert(param);
    }
    for (const auto &st : lambda->body) {
        CollectLocalDeclsFromStmt(st.get(), lambda_locals);
    }

    std::vector<std::string> actual_locals;
    for (const auto &name : lambda_locals) {
        bool is_param = false;
        for (const auto &p : lambda->params) {
            if (p == name) { is_param = true; break; }
        }
        if (!is_param) {
            actual_locals.push_back(name);
        }
    }
    info.locals = actual_locals;

    std::vector<std::string> used;
    for (const auto &st : lambda->body) {
        CollectIdentifiersFromStmt(st.get(), used);
    }

    std::set<std::string> uniq;
    for (const auto &name : used) {
        if (lambda_locals.count(name)) {
            continue;
        }
        if (!Resolve(name)) {
            continue;
        }
        bool is_global = false;
        for (const auto &gv : model_.global_variables) {
            if (gv == name) { is_global = true; break; }
        }
        if (is_global) {
            continue;
        }
        uniq.insert(name);
    }

    for (const auto &name : uniq) {
        info.captures.push_back(name);

        int found_scope = -1;
        for (int si = static_cast<int>(scopes_.size()) - 1; si >= 0; --si) {
            if (scopes_[si].names.count(name)) {
                found_scope = si;
                break;
            }
        }

        int source_kind = 0;
        int source_index = -1;
        if (found_scope >= 0) {
            source_index = scopes_[found_scope].local_index.count(name) ? scopes_[found_scope].local_index[name] : -1;
            source_kind = scopes_[found_scope].is_function_scope ? 0 : 1;
        }
        info.capture_source_kind.push_back(source_kind);
        info.capture_source_index.push_back(source_index);
    }

    model_.lambdas[lambda] = info;
}

} // namespace frontend2
} // namespace lpc
